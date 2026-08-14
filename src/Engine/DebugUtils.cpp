#include "DebugUtils.h"

#if defined(_WIN32)
  #include <windows.h>
  #include <winerror.h>
#endif
#include <source_location>
#include <stdexcept>
#include <string>

namespace batap
{

void ThrowIfFailed(long hr)
{
#ifndef NDEBUG
    if (hr < 0)  // FAILED(hr), sans dépendre de winerror.h
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
#if defined(_WIN32)
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#endif
    fprintf(stderr, "[FATAL] %s\n", msg);
    fflush(stderr);
#if defined(_DEBUG) && defined(_WIN32)
    __debugbreak();  // break AVANT l'unwind
#endif
    throw std::runtime_error(msg);
}
}  // namespace batap
