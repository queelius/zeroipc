#pragma once
#include <cstddef>
#include <string>

class shm_table {
public:
    shm_table() : num_entries(0) {}

    auto push_back(const std::string& name, size_t offset, size_t size, size_t elem_size, size_t num_elem) {
        if (num_entries >= MAX_ENTRIES) return false;
        auto& entry = entries[num_entries++];
        strncpy(entry.name.data(), name.c_str(), MAX_NAME_SIZE);
        entry.offset = offset;
        entry.size = size;
        entry.element_size = elem_size;
        entry.num_elements = num_elem;
        return true;
    }

    auto size() const {
        return num_entries;
    }

    row* find(const std::string& name) {
        for (size_t i = 0; i < num_entries; ++i) {
            if (strcmp(name, entries[i].name) == 0) return &entries[i];
        }
        return nullptr;
    }

private:
    struct row {
        std::array<char, MAX_NAME_SIZE> name;
        size_t offset;
        size_t size;
        size_t elem_size;
        size_t num_elem;
    };

    static const size_t MAX_NAME_SIZE = 32;
    static const size_t MAX_ENTRIES = 100;
    row entries[MAX_ENTRIES];
    size_t num_entries;
};
