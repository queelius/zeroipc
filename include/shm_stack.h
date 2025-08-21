/**
 * @file shm_stack.h
 * @brief Lock-free stack implementation for POSIX shared memory
 * @author POSIX SHM Library Team
 * @date 2025
 * @version 2.0.0
 * 
 * @details
 * Provides a thread-safe LIFO (Last-In-First-Out) stack that can be shared
 * across processes. Uses atomic operations for lock-free push/pop operations.
 * This implementation is based on the robust shm_queue design for improved
 * reliability and consistency.
 * 
 * @par Thread Safety
 * All operations are thread-safe and lock-free using atomic compare-and-swap.
 * 
 * @par Example
 * @code
 * posix_shm shm("simulation", 10 * 1024 * 1024);
 * shm_stack<int> stack(shm, "undo_stack", 100);
 * 
 * // Push operations
 * if (stack.push(42)) {
 *     std::cout << "Pushed 42\n";
 * }
 * 
 * // Pop operations  
 * if (auto val = stack.pop()) {
 *     std::cout << "Popped: " << *val << "\n";
 * }
 * 
 * // Check state
 * std::cout << "Size: " << stack.size() << "\n";
 * std::cout << "Empty: " << stack.empty() << "\n";
 * @endcode
 */

#pragma once
#include "posix_shm.h"
#include "shm_span.h"
#include "shm_table.h"
#include <atomic>
#include <concepts>
#include <optional>
#include <string_view>

/**
 * @brief Lock-free stack for shared memory
 * 
 * @tparam T Value type (must be trivially copyable)
 * @tparam TableType Table implementation for metadata (default: shm_table)
 * 
 * @details
 * Implements a bounded stack using an array with atomic top pointer.
 * Push and pop operations are lock-free using compare-and-swap.
 * Uses an internal capacity of user_capacity+1 to avoid edge cases.
 * 
 * @par Memory Layout
 * @code
 * [StackHeader][T][T][T]...[T]
 *      ^
 *      |-- atomic top index (next free slot)
 * @endcode
 * 
 * @par Performance
 * - Push: O(1) with CAS retry
 * - Pop: O(1) with CAS retry  
 * - Top: O(1) atomic read
 * - Size: O(1) atomic read
 * - Empty/Full: O(1) atomic read
 * 
 * @warning T must be trivially copyable for shared memory compatibility
 * @note Maximum capacity limited by available shared memory and table entries
 */
template<typename T, typename TableType = shm_table>
    requires std::is_trivially_copyable_v<T>
class shm_stack : public shm_span<T, posix_shm_impl<TableType>> {
private:
    struct StackHeader {
        std::atomic<size_t> top{0};  // Index of next free slot
        size_t capacity;              // Maximum number of elements
    };
    
    StackHeader* header() {
        return reinterpret_cast<StackHeader*>(
            static_cast<char*>(this->shm.get_base_addr()) + this->offset
        );
    }
    
    const StackHeader* header() const {
        return reinterpret_cast<const StackHeader*>(
            static_cast<const char*>(this->shm.get_base_addr()) + this->offset
        );
    }
    
    T* data_start() {
        return reinterpret_cast<T*>(
            reinterpret_cast<char*>(header()) + sizeof(StackHeader)
        );
    }
    
    const T* data_start() const {
        return reinterpret_cast<const T*>(
            reinterpret_cast<const char*>(header()) + sizeof(StackHeader)
        );
    }

    const typename TableType::entry* _table_entry{nullptr};
    
public:
    using value_type = T;
    
    /**
     * @brief Check if a stack exists in shared memory
     * @param shm Shared memory segment
     * @param name Stack identifier to check
     * @return true if stack exists, false otherwise
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
     * @brief Create or open a shared memory stack
     * 
     * @param shm Shared memory segment
     * @param name Unique identifier for the stack
     * @param capacity Maximum number of elements (0 to open existing)
     * 
     * @throws std::runtime_error if:
     *   - capacity is 0 and stack doesn't exist
     *   - capacity is non-zero but doesn't match existing stack
     * 
     * @note Use capacity > 0 to create a new stack
     * @note Use capacity = 0 to open an existing stack
     * @note Use exists() to check before opening if unsure
     */
    template<typename ShmType>
    shm_stack(ShmType& shm, std::string_view name, size_t capacity = 0)
        : shm_span<T, posix_shm_impl<TableType>>(shm, 0, 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match stack table type");
        
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        // Convert string_view to null-terminated string for find
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing stack
            if (capacity != 0 && entry->num_elem != capacity) {
                throw std::runtime_error("Stack capacity mismatch");
            }
            this->offset = entry->offset;
            this->num_elem = entry->num_elem;
            _table_entry = entry;
            
            // Validate that the header has been initialized
            auto* hdr = header();
            if (hdr->capacity == 0) {
                // Header wasn't properly initialized, fix it
                // Add 1 to avoid any zero-capacity edge cases
                hdr->capacity = this->num_elem + 1;
                hdr->top.store(0, std::memory_order_relaxed);
            }
        } else if (capacity > 0) {
            // Create new stack
            // Add 1 to capacity like queue does to avoid edge cases
            size_t actual_capacity = capacity + 1;
            size_t required_size = sizeof(StackHeader) + actual_capacity * sizeof(T);
            size_t current_used = table->get_total_allocated_size();
            
            // Data must come after the table
            size_t base_offset = sizeof(TableType) + current_used;
            
            // Align to 64-byte boundary for better cache performance
            void* base_ptr = shm.get_base_addr();
            void* target_ptr = static_cast<char*>(base_ptr) + base_offset;
            uintptr_t addr = reinterpret_cast<uintptr_t>(target_ptr);
            uintptr_t aligned_addr = (addr + 63) & ~63;
            
            // Calculate the aligned offset from base
            this->offset = aligned_addr - reinterpret_cast<uintptr_t>(base_ptr);
            this->num_elem = capacity;  // Store user capacity
            
            // Initialize header with actual capacity
            new (header()) StackHeader{.capacity = actual_capacity};
            
            // Register in table with user capacity
            if (!table->add(name_buf, this->offset, required_size, sizeof(T), capacity)) {
                throw std::runtime_error("Failed to register stack in table - table may be full");
            }
            // Find the entry we just added
            _table_entry = table->find(name_buf);
        } else {
            throw std::runtime_error("Stack not found and capacity not specified");
        }
    }
    
    /**
     * @brief Push element onto stack
     * 
     * @param value Element to push
     * @return true if successful, false if full
     * 
     * @par Thread Safety
     * Lock-free using compare-and-swap. Multiple threads can push concurrently.
     * 
     * @par Complexity
     * O(1) amortized, may retry on contention
     * 
     * @par Example
     * @code
     * if (!stack.push(42)) {
     *     std::cerr << "Stack is full!\n";
     * }
     * @endcode
     */
    [[nodiscard]] bool push(const T& value) noexcept {
        auto* hdr = header();
        size_t current_top = hdr->top.load(std::memory_order_relaxed);
        
        // Check if full (leave one slot empty like queue does)
        if (current_top >= hdr->capacity - 1) {
            return false; // Stack is full
        }
        
        // Try to claim the slot
        size_t next_top = current_top + 1;
        while (!hdr->top.compare_exchange_weak(current_top, next_top,
                                                std::memory_order_release,
                                                std::memory_order_acquire)) {
            // Check again if full
            if (current_top >= hdr->capacity - 1) {
                return false;
            }
            next_top = current_top + 1;
        }
        
        // Successfully claimed slot at current_top, write the value
        data_start()[current_top] = value;
        return true;
    }
    
    /**
     * @brief Pop element from stack
     * 
     * @return Element value or std::nullopt if empty
     * 
     * @par Thread Safety
     * Lock-free using compare-and-swap. Multiple threads can pop concurrently.
     * 
     * @par Complexity
     * O(1) amortized, may retry on contention
     * 
     * @par Example
     * @code
     * if (auto val = stack.pop()) {
     *     std::cout << "Popped: " << *val << "\n";
     * } else {
     *     std::cout << "Stack was empty\n";
     * }
     * @endcode
     */
    [[nodiscard]] std::optional<T> pop() noexcept {
        auto* hdr = header();
        size_t current_top = hdr->top.load(std::memory_order_relaxed);
        
        if (current_top == 0) {
            return std::nullopt; // Stack is empty
        }
        
        // Read the value first
        T value = data_start()[current_top - 1];
        
        // Try to update top
        while (!hdr->top.compare_exchange_weak(current_top, current_top - 1,
                                                std::memory_order_release,
                                                std::memory_order_acquire)) {
            if (current_top == 0) {
                return std::nullopt; // Stack became empty
            }
            value = data_start()[current_top - 1];
        }
        
        return value;
    }
    
    /**
     * @brief Try pop with output parameter (for compatibility)
     */
    [[nodiscard]] bool pop(T& out_value) noexcept {
        if (auto val = pop()) {
            out_value = *val;
            return true;
        }
        return false;
    }
    
    /**
     * @brief Peek at top element without removing
     * @return Top element or std::nullopt if empty
     */
    [[nodiscard]] std::optional<T> top() const noexcept {
        auto* hdr = header();
        size_t current_top = hdr->top.load(std::memory_order_acquire);
        
        if (current_top == 0) {
            return std::nullopt;
        }
        
        return data_start()[current_top - 1];
    }
    
    [[nodiscard]] bool empty() const noexcept {
        const auto* hdr = header();
        return hdr->top.load(std::memory_order_acquire) == 0;
    }
    
    [[nodiscard]] bool full() const noexcept {
        const auto* hdr = header();
        size_t current_top = hdr->top.load(std::memory_order_acquire);
        return current_top >= hdr->capacity - 1;
    }
    
    [[nodiscard]] size_t size() const noexcept {
        const auto* hdr = header();
        return hdr->top.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] size_t capacity() const noexcept {
        // Return user-visible capacity
        return this->num_elem;
    }
    
    /**
     * @brief Clear all elements (not thread-safe)
     * @warning This operation is NOT thread-safe
     */
    void clear() noexcept {
        header()->top.store(0, std::memory_order_release);
    }
    
    [[nodiscard]] std::string_view name() const noexcept {
        return _table_entry ? std::string_view(_table_entry->name.data()) : std::string_view{};
    }
};

// Type aliases for common stack types
using shm_stack_int = shm_stack<int>;
using shm_stack_float = shm_stack<float>;
using shm_stack_double = shm_stack<double>;
using shm_stack_uint32 = shm_stack<uint32_t>;
using shm_stack_uint64 = shm_stack<uint64_t>;