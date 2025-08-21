/**
 * @file shm_set.h
 * @brief Lock-free set implementation for POSIX shared memory
 * @author Alex Towell
 * 
 * @details
 * Provides a thread-safe set (unique elements) that can be shared across processes.
 * Implemented as a thin wrapper around shm_hash_map with dummy values.
 * 
 * @par Thread Safety
 * All operations are thread-safe using atomic operations from the underlying hash map.
 * 
 * @par Example
 * @code
 * posix_shm shm("simulation", 10 * 1024 * 1024);
 * shm_set<uint32_t> active_particles(shm, "active", 10000);
 * 
 * // Insert
 * active_particles.insert(42);
 * 
 * // Check membership
 * if (active_particles.contains(42)) {
 *     process_particle(42);
 * }
 * @endcode
 */

#pragma once

#include "shm_hash_map.h"
#include <functional>

/**
 * @brief Lock-free set for shared memory
 * 
 * @tparam T Element type (must be trivially copyable)
 * @tparam Hash Hash function (default std::hash<T>)
 * @tparam TableType Table implementation for metadata
 * 
 * @details
 * Implements a set as a hash map with dummy values.
 * Provides O(1) average case insert, erase, and contains operations.
 */
template<typename T, 
         typename Hash = std::hash<T>,
         typename TableType = shm_table>
    requires std::is_trivially_copyable_v<T>
class shm_set {
private:
    // Use empty struct as value type to minimize memory usage
    struct Empty {};
    shm_hash_map<T, Empty, Hash, TableType> map;

public:
    using value_type = T;
    using size_type = size_t;

    /**
     * @brief Check if a set exists in shared memory
     */
    template<typename ShmType>
    static bool exists(ShmType& shm, std::string_view name) {
        return shm_hash_map<T, Empty, Hash, TableType>::exists(shm, name);
    }

    /**
     * @brief Create or open a shared memory set
     * 
     * @param shm Shared memory segment
     * @param name Unique identifier
     * @param capacity Maximum number of elements (0 to open existing)
     */
    template<typename ShmType>
    shm_set(ShmType& shm, std::string_view name, size_t capacity = 0)
        : map(shm, name, capacity) {}

    /**
     * @brief Insert an element
     * 
     * @return true if inserted, false if already exists or set is full
     */
    [[nodiscard]] bool insert(const T& value) noexcept {
        return map.insert(value, Empty{});
    }

    /**
     * @brief Remove an element
     * 
     * @return true if removed, false if not found
     */
    [[nodiscard]] bool erase(const T& value) noexcept {
        return map.erase(value);
    }

    /**
     * @brief Check if element exists
     */
    [[nodiscard]] bool contains(const T& value) const noexcept {
        return map.contains(value);
    }

    /**
     * @brief Get number of elements
     */
    [[nodiscard]] size_t size() const noexcept {
        return map.size();
    }

    /**
     * @brief Check if empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return map.empty();
    }

    /**
     * @brief Clear all elements (not thread-safe!)
     */
    void clear() noexcept {
        map.clear();
    }

    /**
     * @brief Apply function to all elements
     * 
     * @warning May see inconsistent state during concurrent modifications
     */
    template<typename Func>
    void for_each(Func&& func) const {
        map.for_each([&func](const T& key, const Empty&) {
            func(key);
        });
    }

    /**
     * @brief Compute union with another set
     * 
     * @return New set containing elements from both sets
     */
    template<typename ShmType>
    shm_set set_union(ShmType& shm, const std::string& result_name, 
                      const shm_set& other) const {
        // Estimate capacity
        size_t cap = this->size() + other.size();
        shm_set result(shm, result_name, cap);
        
        // Add all from this set
        this->for_each([&result](const T& elem) {
            result.insert(elem);
        });
        
        // Add all from other set
        other.for_each([&result](const T& elem) {
            result.insert(elem);
        });
        
        return result;
    }

    /**
     * @brief Compute intersection with another set
     * 
     * @return New set containing common elements
     */
    template<typename ShmType>
    shm_set set_intersection(ShmType& shm, const std::string& result_name,
                             const shm_set& other) const {
        // Use smaller set for iteration
        const shm_set* smaller = this->size() < other.size() ? this : &other;
        const shm_set* larger = this->size() < other.size() ? &other : this;
        
        shm_set result(shm, result_name, smaller->size());
        
        smaller->for_each([&result, larger](const T& elem) {
            if (larger->contains(elem)) {
                result.insert(elem);
            }
        });
        
        return result;
    }

    /**
     * @brief Compute difference with another set
     * 
     * @return New set containing elements in this but not in other
     */
    template<typename ShmType>
    shm_set set_difference(ShmType& shm, const std::string& result_name,
                          const shm_set& other) const {
        shm_set result(shm, result_name, this->size());
        
        this->for_each([&result, &other](const T& elem) {
            if (!other.contains(elem)) {
                result.insert(elem);
            }
        });
        
        return result;
    }

    /**
     * @brief Check if this is a subset of another set
     */
    [[nodiscard]] bool is_subset_of(const shm_set& other) const noexcept {
        if (this->size() > other.size()) {
            return false;
        }
        
        bool is_subset = true;
        this->for_each([&is_subset, &other](const T& elem) {
            if (!other.contains(elem)) {
                is_subset = false;
            }
        });
        
        return is_subset;
    }

    /**
     * @brief Check if this is a superset of another set
     */
    [[nodiscard]] bool is_superset_of(const shm_set& other) const noexcept {
        return other.is_subset_of(*this);
    }

    /**
     * @brief Check if sets are disjoint (no common elements)
     */
    [[nodiscard]] bool is_disjoint(const shm_set& other) const noexcept {
        const shm_set* smaller = this->size() < other.size() ? this : &other;
        const shm_set* larger = this->size() < other.size() ? &other : this;
        
        bool disjoint = true;
        smaller->for_each([&disjoint, larger](const T& elem) {
            if (larger->contains(elem)) {
                disjoint = false;
            }
        });
        
        return disjoint;
    }

    /**
     * @brief Get name of the set
     */
    std::string_view name() const {
        return map.name();
    }
};

// Type aliases for common sets
using shm_set_int = shm_set<int>;
using shm_set_uint32 = shm_set<uint32_t>;
using shm_set_uint64 = shm_set<uint64_t>;