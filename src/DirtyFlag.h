#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rayvox
{
namespace detail
{
template <size_t NBits>
struct ChooseWord
{
    using type = std::conditional_t<
        (NBits <= 8), uint8_t,
        std::conditional_t<(NBits <= 16), uint16_t,
                           std::conditional_t<(NBits <= 32), uint32_t, uint64_t>>>;
};
}  // namespace detail

template <size_t NBits>
struct DirtyFlag
{
    static_assert(NBits > 0, "NBits must be > 0");

    using WordT = typename detail::ChooseWord<NBits>::type;

    static constexpr size_t WordBits = sizeof(WordT) * 8;
    static constexpr size_t WordCount = (NBits + WordBits - 1) / WordBits;

    std::array<WordT, WordCount> words{};

    void mark(size_t bit)
    {
        assert(bit < NBits);

        if constexpr (WordCount == 1)
        {
            words[0] |= (WordT(1) << bit);
        }
        else
        {
            const size_t wi = bit / WordBits;
            const size_t bi = bit % WordBits;
            words[wi] |= (WordT(1) << bi);
        }
    }

    void clear(size_t bit)
    {
        assert(bit < NBits);

        if constexpr (WordCount == 1)
        {
            words[0] &= ~(WordT(1) << bit);
        }
        else
        {
            const size_t wi = bit / WordBits;
            const size_t bi = bit % WordBits;
            words[wi] &= ~(WordT(1) << bi);
        }
    }

    bool isDirty(size_t bit) const
    {
        assert(bit < NBits);

        if constexpr (WordCount == 1)
        {
            return (words[0] & (WordT(1) << bit)) != WordT(0);
        }
        else
        {
            const size_t wi = bit / WordBits;
            const size_t bi = bit % WordBits;
            return (words[wi] & (WordT(1) << bi)) != WordT(0);
        }
    }

    void clearAll()
    {
        if constexpr (WordCount == 1)
        {
            words[0] = WordT(0);
        }
        else
        {
            for (auto& w : words)
                w = WordT(0);
        }
    }

    bool any() const
    {
        if constexpr (WordCount == 1)
        {
            return words[0] != WordT(0);
        }
        else
        {
            for (auto w : words)
                if (w != WordT(0))
                    return true;
            return false;
        }
    }

    bool none() const { return !any(); }

    void markAll(bool value = true)
    {
        if constexpr (WordCount == 1)
        {
            if (value)
            {
                words[0] = ~WordT(0);
                maskUnusedHighBits();
            }
            else
            {
                words[0] = WordT(0);
            }
        }
        else
        {
            if (value)
            {
                for (auto& w : words)
                    w = ~WordT(0);
                maskUnusedHighBits();
            }
            else
            {
                clearAll();
            }
        }
    }

   private:
    void maskUnusedHighBits()
    {
        constexpr size_t usedLast = (NBits % WordBits);
        if constexpr (usedLast != 0)
        {
            const WordT mask = (WordT(1) << usedLast) - WordT(1);

            if constexpr (WordCount == 1)
            {
                words[0] &= mask;
            }
            else
            {
                words[WordCount - 1] &= mask;
            }
        }
    }
};

}  // namespace rayvox
