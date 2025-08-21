/**
 * @file posix_shm.h
 * @brief Core POSIX shared memory management with automatic reference counting
 * @author POSIX SHM Library Team
 * @date 2025
 * @version 1.0.0
 * 
 * @details This file provides the foundation for all shared memory operations
 * in the library. It wraps POSIX shared memory primitives with RAII semantics,
 * automatic reference counting, and embedded metadata management.
 * 
 * @see shm_table.h for metadata table implementation
 * @see shm_array.h, shm_queue.h for data structure implementations
 */

#pragma once
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <cerrno>
#include <utility>
#include <algorithm>
#include "shm_table.h"

/**
 * @brief POSIX shared memory wrapper with RAII and reference counting
 * 
 * @details This class provides:
 * - Automatic creation/attachment to shared memory segments
 * - Multi-process safe reference counting
 * - Embedded metadata table for structure discovery
 * - RAII-style automatic cleanup
 * - Zero-overhead access to shared data
 * 
 * The shared memory layout:
 * ```
 * [Header: ref_count + table] [User data...]
 * ```
 * 
 * @tparam TableType The metadata table implementation (default: shm_table)
 * 
 * @note Thread-safe reference counting via std::atomic
 * @note Last process to detach automatically unlinks the segment
 * @warning Uses stack allocation - no individual deallocation supported
 * 
 * @par Example:
 * @code
 * // Process 1: Create and populate
 * posix_shm shm("simulation", 10*1024*1024);
 * shm_array<float> data(shm, "sensor_data", 1000);
 * 
 * // Process 2: Attach and read
 * posix_shm shm("simulation", 0);  // size=0 means attach existing
 * auto* data = shm_array<float>::open(shm, "sensor_data");
 * @endcode
 */
template<typename TableType = shm_table>
class posix_shm_impl
{
private:
    /**
     * @brief Internal header structure at beginning of shared memory
     * @internal
     */
    struct header
    {
        std::atomic<int> ref_count;  ///< Process reference counter
        TableType table;              ///< Embedded metadata table

        /// @brief Atomically increment reference count
        void inc()
        {
            ref_count.fetch_add(1, std::memory_order_relaxed);
        }

        /// @brief Atomically decrement reference count
        /// @return New reference count after decrement
        int dec()
        {
            return ref_count.fetch_sub(1, std::memory_order_release) - 1;
        }
    };

    void *base_addr;     ///< Base address from mmap
    size_t total_size;   ///< Total size including header
    std::string name;    ///< Shared memory name
    int fd;             ///< File descriptor from shm_open
    header *hdr;        ///< Pointer to header structure

public:
    /**
     * @brief Create or attach to a POSIX shared memory segment
     * 
     * @param name Unique identifier for the shared memory segment
     * @param size Size in bytes (0 = attach to existing segment)
     * 
     * @throws std::runtime_error if shm_open fails
     * @throws std::runtime_error if mmap fails
     * @throws std::runtime_error if ftruncate fails (when creating)
     * 
     * @note If size=0, attempts to attach to existing segment
     * @note If size>0, creates new segment or attaches to existing
     * @note Name should not include /dev/shm/ prefix
     * 
     * @par Thread Safety:
     * Constructor is thread-safe when multiple processes/threads
     * attempt to create/attach simultaneously.
     * 
     * @par Example:
     * @code
     * // Create new 10MB segment
     * posix_shm shm("my_sim", 10*1024*1024);
     * 
     * // Attach to existing segment
     * posix_shm shm2("my_sim", 0);
     * @endcode
     */
    posix_shm_impl(const std::string &name, size_t size = 0)
        : name(name), total_size(size + sizeof(header))
    {
        bool created = false;
        
        if (size == 0) {
            // Size 0 means attach to existing only
            fd = shm_open(name.c_str(), O_RDWR, 0666);
            if (fd == -1)
            {
                throw std::runtime_error("Failed to open existing shared memory: " + std::string(strerror(errno)));
            }
            // Need to get the actual size
            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                close(fd);
                throw std::runtime_error("Failed to get shared memory size: " + std::string(strerror(errno)));
            }
            total_size = sb.st_size;
        }
        else {
            // Size > 0 means create new or attach to existing
            fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
            if (fd == -1)
            {
                if (errno == EEXIST)
                {
                    fd = shm_open(name.c_str(), O_RDWR, 0666);
                    if (fd == -1)
                    {
                        throw std::runtime_error("Failed to open existing shared memory: " + std::string(strerror(errno)));
                    }
                }
                else
                {
                    throw std::runtime_error("Failed to create shared memory: " + std::string(strerror(errno)));
                }
            }
            else
            {
                created = true;
            }
        }

        if (created && ftruncate(fd, total_size) == -1)
        {
            close(fd);
            shm_unlink(name.c_str());
            throw std::runtime_error("Failed to set size of shared memory: " + std::string(strerror(errno)));
        }

        base_addr = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (base_addr == MAP_FAILED)
        {
            close(fd);
            if (created)
                shm_unlink(name.c_str());
            throw std::runtime_error("Failed to map shared memory: " + std::string(strerror(errno)));
        }

        hdr = static_cast<header *>(base_addr);
        if (created)
        {
            // Initialize header with reference count 1 and empty table
            new (hdr) header{{1}, TableType{}};
        }
        else
        {
            hdr->inc();
        }
    }

    /**
     * @brief Destructor with automatic cleanup
     * 
     * @details Performs the following:
     * 1. Decrements reference count atomically
     * 2. Unmaps memory region
     * 3. Closes file descriptor
     * 
     * @note Exception-safe: never throws
     * @note Shared memory persists until explicitly unlinked
     */
    ~posix_shm_impl()
    {
        hdr->dec();
        munmap(base_addr, total_size);
        close(fd);
    }

    /**
     * @brief Get base address for user data (after header)
     * 
     * @return Pointer to beginning of user data area
     * 
     * @note Points to the table, user allocations come after
     * @note Address is stable for lifetime of mapping
     * 
     * @par Performance:
     * O(1) - Simple pointer arithmetic
     */
    void *get_base_addr() const
    {
        return &hdr->table;
    }

    /**
     * @brief Get usable size for data (excluding header)
     * 
     * @return Size in bytes available for user allocations
     * 
     * @note This is less than the size passed to constructor
     * @note Header overhead is sizeof(atomic<int>) + sizeof(TableType)
     * 
     * @par Example:
     * @code
     * posix_shm shm("test", 1024*1024);  // Request 1MB
     * size_t usable = shm.get_total_size();  // ~1MB - header
     * @endcode
     */
    size_t get_total_size() const
    {
        return total_size - sizeof(header);
    }
    
    /**
     * @brief Query available capacity for new allocations
     * 
     * @return Structure containing capacity information
     */
    struct capacity_info {
        size_t available_memory;    ///< Bytes available for allocation
        size_t available_entries;   ///< Table entries available
        size_t max_entries;         ///< Maximum table entries
        
        /// Check if allocation of given size would succeed
        bool can_allocate(size_t size_bytes) const {
            return available_entries > 0 && size_bytes <= available_memory;
        }
    };
    
    capacity_info get_capacity() const
    {
        auto* table = get_table();
        size_t allocated = table->get_total_allocated_size();
        size_t total = get_total_size();
        
        return {
            .available_memory = (allocated < total) ? (total - allocated) : 0,
            .available_entries = table->get_available_entries(),
            .max_entries = TableType::MAX_ENTRIES
        };
    }

    /**
     * @brief Get mutable pointer to metadata table
     * 
     * @return Pointer to embedded table for structure registration
     * 
     * @note Use this to register/discover data structures
     * @note Thread-safety depends on TableType implementation
     * 
     * @par Example:
     * @code
     * auto* table = shm.get_table();
     * table->add("my_array", offset, size);
     * @endcode
     */
    TableType* get_table()
    {
        return &hdr->table;
    }

    /**
     * @brief Get const pointer to metadata table
     * 
     * @return Const pointer for read-only table access
     * 
     * @see get_table() for mutable access
     */
    const TableType* get_table() const
    {
        return &hdr->table;
    }

    /**
     * @brief Get current reference count
     * 
     * @return Number of processes attached to this segment
     * 
     * @note Thread-safe via atomic load
     * @note Useful for debugging and monitoring
     * 
     * @par Example:
     * @code
     * if (shm.get_ref_count() == 1) {
     *     // We're the only process attached
     * }
     * @endcode
     */
    int get_ref_count() const
    {
        return hdr->ref_count.load(std::memory_order_acquire);
    }

    /**
     * @brief Explicitly unlink the shared memory segment
     * 
     * @return true if successfully unlinked, false otherwise
     * 
     * @note This removes the shared memory from the system
     * @note Other processes with the segment mapped can continue using it
     * @note Never throws (safe for cleanup code)
     * @warning This will make the shared memory inaccessible to new processes
     * 
     * @par Example:
     * @code
     * posix_shm shm("test", 1024);
     * // ... use shared memory ...
     * shm.unlink();  // Explicitly remove from system
     * @endcode
     */
    bool unlink()
    {
        if (shm_unlink(name.c_str()) == -1)
        {
            return false;
        }
        return true;
    }

    /// @brief Deleted copy constructor (non-copyable)
    posix_shm_impl(const posix_shm_impl &) = delete;
    
    /// @brief Deleted copy assignment (non-copyable)
    posix_shm_impl &operator=(const posix_shm_impl &) = delete;

    /// @brief Type alias for the table type parameter
    using table_type = TableType;
};

/**
 * @brief Default shared memory type with standard table configuration
 * 
 * @details Uses shm_table (32 char names, 64 max entries)
 * Suitable for most applications.
 */
using posix_shm = posix_shm_impl<shm_table>;

/**
 * @brief Small configuration for embedded/constrained systems
 * 
 * @details Uses shm_table_small (16 char names, 16 max entries)
 * Table overhead: ~900 bytes
 */
using posix_shm_small = posix_shm_impl<shm_table_small>;

/**
 * @brief Large configuration for complex simulations
 * 
 * @details Uses shm_table_large (64 char names, 256 max entries)
 * Table overhead: ~26KB
 */
using posix_shm_large = posix_shm_impl<shm_table_large>;

/**
 * @brief Huge configuration for maximum flexibility
 * 
 * @details Uses shm_table_huge (256 char names, 1024 max entries)
 * Table overhead: ~423KB
 * @warning Large overhead - use only when necessary
 */
using posix_shm_huge = posix_shm_impl<shm_table_huge>;