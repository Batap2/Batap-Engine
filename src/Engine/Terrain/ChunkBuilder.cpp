#include "Terrain/ChunkBuilder.h"

#include "Terrain/TerrainRelief.h"

#include <algorithm>
#include <chrono>
#include <iterator>

namespace batap
{
namespace
{

uint64_t bytesOf(const TerrainDraws::Mesh& mesh)
{
    return mesh.vertices_.size() * sizeof(TerrainVertexGPUData) +
           mesh.indices_.size() * sizeof(uint16_t);
}

}  // namespace

ChunkBuilder::ChunkBuilder(int32_t threads)
{
    for (int32_t i = 0; i < std::max(threads, 1); ++i)
        threads_.emplace_back([this] { work(); });
}

ChunkBuilder::~ChunkBuilder()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    for (std::thread& t : threads_)
        t.join();
}

void ChunkBuilder::request(std::vector<Request> requests)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::erase_if(
            requests,
            [&](const Request& r)
            {
                const Job job{r.owner_, r.fingerprint_, r.key_};
                return std::find(underWay_.begin(), underWay_.end(), job) != underWay_.end() ||
                       std::any_of(done_.begin(), done_.end(), [&](const Result& d)
                                   { return Job{d.owner_, d.fingerprint_, d.key_} == job; });
            });
        queue_ = std::move(requests);
        next_ = 0;
    }
    wake_.notify_all();
}

std::vector<ChunkBuilder::Result> ChunkBuilder::take(uint64_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    uint64_t taken = 0;
    for (; n < done_.size(); ++n)
    {
        const uint64_t size = bytesOf(done_[n].mesh_);
        if (n > 0 && taken + size > bytes)
            break;
        taken += size;
    }
    const auto end = done_.begin() + static_cast<std::ptrdiff_t>(n);
    std::vector<Result> out(std::make_move_iterator(done_.begin()), std::make_move_iterator(end));
    done_.erase(done_.begin(), end);
    return out;
}

void ChunkBuilder::cancel()
{
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.clear();
    next_ = 0;
    idle_.wait(lock, [&] { return underWay_.empty(); });
    done_.clear();
}

size_t ChunkBuilder::queued() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() - next_;
}

size_t ChunkBuilder::underWay() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return underWay_.size();
}

void ChunkBuilder::work()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        wake_.wait(lock, [&] { return stop_ || next_ < queue_.size(); });
        if (stop_)
            return;
        Request request = std::move(queue_[next_++]);
        const Job job{request.owner_, request.fingerprint_, request.key_};
        underWay_.push_back(job);
        lock.unlock();

        const auto start = std::chrono::steady_clock::now();
        ChunkMesh mesh = buildChunk(*request.relief_, request.grid_, request.key_);
        Result result;
        result.owner_ = job.owner_;
        result.fingerprint_ = job.fingerprint_;
        result.key_ = job.key_;
        result.center_ = mesh.center_;
        result.mesh_.vertices_ = packVertices(mesh);
        result.mesh_.indices_ = std::move(mesh.indices_);
        result.mesh_.ranges_ = mesh.ranges_;
        result.mesh_.cells_ = static_cast<uint32_t>(request.grid_.cells_);
        result.mesh_.level_ = static_cast<uint32_t>(request.key_.level_);
        result.ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        // Possibly the relief's last owner: it must go before cancel() returns.
        request.relief_.reset();

        lock.lock();
        done_.push_back(std::move(result));
        std::erase(underWay_, job);
        idle_.notify_all();
    }
}

}  // namespace batap
