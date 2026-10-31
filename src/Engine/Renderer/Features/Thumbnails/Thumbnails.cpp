#include "Renderer/Features/Thumbnails/Thumbnails.h"

#include "Bbox.h"
#include "Flatten.h"
#include "Paths.h"
#include "Renderer/Device/Formats.h"
#include "Renderer/Device/Pipelines.h"
#include "Renderer/Device/ResourceManager.h"
#include "Renderer/Device/VulkanContext.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>

namespace batap
{
namespace
{
constexpr float kFov = 0.6f;
constexpr uint32_t kTileMask = 0xFFFFu;
constexpr VkFormat kAtlasFormat = VK_FORMAT_R8G8B8A8_UNORM;

void updateBuffer(VkCommandBuffer cmd, VkBuffer buffer, std::span<const std::byte> bytes)
{
    constexpr size_t kChunk = 65536;
    for (size_t offset = 0; offset < bytes.size(); offset += kChunk)
    {
        const std::span<const std::byte> chunk =
            bytes.subspan(offset, std::min(kChunk, bytes.size() - offset));
        vkCmdUpdateBuffer(cmd, buffer, offset, chunk.size(), chunk.data());
    }
}

m4f lookAt(const v3f& eye, const v3f& target, const v3f& up)
{
    const v3f f = (target - eye).normalized();
    const v3f s = f.cross(up).normalized();
    const v3f u = s.cross(f);
    m4f m = m4f::Identity();
    m.row(0) << s.x(), s.y(), s.z(), -s.dot(eye);
    m.row(1) << u.x(), u.y(), u.z(), -u.dot(eye);
    m.row(2) << -f.x(), -f.y(), -f.z(), f.dot(eye);
    return m;
}

m4f perspective(float fov, float znear)
{
    const float f = 1.0f / std::tan(fov * 0.5f);
    m4f m = m4f::Zero();
    m(0, 0) = f;
    m(1, 1) = f;
    m(2, 3) = znear;
    m(3, 2) = -1.0f;
    return m;
}

AABB worldBounds(const ThumbnailDraw& draw)
{
    AABB out;
    const AABB& local = draw.mesh_.localBounds_;
    for (int corner = 0; corner < 8; ++corner)
    {
        const v3f p{(corner & 1) ? local.max_.x() : local.min_.x(),
                    (corner & 2) ? local.max_.y() : local.min_.y(),
                    (corner & 4) ? local.max_.z() : local.min_.z()};
        out.extend((draw.world_ * p.homogeneous()).head<3>());
    }
    return out;
}

void fillMatrices(ThumbnailDrawGPUData& data, const m4f& worldView, const m4f& proj)
{
    flatten(data.worldView_, worldView);
    flatten(data.proj_, proj);
    const m3f linear = worldView.topLeftCorner<3, 3>();
    m4f normal = m4f::Identity();
    if (std::abs(linear.determinant()) > 1e-12f)
        normal.topLeftCorner<3, 3>() = linear.inverse().transpose();
    else
        normal.topLeftCorner<3, 3>() = linear;
    flatten(data.normalView_, normal);
}
}  // namespace

Thumbnails::Thumbnails(VulkanContext& ctx, ResourceManager& resources, VkFormat depthFormat)
    : ctx_(ctx), resources_(resources), depthFormat_(depthFormat)
{
    atlas_ = resources_.createTarget(
        AtlasSize, AtlasSize, kAtlasFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "thumbnail atlas");
    draws_ =
        resources_.createPerFrameBuffer(MaxDraws * sizeof(ThumbnailDrawGPUData), "thumbnail draws");
    materials_ =
        resources_.createPerFrameBuffer(MaxMaterials * sizeof(Material), "thumbnail materials");
    sphere_ = buildSphere();
    quad_ = buildQuad();

    const VkDevice device = ctx_.device_;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS)
        throw std::runtime_error("Thumbnails : set layout");

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * FramesInFlight};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = FramesInFlight;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS)
        throw std::runtime_error("Thumbnails : descriptor pool");

    std::array<VkDescriptorSetLayout, FramesInFlight> layouts;
    layouts.fill(setLayout_);
    VkDescriptorSetAllocateInfo setInfo{};
    setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setInfo.descriptorPool = pool_;
    setInfo.descriptorSetCount = FramesInFlight;
    setInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &setInfo, sets_.data()) != VK_SUCCESS)
        throw std::runtime_error("Thumbnails : descriptor sets");

    const std::array<VkDescriptorSetLayout, 2> setLayouts{resources_.textureSetLayout(),
                                                          setLayout_};
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.size = sizeof(ThumbnailPush);
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &layout_) != VK_SUCCESS)
        throw std::runtime_error("Thumbnails : pipeline layout");

    const std::string shaderDir = resolveEngineFile("shaders", "shaders");
    const ShaderModule vs(device, shaderDir + "/ThumbnailVS.spv");
    const ShaderModule ps(device, shaderDir + "/ThumbnailPS.spv");
    pipeline_ = GraphicsPipelineBuilder()
                    .shaders(vs, ps)
                    .vertexAttribute(Mesh::Position, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .vertexAttribute(Mesh::Normal, VK_FORMAT_R32G32B32_SFLOAT, 12)
                    .vertexAttribute(Mesh::UV0, VK_FORMAT_R32G32_SFLOAT, 8)
                    .vertexAttribute(Mesh::Tangent, VK_FORMAT_R32G32B32A32_SFLOAT, 16)
                    .cullBack()
                    .colorFormat(kAtlasFormat)
                    .depth(depthFormat, true, VK_COMPARE_OP_GREATER)
                    .build(device, layout_);
}

Thumbnails::~Thumbnails()
{
    vkDestroyPipeline(ctx_.device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(ctx_.device_, layout_, nullptr);
    vkDestroyDescriptorPool(ctx_.device_, pool_, nullptr);
    vkDestroyDescriptorSetLayout(ctx_.device_, setLayout_, nullptr);
    resources_.requestDestroy(atlas_);
    resources_.requestDestroy(draws_);
    resources_.requestDestroy(materials_);
    resources_.requestDestroy(sphere_.buffer_);
    resources_.requestDestroy(quad_.buffer_);
}

Thumbnails::Id Thumbnails::allocate()
{
    for (uint32_t i = 0; i < TileCount; ++i)
    {
        Tile& tile = tiles_[i];
        if (tile.used_)
            continue;
        tile.used_ = true;
        tile.pending_ = false;
        tile.ready_ = false;
        tile.draws_.clear();
        return (tile.generation_ << 16) | i;
    }
    return None;
}

Thumbnails::Tile* Thumbnails::find(Id id)
{
    if (id == None)
        return nullptr;
    const uint32_t index = id & kTileMask;
    if (index >= TileCount)
        return nullptr;
    Tile& tile = tiles_[index];
    return tile.used_ && tile.generation_ == (id >> 16) ? &tile : nullptr;
}

const Thumbnails::Tile* Thumbnails::find(Id id) const
{
    return const_cast<Thumbnails*>(this)->find(id);
}

Thumbnails::Id Thumbnails::addScene(std::vector<ThumbnailDraw> draws)
{
    AABB bounds;
    for (const ThumbnailDraw& draw : draws)
        if (draw.mesh_.isRenderable() && draw.mesh_.localBounds_.valid())
            bounds.extend(worldBounds(draw));
    if (!bounds.valid())
        return None;
    return addView(std::move(draws), (bounds.min_ + bounds.max_) * 0.5f,
                   (bounds.max_ - bounds.min_).norm() * 0.5f);
}

Thumbnails::Id Thumbnails::addView(std::vector<ThumbnailDraw> draws, const v3f& centre,
                                   float boundingRadius)
{
    const Id id = allocate();
    if (id == None)
        return None;
    Tile& tile = tiles_[id & kTileMask];

    const float radius = std::max(boundingRadius, 1e-3f);
    const float distance = radius / std::sin(kFov * 0.5f) * 1.05f;
    const v3f dir = v3f(1.0f, 0.8f, 1.35f).normalized();
    const m4f view = lookAt(centre + dir * distance, centre, v3f::UnitY());
    const m4f proj = perspective(kFov, std::max((distance - radius) * 0.5f, 1e-3f));

    for (const ThumbnailDraw& draw : draws)
    {
        if (!draw.mesh_.isRenderable())
            continue;
        Batch& batch = tile.draws_.emplace_back();
        batch.mesh_ = draw.mesh_;
        fillMatrices(batch.data_, view * draw.world_, proj);
        batch.materials_ = draw.materials_;
        if (batch.materials_.empty())
            batch.materials_.emplace_back();
        batch.data_.textureIndex_ = InvalidGPUIndex;
        batch.data_.flags_ = 0;
    }
    tile.pending_ = true;
    return id;
}

Thumbnails::Id Thumbnails::addMaterial(const Material& material)
{
    ThumbnailDraw draw;
    draw.mesh_ = sphere_;
    draw.materials_.push_back(material);
    std::vector<ThumbnailDraw> draws;
    draws.push_back(draw);
    return addView(std::move(draws), v3f::Zero(), 1.1f);
}

Thumbnails::Id Thumbnails::addImage(uint32_t textureIndex, uint32_t width, uint32_t height,
                                    bool hdr)
{
    if (textureIndex == InvalidGPUIndex || width == 0 || height == 0)
        return None;
    const Id id = allocate();
    if (id == None)
        return None;
    Tile& tile = tiles_[id & kTileMask];

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    m4f worldView = m4f::Identity();
    worldView(0, 0) = (aspect >= 1.0f ? 1.0f : aspect) * 0.96f;
    worldView(1, 1) = (aspect >= 1.0f ? 1.0f / aspect : 1.0f) * 0.96f;
    m4f proj = m4f::Identity();
    proj(2, 2) = 0.0f;
    proj(2, 3) = 0.5f;

    Batch& batch = tile.draws_.emplace_back();
    batch.mesh_ = quad_;
    fillMatrices(batch.data_, worldView, proj);
    batch.data_.textureIndex_ = textureIndex;
    batch.data_.flags_ = ThumbnailImage | (hdr ? ThumbnailImageHdr : 0u);
    tile.pending_ = true;
    return id;
}

void Thumbnails::remove(Id id)
{
    Tile* tile = find(id);
    if (!tile)
        return;
    tile->used_ = false;
    tile->pending_ = false;
    tile->ready_ = false;
    tile->draws_.clear();
    tile->generation_ = tile->generation_ >= kTileMask ? 1u : tile->generation_ + 1u;
}

bool Thumbnails::ready(Id id) const
{
    const Tile* tile = find(id);
    return tile && tile->ready_;
}

ImVec2 Thumbnails::uv0(Id id) const
{
    const uint32_t index = id & kTileMask;
    const auto col = static_cast<float>(index % TilesPerRow);
    const auto row = static_cast<float>(index / TilesPerRow);
    constexpr auto tile = static_cast<float>(TileSize);
    constexpr auto atlas = static_cast<float>(AtlasSize);
    return {(col * tile + 0.5f) / atlas, (row * tile + 0.5f) / atlas};
}

ImVec2 Thumbnails::uv1(Id id) const
{
    const uint32_t index = id & kTileMask;
    const auto col = static_cast<float>(index % TilesPerRow);
    const auto row = static_cast<float>(index / TilesPerRow);
    constexpr auto tile = static_cast<float>(TileSize);
    constexpr auto atlas = static_cast<float>(AtlasSize);
    return {((col + 1.0f) * tile - 0.5f) / atlas, ((row + 1.0f) * tile - 0.5f) / atlas};
}

void Thumbnails::writeSet(uint32_t frame)
{
    const std::array<VkDescriptorBufferInfo, 2> infos{{
        {resources_.bufferFor(materials_), 0, VK_WHOLE_SIZE},
        {resources_.bufferFor(draws_), 0, VK_WHOLE_SIZE},
    }};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = sets_[frame];
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(ctx_.device_, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
}

std::optional<GraphImage> Thumbnails::addPasses(RenderGraph& graph, uint32_t frame,
                                                GraphBuffer uploads)
{
    std::vector<Work> work;
    std::vector<Draw> drawList;
    std::vector<ThumbnailDrawGPUData> data;
    std::vector<Material> materials;
    for (uint32_t t = 0; t < TileCount && work.size() < kTilesPerFrame; ++t)
    {
        Tile& tile = tiles_[t];
        if (!tile.used_ || !tile.pending_)
            continue;
        size_t tileMaterials = 0;
        for (const Batch& batch : tile.draws_)
            tileMaterials += batch.materials_.size();
        const bool fits = data.size() + tile.draws_.size() <= MaxDraws &&
                          materials.size() + tileMaterials <= MaxMaterials;
        if (!fits && !work.empty())
            break;
        if (tile.draws_.size() > MaxDraws || tileMaterials > MaxMaterials)
            tile.draws_.clear();
        work.push_back(
            {t, static_cast<uint32_t>(data.size()), static_cast<uint32_t>(tile.draws_.size())});
        for (Batch& batch : tile.draws_)
        {
            batch.data_.materialBase_ = static_cast<uint32_t>(materials.size());
            batch.data_.materialCount_ = static_cast<uint32_t>(batch.materials_.size());
            materials.insert(materials.end(), batch.materials_.begin(), batch.materials_.end());
            drawList.push_back({batch.mesh_, static_cast<uint32_t>(data.size())});
            data.push_back(batch.data_);
        }
        tile.pending_ = false;
        tile.ready_ = true;
        tile.draws_.clear();
    }
    if (work.empty())
        return std::nullopt;

    writeSet(frame);

    GraphImageInfo info;
    info.image_ = resources_.imageFor(atlas_);
    info.view_ = resources_.viewFor(atlas_);
    info.extent_ = {AtlasSize, AtlasSize};
    info.textureIndex_ = resources_.textureIndex(atlas_);
    info.resource_ = atlas_;
    const GraphImage atlas = graph.importImage("thumbnail atlas", info, Usage::ShaderRead);
    const GraphImage depth =
        graph.createImage("thumbnail depth", {{AtlasSize, AtlasSize}, depthFormat_});
    const GraphBuffer draws =
        graph.importBuffer("thumbnail draws", resources_, draws_, Usage::None, Usage::None);
    const GraphBuffer mats =
        graph.importBuffer("thumbnail materials", resources_, materials_, Usage::None, Usage::None);

    graph.addPass("thumbnail data")
        .write(draws, Usage::TransferDst)
        .write(mats, Usage::TransferDst)
        .execute(
            [draws, mats, drawData = std::move(data),
             materialData = std::move(materials)](GraphContext& c)
            {
                updateBuffer(c.cmd(), c.buffer(draws), std::as_bytes(std::span{drawData}));
                updateBuffer(c.cmd(), c.buffer(mats), std::as_bytes(std::span{materialData}));
            });

    graph.addPass("thumbnails")
        .color(atlas, cleared_ ? Load::Keep : Load::Clear, VkClearColorValue{{0.f, 0.f, 0.f, 0.f}})
        .depth(depth, Load::Clear, 0.0f)
        .read(uploads, Usage::AnyRead)
        .read(draws, Usage::ShaderRead)
        .read(mats, Usage::ShaderRead)
        .execute([this, frame, tiles = std::move(work), list = std::move(drawList)](GraphContext& c)
                 { record(c.cmd(), frame, tiles, list); });
    cleared_ = true;
    return atlas;
}

void Thumbnails::record(VkCommandBuffer cmd, uint32_t frame, const std::vector<Work>& work,
                        const std::vector<Draw>& draws)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const std::array<VkDescriptorSet, 2> sets{resources_.textureSet(), sets_[frame]};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (const Work& item : work)
    {
        const uint32_t x = (item.tile_ % TilesPerRow) * TileSize;
        const uint32_t y = (item.tile_ / TilesPerRow) * TileSize;
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clear.colorAttachment = 0;
        clear.clearValue.color = VkClearColorValue{{0.f, 0.f, 0.f, 0.f}};
        VkClearRect rect{};
        rect.rect = {{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {TileSize, TileSize}};
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
        setViewportYUpRect(cmd, x, y, TileSize, TileSize);

        for (uint32_t i = 0; i < item.count_; ++i)
        {
            const Draw& draw = draws[item.first_ + i];
            const Mesh& mesh = draw.mesh_;
            const VkBuffer buffer = resources_.bufferFor(mesh.buffer_);
            const std::array<VkBuffer, Mesh::VertexStreams> buffers{buffer, buffer, buffer, buffer};
            vkCmdBindVertexBuffers(cmd, 0, Mesh::VertexStreams, buffers.data(),
                                   mesh.streamOffsets_.data());
            vkCmdBindIndexBuffer(cmd, buffer, mesh.streamOffsets_[Mesh::Index],
                                 toVkIndexType(mesh.indexFormat_));

            ThumbnailPush push{draw.index_, 0};
            if (mesh.subMeshCount == 0)
            {
                vkCmdPushConstants(cmd, layout_, stages, 0, sizeof push, &push);
                vkCmdDrawIndexed(cmd, mesh.indexCount_, 1, 0, 0, 0);
                continue;
            }
            for (uint8_t sub = 0; sub < mesh.subMeshCount; ++sub)
            {
                push.submeshIndex_ = sub;
                vkCmdPushConstants(cmd, layout_, stages, 0, sizeof push, &push);
                const SubMesh& subMesh = mesh.subMeshes[sub];
                vkCmdDrawIndexed(cmd, subMesh.indexCount, 1, subMesh.indexOffset, 0, 0);
            }
        }
    }
}

Mesh Thumbnails::uploadMesh(const std::vector<v3f>& positions, const std::vector<v3f>& normals,
                            const std::vector<v2f>& uvs, const std::vector<v4f>& tangents,
                            const std::vector<uint32_t>& indices, const char* name)
{
    uint64_t cursor = 0;
    const auto place = [&cursor](size_t bytes)
    {
        const uint64_t offset = (cursor + 15) & ~uint64_t(15);
        cursor = offset + bytes;
        return offset;
    };
    const size_t indexBytes = sizeof(uint32_t) * indices.size();
    const size_t positionBytes = sizeof(v3f) * positions.size();
    const size_t normalBytes = sizeof(v3f) * normals.size();
    const size_t uvBytes = sizeof(v2f) * uvs.size();
    const size_t tangentBytes = sizeof(v4f) * tangents.size();

    Mesh mesh;
    mesh.streamOffsets_[Mesh::Index] = place(indexBytes);
    mesh.streamOffsets_[Mesh::Position] = place(positionBytes);
    mesh.streamOffsets_[Mesh::Normal] = place(normalBytes);
    mesh.streamOffsets_[Mesh::UV0] = place(uvBytes);
    mesh.streamOffsets_[Mesh::Tangent] = place(tangentBytes);
    mesh.buffer_ = resources_.createStaticBuffer(cursor, name);

    const auto upload = [&](Mesh::Stream stream, const void* src, size_t bytes)
    {
        std::memcpy(
            resources_.requestUpload(mesh.buffer_, bytes, mesh.streamOffsets_[stream]).data(), src,
            bytes);
    };
    upload(Mesh::Index, indices.data(), indexBytes);
    upload(Mesh::Position, positions.data(), positionBytes);
    upload(Mesh::Normal, normals.data(), normalBytes);
    upload(Mesh::UV0, uvs.data(), uvBytes);
    upload(Mesh::Tangent, tangents.data(), tangentBytes);

    mesh.indexCount_ = static_cast<uint32_t>(indices.size());
    mesh.vertexCount_ = static_cast<uint32_t>(positions.size());
    mesh.indexFormat_ = ResourceFormat::R32_UINT;
    for (const v3f& p : positions)
        mesh.localBounds_.extend(p);
    return mesh;
}

Mesh Thumbnails::buildSphere()
{
    constexpr uint32_t rings = 24;
    constexpr uint32_t segments = 48;
    constexpr float pi = std::numbers::pi_v<float>;
    std::vector<v3f> positions;
    std::vector<v3f> normals;
    std::vector<v2f> uvs;
    std::vector<v4f> tangents;
    std::vector<uint32_t> indices;
    for (uint32_t r = 0; r <= rings; ++r)
    {
        const float theta = pi * static_cast<float>(r) / static_cast<float>(rings);
        for (uint32_t s = 0; s <= segments; ++s)
        {
            const float phi = 2.0f * pi * static_cast<float>(s) / static_cast<float>(segments);
            const v3f p{std::sin(theta) * std::cos(phi), std::cos(theta),
                        std::sin(theta) * std::sin(phi)};
            positions.push_back(p);
            normals.push_back(p);
            uvs.emplace_back(static_cast<float>(s) / static_cast<float>(segments),
                             static_cast<float>(r) / static_cast<float>(rings));
            tangents.emplace_back(-std::sin(phi), 0.0f, std::cos(phi), 1.0f);
        }
    }
    for (uint32_t r = 0; r < rings; ++r)
        for (uint32_t s = 0; s < segments; ++s)
        {
            const uint32_t a = r * (segments + 1) + s;
            const uint32_t b = a + segments + 1;
            indices.insert(indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    return uploadMesh(positions, normals, uvs, tangents, indices, "thumbnail sphere");
}

Mesh Thumbnails::buildQuad()
{
    const std::vector<v3f> positions{
        {-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {1.f, 1.f, 0.f}, {-1.f, 1.f, 0.f}};
    const std::vector<v3f> normals(4, v3f::UnitZ());
    const std::vector<v2f> uvs{{0.f, 1.f}, {1.f, 1.f}, {1.f, 0.f}, {0.f, 0.f}};
    const std::vector<v4f> tangents(4, v4f{1.f, 0.f, 0.f, 1.f});
    const std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3};
    return uploadMesh(positions, normals, uvs, tangents, indices, "thumbnail quad");
}
}  // namespace batap
