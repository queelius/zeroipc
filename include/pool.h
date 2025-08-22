#pragma once
#include "zeroipc.h"
#include "table.h"
#include <atomic>
#include <optional>
#include <span>
#include <bit>

namespace zeroipc {


/**
 * @brief High-performance object pool for shared memory
 * 
 * Manages a preallocated pool of objects with O(1) acquire/release.
 * Perfect for simulations with many temporary objects (particles, entities, etc).
 * 
 * Uses a lock-free free list implemented as a stack for fast allocation.
 * Objects are stored contiguously for cache efficiency.
 * 
 * @tparam T Type of objects in the pool
 * @tparam TableType Metadata table type
 */
template<typename T, typename TableType = table>
    requires std::is_trivially_copyable_v<T>
class pool {
private:
    struct PoolHeader {
        std::atomic<uint32_t> free_head{0};  // Head of free list (stack)
        std::atomic<uint32_t> num_allocated{0};  // Statistics
        uint32_t capacity;
        uint32_t next_array[];  // Next pointers for free list
        // Objects stored after next_array[capacity]
    };

    static constexpr uint32_t NULL_INDEX = std::numeric_limits<uint32_t>::max();
    
    PoolHeader* header_{nullptr};
    T* objects_{nullptr};
    const typename TableType::entry* table_entry_{nullptr};
    
    PoolHeader* get_header(void* base_addr, size_t offset) {
        return reinterpret_cast<PoolHeader*>(
            static_cast<char*>(base_addr) + offset
        );
    }

public:
    using value_type = T;
    using size_type = size_t;
    using handle_type = uint32_t;  // Index into pool
    
    static constexpr handle_type invalid_handle = NULL_INDEX;

    template<typename ShmType>
    pool(ShmType& shm, std::string_view name, size_t capacity = 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match pool table type");

        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing pool
            header_ = get_header(shm.get_base_addr(), entry->offset);
            objects_ = reinterpret_cast<T*>(
                reinterpret_cast<char*>(header_) + 
                sizeof(PoolHeader) + sizeof(uint32_t) * header_->capacity
            );
            table_entry_ = entry;
        } else if (capacity > 0) {
            // Create new pool
            size_t header_size = sizeof(PoolHeader) + sizeof(uint32_t) * capacity;
            size_t objects_size = sizeof(T) * capacity;
            size_t total_size = header_size + objects_size;
            
            size_t table_size = sizeof(TableType);
            size_t current_used = table->get_total_allocated_size();
            size_t offset = table_size + current_used;
            
            header_ = get_header(shm.get_base_addr(), offset);
            
            // Initialize header
            new (header_) PoolHeader();
            header_->capacity = static_cast<uint32_t>(capacity);
            header_->free_head = 0;
            header_->num_allocated = 0;
            
            // Initialize free list (all objects initially free)
            for (uint32_t i = 0; i < capacity; ++i) {
                header_->next_array[i] = i + 1;
            }
            header_->next_array[capacity - 1] = NULL_INDEX;
            
            objects_ = reinterpret_cast<T*>(
                reinterpret_cast<char*>(header_) + header_size
            );
            
            // Register in table
            if (!table->add(name_buf, offset, total_size, sizeof(T), capacity)) {
                throw std::runtime_error("Failed to add pool to table");
            }
            table_entry_ = table->find(name_buf);
        } else {
            throw std::runtime_error("Pool not found and capacity not specified");
        }
    }

    /**
     * @brief Acquire an object from the pool
     * @return Handle to the acquired object, or invalid_handle if pool is full
     */
    [[nodiscard]] handle_type acquire() noexcept {
        uint32_t old_head = header_->free_head.load(std::memory_order_acquire);
        
        while (old_head != NULL_INDEX) {
            uint32_t new_head = header_->next_array[old_head];
            
            if (header_->free_head.compare_exchange_weak(
                    old_head, new_head,
                    std::memory_order_release,
                    std::memory_order_acquire)) {
                
                header_->num_allocated.fetch_add(1, std::memory_order_relaxed);
                return old_head;
            }
        }
        
        return invalid_handle;  // Pool exhausted
    }

    /**
     * @brief Acquire and construct an object
     * @return Optional containing handle if successful
     */
    template<typename... Args>
    [[nodiscard]] std::optional<handle_type> acquire_construct(Args&&... args) {
        handle_type handle = acquire();
        if (handle != invalid_handle) {
            new (&objects_[handle]) T(std::forward<Args>(args)...);
            return handle;
        }
        return std::nullopt;
    }

    /**
     * @brief Release an object back to the pool
     */
    void release(handle_type handle) noexcept {
        if (handle >= header_->capacity) return;  // Invalid handle
        
        // Push onto free list stack
        uint32_t old_head = header_->free_head.load(std::memory_order_acquire);
        do {
            header_->next_array[handle] = old_head;
        } while (!header_->free_head.compare_exchange_weak(
            old_head, handle,
            std::memory_order_release,
            std::memory_order_acquire));
        
        header_->num_allocated.fetch_sub(1, std::memory_order_relaxed);
    }

    /**
     * @brief Access object by handle
     */
    [[nodiscard]] T& operator[](handle_type handle) noexcept {
        return objects_[handle];
    }

    [[nodiscard]] const T& operator[](handle_type handle) const noexcept {
        return objects_[handle];
    }

    [[nodiscard]] T* get(handle_type handle) noexcept {
        if (handle >= header_->capacity) return nullptr;
        return &objects_[handle];
    }

    [[nodiscard]] const T* get(handle_type handle) const noexcept {
        if (handle >= header_->capacity) return nullptr;
        return &objects_[handle];
    }

    /**
     * @brief Check if handle is valid
     */
    [[nodiscard]] bool is_valid(handle_type handle) const noexcept {
        return handle < header_->capacity;
    }

    /**
     * @brief Get pool statistics
     */
    [[nodiscard]] size_t capacity() const noexcept {
        return header_->capacity;
    }

    [[nodiscard]] size_t num_allocated() const noexcept {
        return header_->num_allocated.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t num_available() const noexcept {
        return capacity() - num_allocated();
    }

    [[nodiscard]] bool empty() const noexcept {
        return num_allocated() == 0;
    }

    [[nodiscard]] bool full() const noexcept {
        return num_allocated() == capacity();
    }

    /**
     * @brief Get view of all objects (including free ones)
     * Use with caution - includes uninitialized objects
     */
    [[nodiscard]] std::span<T> unsafe_all_objects() noexcept {
        return std::span<T>(objects_, header_->capacity);
    }

    [[nodiscard]] std::span<const T> unsafe_all_objects() const noexcept {
        return std::span<const T>(objects_, header_->capacity);
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return table_entry_ ? std::string_view(table_entry_->name.data()) : std::string_view{};
    }

    /**
     * @brief Batch acquire multiple objects
     * @param count Number of objects to acquire
     * @param[out] handles Array to store acquired handles
     * @return Number of objects actually acquired
     */
    size_t acquire_batch(size_t count, handle_type* handles) noexcept {
        size_t acquired = 0;
        for (size_t i = 0; i < count; ++i) {
            handle_type h = acquire();
            if (h == invalid_handle) break;
            handles[acquired++] = h;
        }
        return acquired;
    }

    /**
     * @brief Batch release multiple objects
     */
    void release_batch(std::span<const handle_type> handles) noexcept {
        for (handle_type h : handles) {
            release(h);
        }
    }
};
} // namespace zeroipc
