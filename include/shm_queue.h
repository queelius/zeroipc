#pragma once
#include "posix_shm.h"
#include "shm_span.h"
#include "shm_table.h"
#include <atomic>
#include <concepts>
#include <span>
#include <optional>
#include <string_view>

/**
 * @brief Lock-free circular queue for shared memory IPC
 * 
 * Uses C++20/23 features for modern, efficient implementation:
 * - std::atomic for lock-free operations
 * - std::optional for safe dequeue operations
 * - Concepts for type constraints
 * 
 * @tparam T The type of elements stored in the queue. Must be trivially copyable.
 * @tparam TableType The type of shm_table to use (default: shm_table)
 */
template<typename T, typename TableType = shm_table>
    requires std::is_trivially_copyable_v<T>
class shm_queue : public shm_span<T, posix_shm_impl<TableType>> {
private:
    struct QueueHeader {
        std::atomic<size_t> head{0};
        std::atomic<size_t> tail{0};
        size_t capacity;
    };
    
    QueueHeader* header() {
        return reinterpret_cast<QueueHeader*>(
            static_cast<char*>(this->shm.get_base_addr()) + this->offset
        );
    }
    
    const QueueHeader* header() const {
        return reinterpret_cast<const QueueHeader*>(
            static_cast<const char*>(this->shm.get_base_addr()) + this->offset
        );
    }
    
    T* data_start() {
        return reinterpret_cast<T*>(
            reinterpret_cast<char*>(header()) + sizeof(QueueHeader)
        );
    }

    const typename TableType::entry* _table_entry{nullptr};
    
public:
    /**
     * @brief Check if a queue exists in shared memory
     * @param shm Shared memory segment
     * @param name Queue identifier to check
     * @return true if queue exists, false otherwise
     */
    template<typename ShmType>
    static bool exists(ShmType& shm, std::string_view name) {
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return table->find(name_buf) != nullptr;
    }
    
    /**
     * @brief Create or open a shared memory queue
     * 
     * @param shm Shared memory segment
     * @param name Unique identifier for the queue
     * @param capacity Maximum number of elements (0 to open existing)
     * 
     * @throws std::runtime_error if:
     *   - capacity is 0 and queue doesn't exist
     *   - capacity is non-zero but doesn't match existing queue
     * 
     * @note Use capacity > 0 to create a new queue
     * @note Use capacity = 0 to open an existing queue
     * @note Use exists() to check before opening if unsure
     */
    template<typename ShmType>
    shm_queue(ShmType& shm, std::string_view name, size_t capacity = 0)
        : shm_span<T, posix_shm_impl<TableType>>(shm, 0, 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match queue table type");
        
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        // Convert string_view to null-terminated string for find
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing queue
            if (capacity != 0 && entry->num_elem != capacity) {
                throw std::runtime_error("Queue capacity mismatch");
            }
            this->offset = entry->offset;
            this->num_elem = entry->num_elem;
            _table_entry = entry;
            
            // Validate that the header has been initialized
            auto* hdr = header();
            if (hdr->capacity == 0) {
                // Header wasn't properly initialized, fix it
                hdr->capacity = this->num_elem + 1;  // Internal capacity
                hdr->head.store(0, std::memory_order_relaxed);
                hdr->tail.store(0, std::memory_order_relaxed);
            }
        } else if (capacity > 0) {
            // Create new queue
            // Allocate capacity+1 to allow full use of capacity (circular queue needs one empty slot)
            size_t actual_capacity = capacity + 1;
            size_t required_size = sizeof(QueueHeader) + actual_capacity * sizeof(T);
            size_t current_used = table->get_total_allocated_size();
            
            // Data must come after the table
            size_t base_offset = sizeof(TableType) + current_used;
            
            // Align to 64-byte boundary for better cache performance
            // Even though QueueHeader doesn't require it, this ensures consistency
            void* base_ptr = shm.get_base_addr();
            void* target_ptr = static_cast<char*>(base_ptr) + base_offset;
            uintptr_t addr = reinterpret_cast<uintptr_t>(target_ptr);
            uintptr_t aligned_addr = (addr + 63) & ~63;
            
            // Calculate the aligned offset from base
            this->offset = aligned_addr - reinterpret_cast<uintptr_t>(base_ptr);
            this->num_elem = capacity;
            
            // Initialize header with actual capacity
            new (header()) QueueHeader{.capacity = actual_capacity};
            
            // Register in table
            if (!table->add(name_buf, this->offset, required_size, sizeof(T), capacity)) {
                throw std::runtime_error("Failed to register queue in table - table may be full");
            }
            // Find the entry we just added
            _table_entry = table->find(name_buf);
        } else {
            throw std::runtime_error("Queue not found and capacity not specified");
        }
    }
    
    /**
     * @brief Enqueue an element (lock-free)
     * @return true if successful, false if queue is full
     */
    [[nodiscard]] bool enqueue(const T& value) noexcept {
        auto* hdr = header();
        size_t current_tail = hdr->tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % hdr->capacity;
        
        if (next_tail == hdr->head.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        data_start()[current_tail] = value;
        hdr->tail.store(next_tail, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Dequeue an element (lock-free)
     * @return std::optional with value if successful, empty if queue is empty
     */
    [[nodiscard]] std::optional<T> dequeue() noexcept {
        auto* hdr = header();
        size_t current_head = hdr->head.load(std::memory_order_relaxed);
        
        if (current_head == hdr->tail.load(std::memory_order_acquire)) {
            return std::nullopt; // Queue is empty
        }
        
        T value = data_start()[current_head];
        size_t next_head = (current_head + 1) % hdr->capacity;
        hdr->head.store(next_head, std::memory_order_release);
        
        return value;
    }
    
    /**
     * @brief Try dequeue with output parameter (for compatibility)
     */
    [[nodiscard]] bool dequeue(T& out_value) noexcept {
        if (auto val = dequeue()) {
            out_value = *val;
            return true;
        }
        return false;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        const auto* hdr = header();
        return hdr->head.load(std::memory_order_acquire) == 
               hdr->tail.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] bool full() const noexcept {
        const auto* hdr = header();
        size_t current_tail = hdr->tail.load(std::memory_order_acquire);
        size_t next_tail = (current_tail + 1) % hdr->capacity;
        return next_tail == hdr->head.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] size_t size() const noexcept {
        const auto* hdr = header();
        size_t h = hdr->head.load(std::memory_order_acquire);
        size_t t = hdr->tail.load(std::memory_order_acquire);
        
        if (t >= h) {
            return t - h;
        } else {
            return hdr->capacity - h + t;
        }
    }
    
    [[nodiscard]] size_t capacity() const noexcept {
        // Return user-visible capacity
        // For new queues: stored in num_elem
        // For consistency, use the stored value
        return this->num_elem;
    }
    
    [[nodiscard]] std::string_view name() const noexcept {
        return _table_entry ? std::string_view(_table_entry->name.data()) : std::string_view{};
    }
};