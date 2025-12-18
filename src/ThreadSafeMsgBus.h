#pragma once
#include <atomic>
#include <mutex>
#include <queue>
#include <variant>

namespace rayvox
{

template <typename... Msgs>
struct TSMsgBus
{
   public:
    using Message = std::variant<Msgs...>;

    void post(Message msg)
    {
        {
            std::lock_guard<std::mutex> lock(_mtx);
            _q.push(std::move(msg));
            _has.store(true, std::memory_order_relaxed);
        }
    }

    template <typename Fn>
    void pump(Fn&& handle)
    {
        if (!_has.load(std::memory_order_relaxed))
            return;

        std::queue<Message> local;
        {
            std::lock_guard<std::mutex> lock(_mtx);
            std::swap(local, _q);
            if (_q.empty())
                _has.store(false, std::memory_order_release);
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
    std::mutex _mtx;
    std::queue<Message> _q;
    std::atomic<bool> _has{false};
};

}  // namespace rayvox
