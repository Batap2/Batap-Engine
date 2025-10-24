#pragma once

#include <winerror.h>
#include <source_location>
#include <stdexcept>
#include <string>

inline void ThrowIfFailed(HRESULT hr)
{
#ifndef NDEBUG
    if (FAILED(hr))
    {
        throw std::exception();
    }
#else
    (void) hr;
#endif
}

inline constexpr void ThrowAssert(bool condition, std::string_view msg)
{
#ifndef NDEBUG
    if (!condition)
    {
        auto loc = std::source_location::current();
        throw std::runtime_error(std::string("Assertion failed at ") + loc.file_name() + ":" +
                                 std::to_string(loc.line()) + " — " + std::string(msg));
    }
#else
    (void) condition;
    (void) msg;
#endif
}