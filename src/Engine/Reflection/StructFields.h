#pragma once

// Minimal aggregate reflection — field count, field access, field names.
// Hand-written equivalent of the tiny subset of Boost.PFR we need, so the
// engine carries no extra dependency. Same __PRETTY_FUNCTION__ mechanism
// magic_enum already relies on.
//
// Requirements on T:
//   - aggregate: no user ctor, no private/protected fields, no base class
//     (members with ctors like Eigen types are fine)
//   - at most kMaxFields fields
//   - no C-array fields (wrap them in std::array)
//
// Compiler support: clang / clang-cl. Adding MSVC later only means a second
// parsing branch in prettyFieldName().

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace batap::refl
{

inline constexpr std::size_t kMaxFields = 16;

// --- field count ---------------------------------------------------------

namespace detail
{
// Converts to anything — used to probe aggregate initialization.
struct AnyType
{
    template <class U>
    operator U() const;
};

template <class T, std::size_t N>
constexpr bool initializableWithN = []<std::size_t... I>(std::index_sequence<I...>)
{ return requires { T{((void) I, AnyType{})...}; }; }(std::make_index_sequence<N>{});
}  // namespace detail

// Aggregate init accepts *fewer* initializers than fields (rest are
// defaulted), so the field count is the largest N that still compiles.
template <class T, std::size_t N = kMaxFields>
constexpr std::size_t fieldCount()
{
    static_assert(std::is_aggregate_v<T>,
                  "batap::refl requires an aggregate (no user ctor, no private fields, no base). "
                  "Register this component by hand instead.");
    if constexpr (detail::initializableWithN<T, N>)
        return N;
    else
    {
        static_assert(N > 0, "could not detect field count");
        return fieldCount<T, N - 1>();
    }
}

// --- field access ----------------------------------------------------------

// References to all fields of x, as a tuple. One structured-binding case per
// arity — mechanical, extend if a component ever exceeds kMaxFields.
template <class T>
constexpr auto tieFields(T& x)
{
    constexpr std::size_t n = fieldCount<T>();
    static_assert(n <= kMaxFields, "component has too many fields, raise kMaxFields");

    /* clang-format off */
    if constexpr (n == 0)  { return std::tie(); }
    else if constexpr (n == 1)  { auto& [a] = x; return std::tie(a); }
    else if constexpr (n == 2)  { auto& [a,b] = x; return std::tie(a,b); }
    else if constexpr (n == 3)  { auto& [a,b,c] = x; return std::tie(a,b,c); }
    else if constexpr (n == 4)  { auto& [a,b,c,d] = x; return std::tie(a,b,c,d); }
    else if constexpr (n == 5)  { auto& [a,b,c,d,e] = x; return std::tie(a,b,c,d,e); }
    else if constexpr (n == 6)  { auto& [a,b,c,d,e,f] = x; return std::tie(a,b,c,d,e,f); }
    else if constexpr (n == 7)  { auto& [a,b,c,d,e,f,g] = x; return std::tie(a,b,c,d,e,f,g); }
    else if constexpr (n == 8)  { auto& [a,b,c,d,e,f,g,h] = x; return std::tie(a,b,c,d,e,f,g,h); }
    else if constexpr (n == 9)  { auto& [a,b,c,d,e,f,g,h,i] = x; return std::tie(a,b,c,d,e,f,g,h,i); }
    else if constexpr (n == 10) { auto& [a,b,c,d,e,f,g,h,i,j] = x; return std::tie(a,b,c,d,e,f,g,h,i,j); }
    else if constexpr (n == 11) { auto& [a,b,c,d,e,f,g,h,i,j,k] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k); }
    else if constexpr (n == 12) { auto& [a,b,c,d,e,f,g,h,i,j,k,l] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l); }
    else if constexpr (n == 13) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m); }
    else if constexpr (n == 14) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,o] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,o); }
    else if constexpr (n == 15) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,o,p] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,o,p); }
    else if constexpr (n == 16) { auto& [a,b,c,d,e,f,g,h,i,j,k,l,m,o,p,q] = x; return std::tie(a,b,c,d,e,f,g,h,i,j,k,l,m,o,p,q); }
    /* clang-format on */
}

template <std::size_t I, class T>
using FieldTypeAt =
    std::remove_reference_t<std::tuple_element_t<I, decltype(tieFields(std::declval<T&>()))>>;

// --- field names -------------------------------------------------------------

namespace detail
{
// A static instance whose subobject addresses become template arguments.
// Never read at compile time — only addresses are taken, which is a valid
// constant expression even though initialization is dynamic (Eigen members).
template <class T>
struct Probe
{
    static T value;
};
// The whole point of Probe is a static instance — its global ctor/dtor are wanted.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#pragma clang diagnostic ignored "-Wexit-time-destructors"
template <class T>
T Probe<T>::value{};
#pragma clang diagnostic pop

template <class T, std::size_t I>
inline constexpr auto fieldPtr = &std::get<I>(tieFields(Probe<T>::value));

// clang prints the full path of Ptr in the signature, e.g.
// "... [T = ..., Ptr = &batap::refl::detail::Probe<batap::PointLight_C>::value.color_]"
template <auto Ptr>
consteval std::string_view prettySignature()
{
    return __PRETTY_FUNCTION__;
}

consteval std::string_view parseFieldName(std::string_view sig)
{
    const auto end = sig.find_last_of("]}");
    if (end != std::string_view::npos)
        sig = sig.substr(0, end);
    const auto dot = sig.find_last_of('.');
    if (dot == std::string_view::npos)
        return {};  // unexpected format — caught by the static_assert below
    return sig.substr(dot + 1);
}
}  // namespace detail

template <class T, std::size_t I>
consteval std::string_view fieldName()
{
    constexpr auto name = detail::parseFieldName(detail::prettySignature<detail::fieldPtr<T, I>>());
    static_assert(!name.empty(),
                  "field name extraction failed — unsupported compiler output format");
    // Members are spelled with a trailing underscore (color_); json keys and
    // UI labels carry none.
    return name.back() == '_' ? name.substr(0, name.size() - 1) : name;
}

}  // namespace batap::refl
