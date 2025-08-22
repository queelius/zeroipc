#pragma once
#include <cstddef>
#include <string>
#include <array>
#include <cstring>
#include <optional>
#include <algorithm>
#include <string_view>
#include <stdexcept>

namespace zeroipc {


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
class table_impl
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
    table_impl() = default;

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
        if (name.size() >= MAX_NAME_SIZE) {
            throw std::invalid_argument(
                "Name '" + std::string(name) + "' exceeds maximum length of " + 
                std::to_string(MAX_NAME_SIZE - 1) + " characters");
        }
        char name_buf[MAX_NAME_SIZE]{};
        std::copy_n(name.begin(), name.size(), name_buf);
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
        return sizeof(table_impl<MaxNameSize, MaxEntries>);
    }
};

// Predefined table types - all use 32-char names
// Naming convention: table{N} where N is the number of entries
using table1 = table_impl<32, 1>;          // Minimal: 1 entry
using table2 = table_impl<32, 2>;          // Dual: 2 entries
using table4 = table_impl<32, 4>;          // Quad: 4 entries
using table8 = table_impl<32, 8>;          // Tiny: 8 entries
using table16 = table_impl<32, 16>;        // Small: 16 entries  
using table32 = table_impl<32, 32>;        // Compact: 32 entries
using table64 = table_impl<32, 64>;        // Standard: 64 entries
using table128 = table_impl<32, 128>;      // Medium: 128 entries
using table256 = table_impl<32, 256>;      // Large: 256 entries
using table512 = table_impl<32, 512>;      // XLarge: 512 entries
using table1024 = table_impl<32, 1024>;    // Huge: 1024 entries
using table2048 = table_impl<32, 2048>;    // XHuge: 2048 entries
using table4096 = table_impl<32, 4096>;    // Maximum: 4096 entries

// Default table type
using table = table64;
} // namespace zeroipc
