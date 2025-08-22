/**
 * @file zeroipc::array.h
 * @brief Fixed-size shared memory array with STL compatibility
 * @author ZeroIPC Library Team
 * @date 2025
 * @version 1.0.0
 */

#pragma once
#include "zeroipc.h"
#include "span.h"
#include "table.h"
#include <stdexcept>
#include <algorithm>
#include <concepts>
#include <span>
#include <string_view>
#include <iterator>

namespace zeroipc {


/**
 * @brief Fixed-size array in shared memory with zero-overhead access
 *
 * @details Provides a fully STL-compliant array interface for shared memory.
 * Key features:
 * - **Zero-overhead reads**: Direct pointer access, no synchronization
 * - **Named discovery**: Find arrays by name across processes
 * - **STL compatibility**: Works with all standard algorithms
 * - **Range support**: Full C++20 ranges support
 * - **Type safety**: Compile-time checks via concepts
 *
 * Memory layout:
 * ```
 * [T][T][T][T]...  // Contiguous array of T
 * ```
 *
 * @tparam T Element type (must be trivially copyable for shared memory)
 * @tparam TableType Metadata table type (default: table)
 * 
 * @note Read performance identical to native arrays (~2.3ns per access)
 * @note No synchronization provided - use external locking if needed
 * @warning Cannot resize after creation (fixed-size)
 * 
 * @par Example:
 * @code
 * // Process 1: Create and populate
 * memory shm("sim", 10*1024*1024);
 * array<double> temps(shm, "temperatures", 1000);
 * temps[0] = 25.5;
 * 
 * // Process 2: Discover and read
 * memory shm("sim");
 * array<double> temps(shm, "temperatures");  // Auto-discovers size
 * double t = temps[0];  // Zero-overhead read
 * @endcode
 * 
 * @see zeroipc::span Base class providing core functionality
 * @see zeroipc::queue For FIFO operations
 * @see zeroipc::pool For dynamic allocation
 */
template <typename T, typename TableType = table>
    requires std::is_trivially_copyable_v<T>
class array : public zeroipc::span<T, memory_impl<TableType>>
{
private:
    const typename TableType::entry* _table_entry{nullptr};  ///< Cached table entry for name lookup

public:
    /**
     * @brief Create new array or attach to existing array by name
     *
     * @tparam ShmType Type of shared memory (must have matching TableType)
     * @param shared_mem Shared memory segment to allocate from
     * @param name Unique identifier for the array (max TableType::MAX_NAME_SIZE chars)
     * @param count Number of elements (0 = attach to existing)
     * 
     * @throws std::runtime_error if creation fails (insufficient space)
     * @throws std::runtime_error if name not found and count=0
     * @throws std::runtime_error if existing array has different size
     * 
     * @note Name is truncated if longer than MAX_NAME_SIZE
     * @note Uses stack allocation - cannot be deallocated
     * 
     * @par Thread Safety:
     * Constructor is thread-safe for different names.
     * Race condition possible for same name.
     * 
     * @par Example:
     * @code
     * // Create new array of 1000 floats
     * array<float> arr(shm, "sensor_data", 1000);
     * 
     * // Attach to existing array (size auto-discovered)
     * array<float> arr2(shm, "sensor_data");
     * @endcode
     */
    template<typename ShmType>
    array(ShmType &shared_mem, std::string_view name, size_t count = 0)
        : zeroipc::span<T, memory_impl<TableType>>(shared_mem, 0, 0)
    {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match array table type");
        initialize(name, count);
    }

    /**
     * @brief Internal initialization helper
     * @internal
     * 
     * @param name Array name for table lookup
     * @param count Number of elements (0 = attach existing)
     * 
     * @throws std::runtime_error on various failure conditions
     */
    void initialize(std::string_view name, size_t count = 0)
    {
        auto *table = static_cast<TableType *>(this->shm.get_base_addr());
        
        // Convert string_view to null-terminated string for find
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto *entry = table->find(name_buf);

        if (entry)
        {
            // Open existing array
            if (count != 0 && entry->num_elem != count)
            {
                throw std::runtime_error("Mismatch in array size");
            }
            this->offset = entry->offset;  // Keep as byte offset
            this->num_elem = entry->num_elem;
            _table_entry = entry;
        }
        else if (count > 0)
        {
            // Create new array
            size_t required_size = count * sizeof(T);
            size_t current_used = table->get_total_allocated_size();
            // get_total_size() already excludes the header (which includes the table)
            size_t available_size = this->shm.get_total_size() - current_used;
            
            if (required_size > available_size)
            {
                throw std::runtime_error("Not enough space in shared memory");
            }
            
            // Data must come after the table
            size_t base_offset = sizeof(TableType) + current_used;
            
            // Align to 64-byte boundary for better cache performance
            // This ensures consistency across all data structures
            void* base_ptr = this->shm.get_base_addr();
            void* target_ptr = static_cast<char*>(base_ptr) + base_offset;
            uintptr_t addr = reinterpret_cast<uintptr_t>(target_ptr);
            uintptr_t aligned_addr = (addr + 63) & ~63;
            
            // Calculate the aligned offset from base
            this->offset = aligned_addr - reinterpret_cast<uintptr_t>(base_ptr);
            this->num_elem = count;
            
            if (!table->add(name_buf, this->offset, required_size, sizeof(T), count))
            {
                throw std::runtime_error("Failed to add array to table");
            }
            // Find the entry we just added to get a pointer to it
            _table_entry = table->find(name_buf);
        }
        else
        {
            throw std::runtime_error("Array not found and size not specified");
        }
    }

    /**
     * @brief Bounds-checked element access
     * 
     * @param pos Index to access
     * @return Reference to element
     * 
     * @throws std::out_of_range if pos >= size()
     * 
     * @note Use operator[] for unchecked access (faster)
     * 
     * @par Complexity: O(1)
     */
    [[nodiscard]] T &at(size_t pos)
    {
        if (pos >= this->num_elem)
        {
            throw std::out_of_range("Array index out of range");
        }
        return (*this)[pos];
    }

    /// @brief Const version of at()
    /// @see at(size_t)
    [[nodiscard]] const T &at(size_t pos) const
    {
        if (pos >= this->num_elem)
        {
            throw std::out_of_range("Array index out of range");
        }
        return (*this)[pos];
    }

    /// @brief Access first element
    /// @return Reference to first element
    /// @pre !empty()
    [[nodiscard]] T &front() { return *this->data(); }
    
    /// @brief Access first element (const)
    [[nodiscard]] const T &front() const { return *this->data(); }

    /// @brief Access last element
    /// @return Reference to last element
    /// @pre !empty()
    [[nodiscard]] T &back() { return *(this->data() + this->num_elem - 1); }
    
    /// @brief Access last element (const)
    [[nodiscard]] const T &back() const { return *(this->data() + this->num_elem - 1); }

    /// @brief Check if array is empty
    /// @return true if size() == 0
    [[nodiscard]] bool empty() const noexcept { return this->num_elem == 0; }
    
    /// @brief Get number of elements
    /// @return Number of elements in array
    [[nodiscard]] size_t size() const noexcept { return this->num_elem; }

    /**
     * @brief Fill array with value
     * 
     * @param value Value to fill with
     * 
     * @note No synchronization - use external locking if needed
     * 
     * @par Complexity: O(n)
     * 
     * @par Example:
     * @code
     * array<int> arr(shm, "data", 1000);
     * arr.fill(42);  // All elements = 42
     * @endcode
     */
    void fill(const T &value)
    {
        std::fill_n(this->data(), this->num_elem, value);
    }

    /**
     * @brief Convert to std::span for modern C++ usage
     * 
     * @return std::span view of the array
     * 
     * @note Allows use with C++20 ranges and algorithms
     * 
     * @par Example:
     * @code
     * auto span = arr.as_span();
     * std::ranges::sort(span);
     * @endcode
     */
    [[nodiscard]] std::span<T> as_span() noexcept
    {
        return std::span<T>(this->data(), this->num_elem);
    }

    /// @brief Convert to const span
    [[nodiscard]] std::span<const T> as_span() const noexcept
    {
        return std::span<const T>(this->data(), this->num_elem);
    }

    /// @name STL Type Definitions
    /// @{
    using value_type = T;                                    ///< Element type
    using size_type = size_t;                               ///< Size type
    using difference_type = std::ptrdiff_t;                 ///< Iterator difference
    using reference = T&;                                   ///< Reference type
    using const_reference = const T&;                       ///< Const reference
    using pointer = T*;                                     ///< Pointer type
    using const_pointer = const T*;                         ///< Const pointer
    using iterator = T*;                                     ///< Iterator type (raw pointer)
    using const_iterator = const T*;                        ///< Const iterator
    using reverse_iterator = std::reverse_iterator<iterator>; ///< Reverse iterator
    using const_reverse_iterator = std::reverse_iterator<const_iterator>; ///< Const reverse
    /// @}
    
    /// @name Iterator Support
    /// @{
    
    /// @brief Get iterator to beginning
    [[nodiscard]] iterator begin() noexcept { return this->data(); }
    
    /// @brief Get iterator to end
    [[nodiscard]] iterator end() noexcept { return this->data() + this->num_elem; }
    
    /// @brief Get const iterator to beginning
    [[nodiscard]] const_iterator begin() const noexcept { return this->data(); }
    
    /// @brief Get const iterator to end
    [[nodiscard]] const_iterator end() const noexcept { return this->data() + this->num_elem; }
    
    /// @brief Get const iterator to beginning (explicit)
    [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
    
    /// @brief Get const iterator to end (explicit)
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }
    /// @}
    
    /// @name Reverse Iterator Support
    /// @{
    
    /// @brief Get reverse iterator to beginning
    [[nodiscard]] reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    
    /// @brief Get reverse iterator to end
    [[nodiscard]] reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    
    /// @brief Get const reverse iterator to beginning
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    
    /// @brief Get const reverse iterator to end
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    
    /// @brief Get const reverse iterator to beginning (explicit)
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return rbegin(); }
    
    /// @brief Get const reverse iterator to end (explicit)
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return rend(); }
    /// @}
    
    /// @brief Get maximum size (same as size() for fixed arrays)
    /// @return Maximum number of elements (equals size())
    [[nodiscard]] size_type max_size() const noexcept { return this->num_elem; }
    
    /// @brief Get pointer to underlying data
    /// @return Pointer to first element (or nullptr if empty)
    /// @note Direct memory access - use with caution
    [[nodiscard]] pointer data() noexcept { return zeroipc::span<T, memory_impl<TableType>>::data(); }
    
    /// @brief Get const pointer to underlying data
    [[nodiscard]] const_pointer data() const noexcept { return zeroipc::span<T, memory_impl<TableType>>::data(); }

    /**
     * @brief Get array name from metadata table
     * 
     * @return Name used to create/find this array
     * 
     * @note Returns empty string_view if table entry lost
     * 
     * @par Complexity: O(1) - cached lookup
     * 
     * @par Example:
     * @code
     * array<int> arr(shm, "sensor_data", 100);
     * std::cout << arr.name();  // Prints: sensor_data
     * @endcode
     */
    [[nodiscard]] std::string_view name() const noexcept { 
        return _table_entry ? std::string_view(_table_entry->name.data()) : std::string_view{};
    }
};
} // namespace zeroipc
