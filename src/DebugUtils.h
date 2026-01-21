#pragma once

#include <string_view>

namespace rayvox
{

void ThrowIfFailed(long hr);

void ThrowAssert(bool condition, std::string_view msg);

[[noreturn]] void ThrowRuntime(const char* msg);
}  // namespace rayvox
