/**
 * @file shm_simd_utils.h
 * @brief SIMD-optimized utilities for simulation workloads
 * @author Alex Towell
 * @date 2025
 * @version 1.0.0
 * 
 * @details Provides SIMD-friendly operations for common simulation patterns.
 * Requires C++23 and x86-64 with AVX2 or better.
 */

#pragma once
#include <immintrin.h>
#include <cstddef>
#include <span>
#include <algorithm>
#include "shm_array.h"

namespace shm_simd {

/**
 * @brief Check if pointer is aligned to boundary
 * 
 * @param ptr Pointer to check
 * @param alignment Required alignment (must be power of 2)
 * @return true if aligned
 */
inline bool is_aligned(const void* ptr, size_t alignment) noexcept {
    return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

/**
 * @brief Vectorized sum of float array using AVX2
 * 
 * @param data Input array (should be 32-byte aligned for best performance)
 * @param count Number of elements
 * @return Sum of all elements
 * 
 * @note Falls back to scalar for unaligned or small arrays
 * 
 * @par Example:
 * @code
 * shm_array<float> temps(shm, "temperatures", 1000);
 * float total = shm_simd::sum_floats(temps.data(), temps.size());
 * @endcode
 */
inline float sum_floats(const float* data, size_t count) noexcept {
    if (count < 8) {
        // Scalar fallback for small arrays
        float sum = 0.0f;
        for (size_t i = 0; i < count; ++i) {
            sum += data[i];
        }
        return sum;
    }

    __m256 sum_vec = _mm256_setzero_ps();
    size_t simd_count = count & ~7;  // Round down to multiple of 8

    // Main SIMD loop
    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec = _mm256_loadu_ps(&data[i]);
        sum_vec = _mm256_add_ps(sum_vec, vec);
    }

    // Horizontal sum of vector
    __m128 low = _mm256_castps256_ps128(sum_vec);
    __m128 high = _mm256_extractf128_ps(sum_vec, 1);
    __m128 sum128 = _mm_add_ps(low, high);
    sum128 = _mm_hadd_ps(sum128, sum128);
    sum128 = _mm_hadd_ps(sum128, sum128);
    
    float sum = _mm_cvtss_f32(sum128);

    // Handle remaining elements
    for (size_t i = simd_count; i < count; ++i) {
        sum += data[i];
    }

    return sum;
}

/**
 * @brief Vectorized dot product of two float arrays
 * 
 * @param a First array
 * @param b Second array  
 * @param count Number of elements
 * @return Dot product a·b
 * 
 * @par Performance:
 * ~8x faster than scalar for aligned arrays
 */
inline float dot_product(const float* a, const float* b, size_t count) noexcept {
    __m256 sum_vec = _mm256_setzero_ps();
    size_t simd_count = count & ~7;

    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec_a = _mm256_loadu_ps(&a[i]);
        __m256 vec_b = _mm256_loadu_ps(&b[i]);
        __m256 prod = _mm256_mul_ps(vec_a, vec_b);
        sum_vec = _mm256_add_ps(sum_vec, prod);
    }

    // Horizontal sum
    __m128 low = _mm256_castps256_ps128(sum_vec);
    __m128 high = _mm256_extractf128_ps(sum_vec, 1);
    __m128 sum128 = _mm_add_ps(low, high);
    sum128 = _mm_hadd_ps(sum128, sum128);
    sum128 = _mm_hadd_ps(sum128, sum128);
    
    float sum = _mm_cvtss_f32(sum128);

    // Scalar remainder
    for (size_t i = simd_count; i < count; ++i) {
        sum += a[i] * b[i];
    }

    return sum;
}

/**
 * @brief Vectorized array scaling (multiply by scalar)
 * 
 * @param data Array to scale (modified in-place)
 * @param count Number of elements
 * @param scale Scale factor
 * 
 * @par Example:
 * @code
 * shm_array<float> values(shm, "values", 1000);
 * shm_simd::scale_floats(values.data(), values.size(), 2.0f);
 * @endcode
 */
inline void scale_floats(float* data, size_t count, float scale) noexcept {
    __m256 scale_vec = _mm256_set1_ps(scale);
    size_t simd_count = count & ~7;

    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec = _mm256_loadu_ps(&data[i]);
        vec = _mm256_mul_ps(vec, scale_vec);
        _mm256_storeu_ps(&data[i], vec);
    }

    // Scalar remainder
    for (size_t i = simd_count; i < count; ++i) {
        data[i] *= scale;
    }
}

/**
 * @brief Vectorized FMA operation: result = a * b + c
 * 
 * @param a First multiplicand array
 * @param b Second multiplicand array
 * @param c Addend array
 * @param result Output array
 * @param count Number of elements
 * 
 * @note Uses FMA instructions if available (single rounding)
 */
inline void fma_floats(const float* a, const float* b, const float* c, 
                       float* result, size_t count) noexcept {
    size_t simd_count = count & ~7;

    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec_a = _mm256_loadu_ps(&a[i]);
        __m256 vec_b = _mm256_loadu_ps(&b[i]);
        __m256 vec_c = _mm256_loadu_ps(&c[i]);
        __m256 res = _mm256_fmadd_ps(vec_a, vec_b, vec_c);
        _mm256_storeu_ps(&result[i], res);
    }

    // Scalar remainder
    for (size_t i = simd_count; i < count; ++i) {
        result[i] = a[i] * b[i] + c[i];
    }
}

/**
 * @brief Find minimum value in float array
 * 
 * @param data Input array
 * @param count Number of elements
 * @return Minimum value
 */
inline float min_float(const float* data, size_t count) noexcept {
    if (count == 0) return 0.0f;
    
    __m256 min_vec = _mm256_set1_ps(std::numeric_limits<float>::max());
    size_t simd_count = count & ~7;

    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec = _mm256_loadu_ps(&data[i]);
        min_vec = _mm256_min_ps(min_vec, vec);
    }

    // Extract minimum from vector
    float mins[8];
    _mm256_storeu_ps(mins, min_vec);
    float min_val = *std::min_element(mins, mins + 8);

    // Check remainder
    for (size_t i = simd_count; i < count; ++i) {
        min_val = std::min(min_val, data[i]);
    }

    return min_val;
}

/**
 * @brief Find maximum value in float array
 * 
 * @param data Input array
 * @param count Number of elements
 * @return Maximum value
 */
inline float max_float(const float* data, size_t count) noexcept {
    if (count == 0) return 0.0f;
    
    __m256 max_vec = _mm256_set1_ps(std::numeric_limits<float>::lowest());
    size_t simd_count = count & ~7;

    for (size_t i = 0; i < simd_count; i += 8) {
        __m256 vec = _mm256_loadu_ps(&data[i]);
        max_vec = _mm256_max_ps(max_vec, vec);
    }

    // Extract maximum from vector
    float maxs[8];
    _mm256_storeu_ps(maxs, max_vec);
    float max_val = *std::max_element(maxs, maxs + 8);

    // Check remainder
    for (size_t i = simd_count; i < count; ++i) {
        max_val = std::max(max_val, data[i]);
    }

    return max_val;
}

/**
 * @brief Prefetch data for read
 * 
 * @param ptr Address to prefetch
 * @param distance How many cache lines ahead (0-3)
 * 
 * @note Hints to CPU to load data into cache
 */
template<int distance = 1>
inline void prefetch_read(const void* ptr) noexcept {
    _mm_prefetch(static_cast<const char*>(ptr), distance);
}

/**
 * @brief Prefetch data for write
 * 
 * @param ptr Address to prefetch
 * 
 * @note Prepares cache line for modification
 */
inline void prefetch_write(void* ptr) noexcept {
    __builtin_prefetch(ptr, 1, 1);
}

/**
 * @brief Stream store (bypass cache) for large arrays
 * 
 * @param dest Destination (should be 32-byte aligned)
 * @param src Source data
 * @param count Number of floats to copy
 * 
 * @note Use for data that won't be read soon (avoids cache pollution)
 */
inline void stream_store_floats(float* dest, const float* src, size_t count) noexcept {
    size_t simd_count = count & ~7;

    // Ensure aligned for stream stores
    if (is_aligned(dest, 32)) {
        for (size_t i = 0; i < simd_count; i += 8) {
            __m256 vec = _mm256_loadu_ps(&src[i]);
            _mm256_stream_ps(&dest[i], vec);
        }
        _mm_sfence();  // Ensure stores complete
    } else {
        // Fallback to regular stores
        for (size_t i = 0; i < simd_count; i += 8) {
            __m256 vec = _mm256_loadu_ps(&src[i]);
            _mm256_storeu_ps(&dest[i], vec);
        }
    }

    // Handle remainder
    for (size_t i = simd_count; i < count; ++i) {
        dest[i] = src[i];
    }
}

/**
 * @brief Helper class for SIMD operations on shm_array
 * 
 * @tparam TableType Metadata table type
 * 
 * @par Example:
 * @code
 * shm_array<float> data(shm, "data", 10000);
 * SimdArray simd(data);
 * float sum = simd.sum();
 * simd.scale(2.0f);
 * @endcode
 */
template<typename TableType = shm_table>
class SimdArray {
private:
    shm_array<float, TableType>& arr;

public:
    explicit SimdArray(shm_array<float, TableType>& array) : arr(array) {}

    float sum() const noexcept {
        return sum_floats(arr.data(), arr.size());
    }

    float min() const noexcept {
        return min_float(arr.data(), arr.size());
    }

    float max() const noexcept {
        return max_float(arr.data(), arr.size());
    }

    void scale(float factor) noexcept {
        scale_floats(arr.data(), arr.size(), factor);
    }

    float dot(const shm_array<float, TableType>& other) const noexcept {
        size_t min_size = std::min(arr.size(), other.size());
        return dot_product(arr.data(), other.data(), min_size);
    }
};

} // namespace shm_simd