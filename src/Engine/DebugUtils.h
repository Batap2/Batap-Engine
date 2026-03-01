#pragma once

#include <string_view>

namespace batap
{

void ThrowIfFailed(long hr);

void ThrowAssert(bool condition, std::string_view msg);

[[noreturn]] void ThrowRuntime(const char* msg);
}  // namespace batap
