#pragma once

#include "EigenTypes.h"

#include <cstddef>
#include <cstring>

namespace batap
{
template <size_t N, class Derived>
void flatten(float (&dst)[N], const Eigen::MatrixBase<Derived>& src)
{
    constexpr int rows = Derived::RowsAtCompileTime;
    constexpr int cols = Derived::ColsAtCompileTime;
    static_assert(size_t(rows) * size_t(cols) <= N, "destination too small for this value");
    const Eigen::Matrix<float, rows, cols> value = src;
    std::memcpy(dst, value.data(), sizeof(float) * size_t(rows) * size_t(cols));
}
}  // namespace batap
