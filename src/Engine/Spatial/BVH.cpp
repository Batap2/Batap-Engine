#include "Spatial/BVH.h"

#include <numeric>

namespace batap
{
namespace
{
constexpr uint32_t MaxLeafPrims = 4;
constexpr int BinCount = 12;

struct Bin
{
    AABB bounds_;
    uint32_t count_ = 0;
};

struct Task
{
    uint32_t node_;
    uint32_t first_;
    uint32_t count_;
    uint32_t depth_;
};
}  // namespace

void BVH::clear()
{
    nodes_.clear();
    order_.clear();
}

void BVH::build(std::span<const AABB> prims)
{
    clear();
    if (prims.empty())
        return;

    order_.resize(prims.size());
    std::iota(order_.begin(), order_.end(), 0u);

    std::vector<v3f> centroids(prims.size());
    for (size_t i = 0; i < prims.size(); ++i)
        centroids[i] = prims[i].center();

    nodes_.reserve(prims.size() * 2);
    nodes_.emplace_back();

    std::vector<Task> stack;
    stack.push_back({0, 0, static_cast<uint32_t>(prims.size()), 0});

    while (!stack.empty())
    {
        const Task task = stack.back();
        stack.pop_back();

        AABB bounds;
        AABB centroidBounds;
        for (uint32_t i = 0; i < task.count_; ++i)
        {
            const uint32_t p = order_[task.first_ + i];
            bounds.extend(prims[p]);
            centroidBounds.extend(centroids[p]);
        }
        nodes_[task.node_].bounds_ = bounds;

        const auto makeLeaf = [&]
        {
            nodes_[task.node_].firstPrim_ = task.first_;
            nodes_[task.node_].primCount_ = task.count_;
        };

        if (task.count_ <= MaxLeafPrims || task.depth_ >= BVH::MaxDepth)
        {
            makeLeaf();
            continue;
        }

        const v3f extent = centroidBounds.size();
        int axis = 0;
        if (extent.y() > extent[axis])
            axis = 1;
        if (extent.z() > extent[axis])
            axis = 2;

        if (extent[axis] <= 0.f)
        {
            makeLeaf();
            continue;
        }

        std::array<Bin, BinCount> bins{};
        const float scale = static_cast<float>(BinCount) / extent[axis];
        const float axisMin = centroidBounds.min_[axis];

        const auto binOf = [&](uint32_t prim)
        {
            const int b = static_cast<int>((centroids[prim][axis] - axisMin) * scale);
            return static_cast<size_t>(std::clamp(b, 0, BinCount - 1));
        };

        for (uint32_t i = 0; i < task.count_; ++i)
        {
            const uint32_t p = order_[task.first_ + i];
            Bin& bin = bins[binOf(p)];
            bin.bounds_.extend(prims[p]);
            bin.count_++;
        }

        std::array<float, BinCount - 1> leftArea{};
        std::array<uint32_t, BinCount - 1> leftCount{};
        {
            AABB acc;
            uint32_t n = 0;
            for (int i = 0; i < BinCount - 1; ++i)
            {
                acc.extend(bins[static_cast<size_t>(i)].bounds_);
                n += bins[static_cast<size_t>(i)].count_;
                leftArea[static_cast<size_t>(i)] = acc.surfaceArea();
                leftCount[static_cast<size_t>(i)] = n;
            }
        }

        float bestCost = static_cast<float>(task.count_) * bounds.surfaceArea();
        int bestSplit = -1;
        {
            AABB acc;
            uint32_t n = 0;
            for (int i = BinCount - 1; i > 0; --i)
            {
                acc.extend(bins[static_cast<size_t>(i)].bounds_);
                n += bins[static_cast<size_t>(i)].count_;
                const size_t li = static_cast<size_t>(i - 1);
                if (leftCount[li] == 0 || n == 0)
                    continue;
                const float cost = leftArea[li] * static_cast<float>(leftCount[li]) +
                                   acc.surfaceArea() * static_cast<float>(n);
                if (cost < bestCost)
                {
                    bestCost = cost;
                    bestSplit = i;
                }
            }
        }

        if (bestSplit < 0)
        {
            makeLeaf();
            continue;
        }

        const auto begin = order_.begin() + task.first_;
        const auto mid = std::partition(begin, begin + task.count_, [&](uint32_t p)
                                        { return binOf(p) < static_cast<size_t>(bestSplit); });
        const auto leftCountFinal = static_cast<uint32_t>(std::distance(begin, mid));

        if (leftCountFinal == 0 || leftCountFinal == task.count_)
        {
            makeLeaf();
            continue;
        }

        const uint32_t left = static_cast<uint32_t>(nodes_.size());
        nodes_.emplace_back();
        nodes_.emplace_back();
        nodes_[task.node_].leftChild_ = left;
        nodes_[task.node_].rightChild_ = left + 1;

        stack.push_back({left, task.first_, leftCountFinal, task.depth_ + 1});
        stack.push_back({left + 1, task.first_ + leftCountFinal, task.count_ - leftCountFinal,
                         task.depth_ + 1});
    }
}

}  // namespace batap
