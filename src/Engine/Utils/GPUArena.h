#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Renderer/ResourceManager.h"

namespace batap
{

// Stable Keys, Not dense on GPU, T cached on CPU for reupload
template <typename T>
struct GPUArena
{
    static_assert(std::is_trivially_copyable_v<T>);

    struct Key
    {
        uint32_t index = 0;
        uint32_t generation = 0;  // 0 == null

        bool operator==(const Key&) const = default;
    };

    GPUArena(ResourceManager& rm, size_t initCapacity, std::string name = "GPUArena")
        : rm_(rm), name_(std::move(name))
    {
        gpuCapacity_ = initCapacity > 0 ? initCapacity : 1;
        data_.reserve(gpuCapacity_);
        createGpuResources(gpuCapacity_);
    }

    ~GPUArena()
    {
        if (srvHandle_.valid())
            rm_.requestDestroy(srvHandle_, true);
    }

    GPUArena(const GPUArena&) = delete;
    GPUArena& operator=(const GPUArena&) = delete;

    Key insert(T value)
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
                rm_.requestUploadOwned(resourceHandle_, data_.size() * sizeof(T), sizeof(T));
            memcpy(span.data(), data_.data(), data_.size() * sizeof(T));
        }
        else
        {
            auto span = rm_.requestUploadOwned(resourceHandle_, sizeof(T), sizeof(T),
                                               static_cast<size_t>(idx) * sizeof(T));
            memcpy(span.data(), &data_[idx], sizeof(T));
        }

        return Key{idx, generations_[idx]};
    }

    bool erase(Key k)
    {
        if (!isValid(k))
            return false;

        alive_[k.index] = false;
        generations_[k.index]++;
        free_.push_back(k.index);
        return true;
    }

    const T* get(Key k) const
    {
        if (!isValid(k))
            return nullptr;
        return &data_[k.index];
    }

    bool update(Key k, T value)
    {
        if (!isValid(k))
            return false;

        data_[k.index] = value;
        auto span = rm_.requestUploadOwned(resourceHandle_, sizeof(T), sizeof(T),
                                           static_cast<size_t>(k.index) * sizeof(T));
        memcpy(span.data(), &data_[k.index], sizeof(T));
        return true;
    }

    bool contains(Key k) const { return isValid(k); }

    GPUViewHandle srvHandle() const { return srvHandle_; }

    size_t capacity() const { return data_.size(); }

   private:
    bool isValid(Key k) const
    {
        return k.generation != 0 && k.index < data_.size() && alive_[k.index] &&
               generations_[k.index] == k.generation;
    }

    void grow(size_t newCapacity)
    {
        const GPUViewHandle oldSrv = srvHandle_;
        createGpuResources(newCapacity);
        rm_.requestDestroy(oldSrv, true);
    }

    void createGpuResources(size_t capacity)
    {
        resourceHandle_ = rm_.createBufferStaticResource(
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
            rm_.createStaticView<D3D12_SHADER_RESOURCE_VIEW_DESC>(resourceHandle_, srvDesc);

        gpuCapacity_ = capacity;
    }

    std::vector<T> data_;
    std::vector<uint32_t> generations_;
    std::vector<bool> alive_;
    std::vector<uint32_t> free_;

    ResourceManager& rm_;
    std::string name_;

    GPUResourceHandle resourceHandle_;
    GPUViewHandle srvHandle_;
    size_t gpuCapacity_ = 1;
};

}  // namespace batap
