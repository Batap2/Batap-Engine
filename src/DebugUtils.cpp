#include "DebugUtils.h"

#include <windows.h>
#include <winerror.h>
#include <source_location>
#include <stdexcept>
#include <string>

namespace rayvox
{

void ThrowIfFailed(long hr)
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

void ThrowAssert(bool condition, std::string_view msg)
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

[[noreturn]] void ThrowRuntime(const char* msg)
{
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#ifdef _DEBUG
    __debugbreak();  // break AVANT l'unwind
#endif
    throw std::runtime_error(msg);
}
}  // namespace rayvox
