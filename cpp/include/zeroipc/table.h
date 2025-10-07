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
        uint32_t reserved;      // Padding/reserved for future use
        uint64_t memory_size;   // Total size of the shared memory segment (supports >4GB)
        uint64_t next_offset;   // Next allocation offset (supports >4GB)
    };
    
    struct Entry {
        char name[32];
        uint64_t offset;        // Supports offsets >4GB
        uint64_t size;          // Supports sizes >4GB
    };
    
    /**
     * Initialize a table in existing memory
     * @param memory Pointer to the start of shared memory
     * @param max_entries Maximum number of entries this table can hold
     * @param memory_size Total size of the shared memory segment
     * @param create If true, initialize a new table; if false, open existing
     */
    Table(void* memory, size_t max_entries, size_t memory_size, bool create = false)
        : memory_(static_cast<char*>(memory))
        , max_entries_(max_entries)
        , memory_size_(memory_size) {
        
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
    [[nodiscard]] bool add(std::string_view name, uint64_t offset, uint64_t size) {
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
    uint64_t allocate(size_t size, size_t alignment = 8) {
        auto* header = get_header();
        
        // Align the offset
        uint64_t aligned = (header->next_offset + alignment - 1) & ~(alignment - 1);
        uint64_t result = aligned;
        
        // Check if allocation would exceed memory bounds
        if (aligned + size < aligned) {  // Check for overflow
            throw std::runtime_error("Allocation size overflow");
        }
        
        if (aligned + size > memory_size_) {  // Check against total memory size
            throw std::runtime_error("Allocation would exceed memory bounds");
        }
        
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
    uint64_t next_offset() const {
        return get_header()->next_offset;
    }
    
private:
    void initialize() {
        auto* header = get_header();
        header->magic = TABLE_MAGIC;
        header->version = TABLE_VERSION;
        header->entry_count = 0;
        header->reserved = 0;
        header->memory_size = memory_size_;
        header->next_offset = calculate_size(max_entries_);  // Already aligned due to struct sizes
        
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
        
        // Use the stored memory size when opening existing table
        memory_size_ = header->memory_size;
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
    size_t memory_size_;
};

} // namespace zeroipc