#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Renderer/ResourceManager.h"

namespace batap
{

struct GPUArenaKey
{
    uint32_t index = 0;
    uint32_t generation = 0;  // 0 == null

    bool operator==(const GPUArenaKey&) const = default;
    explicit operator bool() const { return generation != 0; }
};

// Stable Keys, Not dense on GPU, T source of truth on CPU
template <typename T, typename TKey = GPUArenaKey>
struct GPUArena
{
    static_assert(std::is_trivially_copyable_v<T>);

    GPUArena() = default;

    static GPUArena create(ResourceManager& rm, size_t initCapacity, std::string name = "GPUArena")
    {
        return GPUArena(rm, initCapacity, std::move(name));
    }

    ~GPUArena()
    {
        if (rm_ && srvHandle_.valid())
            rm_->requestDestroy(srvHandle_, true);
    }

    GPUArena(const GPUArena&) = delete;
    GPUArena& operator=(const GPUArena&) = delete;

    GPUArena(GPUArena&& o) noexcept
        : data_(std::move(o.data_)),
          generations_(std::move(o.generations_)),
          alive_(std::move(o.alive_)),
          free_(std::move(o.free_)),
          rm_(o.rm_),
          name_(std::move(o.name_)),
          resourceHandle_(o.resourceHandle_),
          srvHandle_(o.srvHandle_),
          gpuCapacity_(o.gpuCapacity_)
    {
        o.rm_ = nullptr;
    }

    GPUArena& operator=(GPUArena&& o) noexcept
    {
        if (this != &o)
        {
            if (rm_ && srvHandle_.valid())
                rm_->requestDestroy(srvHandle_, true);
            data_ = std::move(o.data_);
            generations_ = std::move(o.generations_);
            alive_ = std::move(o.alive_);
            free_ = std::move(o.free_);
            rm_ = o.rm_;
            name_ = std::move(o.name_);
            resourceHandle_ = o.resourceHandle_;
            srvHandle_ = o.srvHandle_;
            gpuCapacity_ = o.gpuCapacity_;
            o.rm_ = nullptr;
        }
        return *this;
    }

    TKey insert(T value)
    {
        uint32_t idx;
        if (!free_.empty())
        {
            idx = free_.back();
            free_.pop_back();
            data_[idx] = value;
            alive_[idx] = true;
        }
        else
        {
            idx = static_cast<uint32_t>(data_.size());
            data_.push_back(value);
            generations_.push_back(1);
            alive_.push_back(true);
        }

        if (data_.size() > gpuCapacity_)
        {
            grow(std::max(gpuCapacity_ * 2, data_.size()));
            auto span =
                rm_->requestUploadOwned(resourceHandle_, data_.size() * sizeof(T), sizeof(T));
            memcpy(span.data(), data_.data(), data_.size() * sizeof(T));
        }
        else
        {
            auto span = rm_->requestUploadOwned(resourceHandle_, sizeof(T), sizeof(T),
                                                static_cast<size_t>(idx) * sizeof(T));
            memcpy(span.data(), &data_[idx], sizeof(T));
        }

        return TKey{idx, generations_[idx]};
    }

    bool erase(TKey k)
    {
        if (!isValid(k))
            return false;

        alive_[k.index] = false;
        generations_[k.index]++;
        free_.push_back(k.index);
        return true;
    }

    const T* get(TKey k) const
    {
        if (!isValid(k))
            return nullptr;
        return &data_[k.index];
    }

    bool update(TKey k, T value)
    {
        if (!isValid(k))
            return false;

        data_[k.index] = value;
        auto span = rm_->requestUploadOwned(resourceHandle_, sizeof(T), sizeof(T),
                                            static_cast<size_t>(k.index) * sizeof(T));
        memcpy(span.data(), &data_[k.index], sizeof(T));
        return true;
    }

    bool contains(TKey k) const { return isValid(k); }

    GPUViewHandle srvHandle() const { return srvHandle_; }

    size_t capacity() const { return data_.size(); }

   private:
    GPUArena(ResourceManager& rm, size_t initCapacity, std::string name)
        : rm_(&rm), name_(std::move(name))
    {
        gpuCapacity_ = initCapacity > 0 ? initCapacity : 1;
        data_.reserve(gpuCapacity_);
        createGpuResources(gpuCapacity_);
    }

    bool isValid(TKey k) const
    {
        return k.generation != 0 && k.index < data_.size() && alive_[k.index] &&
               generations_[k.index] == k.generation;
    }

    void grow(size_t newCapacity)
    {
        const GPUViewHandle oldSrv = srvHandle_;
        createGpuResources(newCapacity);
        rm_->requestDestroy(oldSrv, true);
    }

    void createGpuResources(size_t capacity)
    {
        resourceHandle_ = rm_->createBufferStaticResource(
            capacity * sizeof(T), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_HEAP_TYPE_DEFAULT, name_);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = static_cast<UINT>(capacity);
        srvDesc.Buffer.StructureByteStride = static_cast<UINT>(sizeof(T));
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        srvHandle_ =
            rm_->createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(resourceHandle_, srvDesc);

        gpuCapacity_ = capacity;
    }

    std::vector<T> data_;
    std::vector<uint32_t> generations_;
    std::vector<bool> alive_;
    std::vector<uint32_t> free_;

    ResourceManager* rm_ = nullptr;
    std::string name_;

    GPUResourceHandle resourceHandle_;
    GPUViewHandle srvHandle_;
    size_t gpuCapacity_ = 1;
};

}  // namespace batap
