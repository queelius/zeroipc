#pragma once
#include "zeroipc.h"
#include "table.h"
#include <atomic>
#include <concepts>
#include <string_view>

namespace zeroipc {


/**
 * @brief Shared memory atomic value with auto-discovery
 * 
 * Provides a named atomic value in shared memory that can be discovered
 * by other processes. Fully compatible with std::atomic interface.
 * 
 * @tparam T Type of the atomic value (must be trivially copyable and lock-free)
 * @tparam TableType Type of metadata table (default: table)
 */
template<typename T, typename TableType = table>
    requires std::is_trivially_copyable_v<T> && std::atomic<T>::is_always_lock_free
class atomic_value {
private:
    std::atomic<T>* atom_ptr{nullptr};
    const typename TableType::entry* table_entry{nullptr};

    std::atomic<T>* get_atomic_ptr(void* base_addr, size_t offset) {
        return reinterpret_cast<std::atomic<T>*>(
            static_cast<char*>(base_addr) + offset
        );
    }

public:
    // STL-compliant type aliases
    using value_type = T;

    /**
     * @brief Create or open a shared atomic value
     */
    template<typename ShmType>
    atomic_value(ShmType& shm, std::string_view name, T initial_value = T{}) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match atomic table type");

        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing atomic
            if (entry->elem_size != sizeof(std::atomic<T>)) {
                throw std::runtime_error("Type size mismatch for atomic");
            }
            atom_ptr = get_atomic_ptr(shm.get_base_addr(), entry->offset);
            table_entry = entry;
        } else {
            // Create new atomic
            size_t required_size = sizeof(std::atomic<T>);
            size_t table_size = sizeof(TableType);
            size_t current_used = table->get_total_allocated_size();
            
            size_t offset = table_size + current_used;
            atom_ptr = get_atomic_ptr(shm.get_base_addr(), offset);
            
            // Initialize atomic
            new (atom_ptr) std::atomic<T>(initial_value);
            
            // Register in table
            if (!table->add(name_buf, offset, required_size, sizeof(std::atomic<T>), 1)) {
                throw std::runtime_error("Failed to add atomic to table");
            }
            table_entry = table->find(name_buf);
        }
    }

    // std::atomic compatible interface
    [[nodiscard]] bool is_lock_free() const noexcept {
        return atom_ptr->is_lock_free();
    }

    static constexpr bool is_always_lock_free = std::atomic<T>::is_always_lock_free;

    void store(T value, std::memory_order order = std::memory_order_seq_cst) noexcept {
        atom_ptr->store(value, order);
    }

    [[nodiscard]] T load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return atom_ptr->load(order);
    }

    [[nodiscard]] T exchange(T value, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->exchange(value, order);
    }

    [[nodiscard]] bool compare_exchange_weak(T& expected, T desired,
                                            std::memory_order success = std::memory_order_seq_cst,
                                            std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return atom_ptr->compare_exchange_weak(expected, desired, success, failure);
    }

    [[nodiscard]] bool compare_exchange_strong(T& expected, T desired,
                                              std::memory_order success = std::memory_order_seq_cst,
                                              std::memory_order failure = std::memory_order_seq_cst) noexcept {
        return atom_ptr->compare_exchange_strong(expected, desired, success, failure);
    }

    // Arithmetic operations (enabled for integral and floating-point types)
    template<typename U = T>
        requires std::is_integral_v<U> || std::is_floating_point_v<U>
    T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->fetch_add(arg, order);
    }

    template<typename U = T>
        requires std::is_integral_v<U> || std::is_floating_point_v<U>
    T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->fetch_sub(arg, order);
    }

    // Bitwise operations (enabled for integral types)
    template<typename U = T>
        requires std::is_integral_v<U>
    T fetch_and(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->fetch_and(arg, order);
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T fetch_or(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->fetch_or(arg, order);
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T fetch_xor(T arg, std::memory_order order = std::memory_order_seq_cst) noexcept {
        return atom_ptr->fetch_xor(arg, order);
    }

    // Operators
    T operator=(T value) noexcept {
        store(value);
        return value;
    }

    operator T() const noexcept {
        return load();
    }

    // Increment/decrement for integral types
    template<typename U = T>
        requires std::is_integral_v<U>
    T operator++() noexcept {
        return fetch_add(1) + 1;
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator++(int) noexcept {
        return fetch_add(1);
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator--() noexcept {
        return fetch_sub(1) - 1;
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator--(int) noexcept {
        return fetch_sub(1);
    }

    // Compound assignment operators
    template<typename U = T>
        requires std::is_integral_v<U> || std::is_floating_point_v<U>
    T operator+=(T arg) noexcept {
        return fetch_add(arg) + arg;
    }

    template<typename U = T>
        requires std::is_integral_v<U> || std::is_floating_point_v<U>
    T operator-=(T arg) noexcept {
        return fetch_sub(arg) - arg;
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator&=(T arg) noexcept {
        return fetch_and(arg) & arg;
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator|=(T arg) noexcept {
        return fetch_or(arg) | arg;
    }

    template<typename U = T>
        requires std::is_integral_v<U>
    T operator^=(T arg) noexcept {
        return fetch_xor(arg) ^ arg;
    }

    // Metadata access
    [[nodiscard]] std::string_view name() const noexcept {
        return table_entry ? std::string_view(table_entry->name.data()) : std::string_view{};
    }

    // Wait operations (C++20)
    void wait(T old, std::memory_order order = std::memory_order_seq_cst) const noexcept {
        atom_ptr->wait(old, order);
    }

    void notify_one() noexcept {
        atom_ptr->notify_one();
    }

    void notify_all() noexcept {
        atom_ptr->notify_all();
    }
};

// Convenience type aliases for common atomic types
template<typename TableType = table>
using atomic_bool = atomic_value<bool, TableType>;

template<typename TableType = table>
using atomic_int = atomic_value<int, TableType>;

template<typename TableType = table>
using atomic_uint = atomic_value<unsigned int, TableType>;

template<typename TableType = table>
using atomic_size_t = atomic_value<size_t, TableType>;

template<typename TableType = table>
using atomic_int64 = atomic_value<int64_t, TableType>;

template<typename TableType = table>
using atomic_uint64 = atomic_value<uint64_t, TableType>;
} // namespace zeroipc
