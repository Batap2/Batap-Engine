#include "Physics/MeshCollider.h"

#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <iostream>

namespace batap
{

namespace
{

JPH::VertexList vertexListOf(const std::vector<v3f>& positions)
{
    JPH::VertexList vertices;
    vertices.reserve(positions.size());
    for (const v3f& v : positions)
        vertices.push_back(JPH::Float3(v.x(), v.y(), v.z()));
    return vertices;
}

JPH::ShapeRefC meshShapeOf(JPH::VertexList vertices, JPH::IndexedTriangleList triangles)
{
    JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
    const JPH::Shape::ShapeResult result = settings.Create();
    if (result.HasError())
    {
        std::cerr << "[MeshCollider] " << result.GetError() << "\n";
        return nullptr;
    }
    return result.Get();
}

}  // namespace

JPH::ShapeRefC makeMeshShape(const BmeshData& data)
{
    if (data.vertices.empty() || data.indices.size() < 3)
        return nullptr;

    JPH::IndexedTriangleList triangles;
    triangles.reserve(data.indices.size() / 3);
    for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
        triangles.emplace_back(data.indices[i], data.indices[i + 1], data.indices[i + 2], 0u);

    return meshShapeOf(vertexListOf(data.vertices), std::move(triangles));
}

JPH::ShapeRefC makeGridMeshShape(const std::vector<v3f>& positions, int32_t cells)
{
    const auto at = [&](int32_t i, int32_t j)
    { return static_cast<uint32_t>(j * (cells + 1) + i); };
    JPH::IndexedTriangleList triangles;
    triangles.reserve(static_cast<size_t>(cells * cells * 2));
    // buildChunk's diagonals, from (i, j) to (i + 1, j + 1).
    for (int32_t j = 0; j < cells; ++j)
        for (int32_t i = 0; i < cells; ++i)
        {
            triangles.emplace_back(at(i, j), at(i + 1, j), at(i + 1, j + 1), 0u);
            triangles.emplace_back(at(i, j), at(i + 1, j + 1), at(i, j + 1), 0u);
        }
    return meshShapeOf(vertexListOf(positions), std::move(triangles));
}

}  // namespace batap
