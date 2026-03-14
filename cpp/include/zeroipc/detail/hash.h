#pragma once

#include <cstring>
#include <functional>
#include <string_view>
#include <type_traits>

namespace zeroipc::detail {

/// Hash function for trivially copyable types in shared memory.
/// Uses multiplicative hash for integers, byte-level hash otherwise.
template<typename T>
size_t trivial_hash(const T& val) {
    if constexpr (std::is_integral_v<T>) {
        return static_cast<size_t>(val) * 2654435761U;
    } else {
        std::hash<std::string_view> hasher;
        std::string_view sv(reinterpret_cast<const char*>(&val), sizeof(T));
        return hasher(sv);
    }
}

/// Equality comparison for trivially copyable types in shared memory.
/// Uses == for arithmetic types, memcmp for structs.
template<typename T>
bool trivial_equal(const T& a, const T& b) {
    if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
        return a == b;
    } else {
        return std::memcmp(&a, &b, sizeof(T)) == 0;
    }
}

} // namespace zeroipc::detail
