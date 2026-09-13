#pragma once

#include "Bbox.h"
#include "Spatial/Ray.h"

#include <array>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace batap
{

struct BVH
{
    // The traversal stack is bounded by the tree depth, so the build caps it
    // and the stack below can never overflow.
    static constexpr uint32_t MaxDepth = 48;

    struct Node
    {
        AABB bounds_;
        uint32_t firstPrim_ = 0;
        uint32_t primCount_ = 0;
        uint32_t leftChild_ = 0;
        uint32_t rightChild_ = 0;

        bool leaf() const { return primCount_ > 0; }
    };

    std::vector<Node> nodes_;
    std::vector<uint32_t> order_;

    void build(std::span<const AABB> prims);
    void clear();
    bool empty() const { return nodes_.empty(); }

    template <std::invocable<uint32_t, float> Visit>
        requires std::same_as<std::invoke_result_t<Visit, uint32_t, float>, float>
    void raycast(const Ray& ray, Visit&& visit) const
    {
        if (nodes_.empty())
            return;

        const v3f invDir = ray.dir_.cwiseInverse();
        float tMax = ray.tMax_;

        std::array<uint32_t, MaxDepth + 2> stack{};
        size_t sp = 0;
        stack[sp++] = 0;

        while (sp > 0)
        {
            const Node& node = nodes_[stack[--sp]];
            const AABBHit boxHit = rayAABB(ray.origin_, invDir, ray.tMin_, tMax, node.bounds_);
            if (!boxHit.hit_)
                continue;

            if (node.leaf())
            {
                for (uint32_t i = 0; i < node.primCount_; ++i)
                    tMax = visit(order_[node.firstPrim_ + i], tMax);
                continue;
            }

            assert(sp + 2 <= stack.size());

            const AABBHit left =
                rayAABB(ray.origin_, invDir, ray.tMin_, tMax, nodes_[node.leftChild_].bounds_);
            const AABBHit right =
                rayAABB(ray.origin_, invDir, ray.tMin_, tMax, nodes_[node.rightChild_].bounds_);

            // Farther child pushed first: the near one is popped and can shrink
            // tMax before the far one is ever tested.
            if (left.hit_ && right.hit_ && right.tNear_ < left.tNear_)
            {
                stack[sp++] = node.leftChild_;
                stack[sp++] = node.rightChild_;
            }
            else
            {
                if (right.hit_)
                    stack[sp++] = node.rightChild_;
                if (left.hit_)
                    stack[sp++] = node.leftChild_;
            }
        }
    }

    template <std::invocable<uint32_t> Visit>
    void overlap(const AABB& box, Visit&& visit) const
    {
        if (nodes_.empty())
            return;

        std::array<uint32_t, MaxDepth + 2> stack{};
        size_t sp = 0;
        stack[sp++] = 0;

        while (sp > 0)
        {
            const Node& node = nodes_[stack[--sp]];
            if (!node.bounds_.intersects(box))
                continue;

            if (node.leaf())
            {
                for (uint32_t i = 0; i < node.primCount_; ++i)
                    visit(order_[node.firstPrim_ + i]);
                continue;
            }

            assert(sp + 2 <= stack.size());
            stack[sp++] = node.leftChild_;
            stack[sp++] = node.rightChild_;
        }
    }
};

}  // namespace batap
