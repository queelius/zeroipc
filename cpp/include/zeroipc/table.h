#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <stdexcept>

namespace zeroipc {

constexpr uint32_t TABLE_MAGIC = 0x5A49504D; // 'ZIPM'
constexpr uint32_t TABLE_VERSION = 1;

/**
 * Runtime-configurable table for managing named structures in shared memory.
 * 
 * The table is stored at the beginning of shared memory and tracks
 * all allocated structures by name, offset, and size.
 */
class Table {
public:
    struct Header {
        uint32_t magic;
        uint32_t version;
        uint32_t entry_count;
        uint32_t next_offset;
    };
    
    struct Entry {
        char name[32];
        uint32_t offset;
        uint32_t size;
    };
    
    /**
     * Initialize a table in existing memory
     * @param memory Pointer to the start of shared memory
     * @param max_entries Maximum number of entries this table can hold
     * @param create If true, initialize a new table; if false, open existing
     */
    Table(void* memory, size_t max_entries, bool create = false)
        : memory_(static_cast<char*>(memory))
        , max_entries_(max_entries) {
        
        if (create) {
            initialize();
        } else {
            validate();
        }
    }
    
    /**
     * Find an entry by name
     * @return Pointer to entry or nullptr if not found
     */
    const Entry* find(std::string_view name) const {
        auto* header = get_header();
        auto* entries = get_entries();
        
        for (uint32_t i = 0; i < header->entry_count; ++i) {
            if (name == entries[i].name) {
                return &entries[i];
            }
        }
        return nullptr;
    }
    
    /**
     * Add a new entry to the table
     * @return true if successful, false if table is full
     */
    bool add(std::string_view name, uint32_t offset, uint32_t size) {
        if (name.size() >= 32) {
            throw std::invalid_argument("Name too long (max 31 characters)");
        }
        
        auto* header = get_header();
        if (header->entry_count >= max_entries_) {
            return false;
        }
        
        if (find(name) != nullptr) {
            throw std::invalid_argument("Name already exists");
        }
        
        auto* entries = get_entries();
        auto& entry = entries[header->entry_count++];
        
        std::memset(entry.name, 0, sizeof(entry.name));
        std::memcpy(entry.name, name.data(), name.size());
        entry.offset = offset;
        entry.size = size;
        
        return true;
    }
    
    /**
     * Allocate space for a new structure
     * @param size Size in bytes to allocate
     * @param alignment Alignment requirement (default 8)
     * @return Offset of allocated space
     */
    uint32_t allocate(size_t size, size_t alignment = 8) {
        auto* header = get_header();
        
        // Align the offset
        uint32_t aligned = (header->next_offset + alignment - 1) & ~(alignment - 1);
        uint32_t result = aligned;
        
        header->next_offset = aligned + size;
        return result;
    }
    
    /**
     * Get the total size of the table in bytes
     */
    static size_t calculate_size(size_t max_entries) {
        return sizeof(Header) + max_entries * sizeof(Entry);
    }
    
    /**
     * Get the number of entries currently in the table
     */
    size_t entry_count() const {
        return get_header()->entry_count;
    }
    
    /**
     * Get the maximum number of entries this table can hold
     */
    size_t max_entries() const {
        return max_entries_;
    }
    
    /**
     * Get the next allocation offset
     */
    uint32_t next_offset() const {
        return get_header()->next_offset;
    }
    
private:
    void initialize() {
        auto* header = get_header();
        header->magic = TABLE_MAGIC;
        header->version = TABLE_VERSION;
        header->entry_count = 0;
        header->next_offset = calculate_size(max_entries_);
        
        // Zero out entries
        auto* entries = get_entries();
        std::memset(entries, 0, max_entries_ * sizeof(Entry));
    }
    
    void validate() {
        auto* header = get_header();
        
        if (header->magic != TABLE_MAGIC) {
            throw std::runtime_error("Invalid table magic number");
        }
        
        if (header->version != TABLE_VERSION) {
            throw std::runtime_error("Incompatible table version");
        }
        
        if (header->entry_count > max_entries_) {
            throw std::runtime_error("Table corruption: entry count exceeds maximum");
        }
    }
    
    Header* get_header() {
        return reinterpret_cast<Header*>(memory_);
    }
    
    const Header* get_header() const {
        return reinterpret_cast<const Header*>(memory_);
    }
    
    Entry* get_entries() {
        return reinterpret_cast<Entry*>(memory_ + sizeof(Header));
    }
    
    const Entry* get_entries() const {
        return reinterpret_cast<const Entry*>(memory_ + sizeof(Header));
    }
    
    char* memory_;
    size_t max_entries_;
};

} // namespace zeroipc