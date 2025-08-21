#pragma once
#include <cstddef>
#include <string>
#include <array>
#include <cstring>
#include <optional>
#include <algorithm>
#include <string_view>

/**
 * @brief Metadata table for managing shared memory data structures
 * 
 * Stored at the beginning of shared memory segment to allow
 * dynamic discovery and management of data structures.
 * 
 * @tparam MaxNameSize Maximum size for entry names (default 32)
 * @tparam MaxEntries Maximum number of entries in the table (default 64)
 */
template<size_t MaxNameSize = 32, size_t MaxEntries = 64>
class shm_table_impl
{
public:
    static constexpr size_t MAX_NAME_SIZE = MaxNameSize;
    static constexpr size_t MAX_ENTRIES = MaxEntries;

    struct entry
    {
        std::array<char, MAX_NAME_SIZE> name{};
        size_t offset{0};
        size_t size{0};
        size_t elem_size{0};
        size_t num_elem{0};
        bool active{false};
    };

private:
    entry entries[MAX_ENTRIES]{};
    size_t total_allocated{0};

public:
    shm_table_impl() = default;

    /**
     * @brief Add a new entry to the table.
     * 
     * @param name Name of the resource that resides in shared memory.
     * @param offset Offset of the resource in shared memory.
     * @param size Total size of the resource in bytes.
     * @param elem_size Size of each element (for arrays/queues).
     * @param num_elem Number of elements.
     * @return true if added successfully, false if name exists or table full.
     */
    bool add(const char* name, size_t offset, size_t size, 
             size_t elem_size = 0, size_t num_elem = 0)
    {
        if (find(name))
            return false;

        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (!entries[i].active)
            {
                auto &e = entries[i];
                std::strncpy(e.name.data(), name, MAX_NAME_SIZE - 1);
                e.name[MAX_NAME_SIZE - 1] = '\0';
                e.offset = offset;
                e.size = size;
                e.elem_size = elem_size;
                e.num_elem = num_elem;
                e.active = true;
                
                // Update total allocated
                // offset is absolute from base, so subtract table size
                size_t relative_end = (offset - sizeof(*this)) + size;
                total_allocated = std::max(total_allocated, relative_end);
                
                return true;
            }
        }
        return false;
    }

    bool add(std::string_view name, size_t offset, size_t size,
             size_t elem_size = 0, size_t num_elem = 0)
    {
        char name_buf[MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return add(name_buf, offset, size, elem_size, num_elem);
    }

    bool erase(const char* name)
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (entries[i].active && 
                std::strcmp(name, entries[i].name.data()) == 0)
            {
                entries[i].active = false;
                // Note: doesn't reclaim space, just marks as inactive
                return true;
            }
        }
        return false;
    }

    bool erase(std::string_view name)
    {
        char name_buf[MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return erase(name_buf);
    }

    /**
     * @brief Find an entry by name
     * @return Pointer to entry if found, nullptr otherwise
     */
    entry* find(const char* name)
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (entries[i].active && 
                std::strcmp(name, entries[i].name.data()) == 0)
            {
                return &entries[i];
            }
        }
        return nullptr;
    }

    const entry* find(const char* name) const
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (entries[i].active && 
                std::strcmp(name, entries[i].name.data()) == 0)
            {
                return &entries[i];
            }
        }
        return nullptr;
    }

    entry* find(std::string_view name)
    {
        char name_buf[MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return find(name_buf);
    }

    const entry* find(std::string_view name) const
    {
        char name_buf[MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return find(name_buf);
    }

    /**
     * @brief Get total allocated size (excluding the table itself)
     */
    size_t get_total_allocated_size() const
    {
        return total_allocated;
    }
    
    /**
     * @brief Get the number of available entries in the table
     * @return Number of entries that can still be added
     */
    size_t get_available_entries() const
    {
        size_t count = 0;
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (!entries[i].active)
            {
                count++;
            }
        }
        return count;
    }

    /**
     * @brief Get number of active entries
     */
    size_t get_entry_count() const
    {
        size_t count = 0;
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (entries[i].active)
                ++count;
        }
        return count;
    }

    /**
     * @brief Clear all entries (for initialization)
     */
    void clear()
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            entries[i].active = false;
        }
        total_allocated = 0;
    }

    /**
     * @brief Get the actual size of this table in bytes
     */
    static constexpr size_t size_bytes() 
    {
        return sizeof(shm_table_impl<MaxNameSize, MaxEntries>);
    }
};

// Default table type for backward compatibility
using shm_table = shm_table_impl<32, 64>;

// Common alternative configurations
using shm_table_small = shm_table_impl<16, 16>;   // Minimal overhead
using shm_table_large = shm_table_impl<64, 256>;   // More entries, longer names
using shm_table_huge = shm_table_impl<256, 1024>;  // Maximum flexibility