#pragma once
#include <cstddef>
#include <string>
#include <array>
#include <cstring>
#include <optional>

using std::optional;
using std::string;

class shm_table
{
    static const size_t MAX_NAME_SIZE = 32;
    static const size_t MAX_ENTRIES = 8;

    struct entry
    {
        std::array<char, MAX_NAME_SIZE> name;
        size_t offset;
        size_t size;
        size_t elem_size;
        size_t num_elem;
        bool active;
    };

    entry entries[MAX_ENTRIES];

public:
    shm_table()
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            entries[i].active = false;
        }
    }

    /**
     * @brief Add a new entry to the table.
     * 
     * @param name Name of the resource that resides in shared memory.
     * @param offset Offset of the resource in shared memory.
     * @param size Size of the resource in bytes.
     * @return bool 
     */
    auto add(const string &name, size_t offset, size_t size)
    {
        if (find(name))
            return false;

        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (!entries[i].active)
            {
                auto &entry = entries[i];
                strncpy(entry.name.data(), name.c_str(), MAX_NAME_SIZE);
                entry.offset = offset;
                entry.size = size;
                entry.active = true;
                return true;
            }
        }
    }

    bool erase(const std::string &name)
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (strcmp(name.c_str(), entries[i].name.data()) == 0)
            {
                entries[i].active = false;
                return true;
            }
        }
        return false;
    }

    optional<entry> find(const std::string &name)
    {
        for (size_t i = 0; i < MAX_ENTRIES; ++i)
        {
            if (strcmp(name.c_str(), entries[i].name.data()) == 0)
                return entries[i];
        }
        return std::nullopt;
    }
};
