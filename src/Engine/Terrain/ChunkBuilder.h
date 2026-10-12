#pragma once

#include "EigenTypes.h"
#include "Renderer/TerrainDraws.h"
#include "Terrain/TerrainChunk.h"

#include <entt/entt.hpp>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace batap
{

struct TerrainRelief;

// Builds chunks on worker threads, in the order asked.
struct ChunkBuilder
{
    struct Request
    {
        entt::entity owner_ = entt::null;
        uint64_t fingerprint_ = 0;
        std::shared_ptr<const TerrainRelief> relief_;
        TerrainGrid grid_;
        ChunkKey key_;
    };

    struct Result
    {
        entt::entity owner_ = entt::null;
        uint64_t fingerprint_ = 0;
        ChunkKey key_;
        v3d center_ = v3d::Zero();
        TerrainDraws::Mesh mesh_;
        double ms_ = 0.0;
    };

    explicit ChunkBuilder(int32_t threads);
    ~ChunkBuilder();

    ChunkBuilder(const ChunkBuilder&) = delete;
    ChunkBuilder& operator=(const ChunkBuilder&) = delete;

    // Replaces the requests not started yet. One under way or done and not
    // taken is not built twice.
    void request(std::vector<Request> requests);
    // In the order they finished, until `bytes` of vertices and indices: at
    // least one.
    std::vector<Result> take(uint64_t bytes);
    // Drops the requests not started and waits for the others: a relief is
    // game code, which a hot reload unloads.
    void cancel();

    size_t queued() const;
    size_t underWay() const;

   private:
    struct Job
    {
        entt::entity owner_ = entt::null;
        uint64_t fingerprint_ = 0;
        ChunkKey key_;

        bool operator==(const Job& o) const
        {
            return owner_ == o.owner_ && fingerprint_ == o.fingerprint_ && key_ == o.key_;
        }
    };

    void work();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::vector<Request> queue_;
    size_t next_ = 0;
    std::vector<Job> underWay_;
    std::vector<Result> done_;
    bool stop_ = false;
    std::vector<std::thread> threads_;
};

}  // namespace batap
