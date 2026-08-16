#pragma once
#include <atomic>
#include <mutex>
#include <queue>
#include <variant>

namespace batap
{

template <typename... Msgs>
struct TSMsgBus
{
   public:
    using Message = std::variant<Msgs...>;

    void post(Message msg)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            q_.push(std::move(msg));
            has_.store(true, std::memory_order_relaxed);
        }
    }

    template <typename Fn>
    void pump(Fn&& handle)
    {
        if (!has_.load(std::memory_order_relaxed))
            return;

        std::queue<Message> local;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            std::swap(local, q_);
            if (q_.empty())
                has_.store(false, std::memory_order_release);
        }

        while (!local.empty())
        {
            handle(std::move(local.front()));
            local.pop();
        }
    }

    template <typename T, typename Fn>
    void pumpType(Fn&& handle)
    {
        pump(
            [&](Message&& v)
            {
                if (auto* p = std::get_if<T>(&v))
                    handle(std::move(*p));
            });
    }

   private:
    std::mutex mtx_;
    std::queue<Message> q_;
    std::atomic<bool> has_{false};
};

}  // namespace batap
