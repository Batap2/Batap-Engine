#pragma once

#include <winerror.h>
#include <source_location>
#include <stdexcept>
#include <string>


inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::exception();
    }
}

inline void ThrowAssert(bool condition, std::string_view msg)
{
    if (!condition)
    {
        auto loc = std::source_location::current();
        throw std::runtime_error(
            std::string("Assertion failed at ")
            + loc.file_name() + ":" + std::to_string(loc.line()) +
            " — " + msg.data()
        );
    }
}