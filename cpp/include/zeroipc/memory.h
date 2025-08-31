#pragma once

#include <zeroipc/table.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <stdexcept>
#include <memory>

namespace zeroipc {

/**
 * POSIX shared memory wrapper with automatic cleanup and table management.
 * 
 * This class manages a shared memory segment and its metadata table.
 * The table is always placed at the beginning of the shared memory.
 */
class Memory {
public:
    /**
     * Create or open shared memory
     * @param name Shared memory name (e.g., "/myshm")
     * @param size Size in bytes (0 to open existing)
     * @param max_entries Maximum number of table entries (default 64)
     */
    Memory(const std::string& name, size_t size = 0, size_t max_entries = 64)
        : name_(name)
        , size_(size)
        , max_entries_(max_entries)
        , fd_(-1)
        , memory_(nullptr)
        , table_(nullptr)
        , owner_(size > 0) {
        
        if (size > 0) {
            create();
        } else {
            open();
        }
        
        // Initialize table
        size_t table_size = Table::calculate_size(max_entries_);
        table_ = std::make_unique<Table>(memory_, max_entries_, owner_);
    }
    
    ~Memory() {
        if (memory_ && size_ > 0) {
            munmap(memory_, size_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }
    
    // Disable copy
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    
    // Enable move
    Memory(Memory&& other) noexcept
        : name_(std::move(other.name_))
        , size_(other.size_)
        , max_entries_(other.max_entries_)
        , fd_(other.fd_)
        , memory_(other.memory_)
        , table_(std::move(other.table_))
        , owner_(other.owner_) {
        other.fd_ = -1;
        other.memory_ = nullptr;
        other.size_ = 0;
    }
    
    Memory& operator=(Memory&& other) noexcept {
        if (this != &other) {
            // Clean up current resources
            if (memory_ && size_ > 0) {
                munmap(memory_, size_);
            }
            if (fd_ >= 0) {
                close(fd_);
            }
            
            // Move resources
            name_ = std::move(other.name_);
            size_ = other.size_;
            max_entries_ = other.max_entries_;
            fd_ = other.fd_;
            memory_ = other.memory_;
            table_ = std::move(other.table_);
            owner_ = other.owner_;
            
            // Clear other
            other.fd_ = -1;
            other.memory_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    /**
     * Unlink (delete) the shared memory
     */
    void unlink() {
        shm_unlink(name_.c_str());
    }
    
    /**
     * Static method to unlink shared memory by name
     */
    static void unlink(const std::string& name) {
        shm_unlink(name.c_str());
    }
    
    /**
     * Get pointer to the shared memory
     */
    void* data() { return memory_; }
    const void* data() const { return memory_; }
    
    /**
     * Get base pointer (alias for data())
     */
    void* base() { return memory_; }
    const void* base() const { return memory_; }
    
    /**
     * Get pointer to memory at specific offset
     */
    void* at(size_t offset) {
        if (offset >= size_) {
            throw std::out_of_range("Offset out of bounds");
        }
        return static_cast<char*>(memory_) + offset;
    }
    
    const void* at(size_t offset) const {
        if (offset >= size_) {
            throw std::out_of_range("Offset out of bounds");
        }
        return static_cast<const char*>(memory_) + offset;
    }
    
    /**
     * Allocate space in shared memory
     * @param name Name for the table entry
     * @param size Size to allocate
     * @return Offset of allocated space
     */
    size_t allocate(std::string_view name, size_t size) {
        // First allocate the space
        uint32_t offset = table_->allocate(size);
        
        // Then add to table
        if (!table_->add(name, offset, size)) {
            throw std::runtime_error("Failed to add entry to table");
        }
        
        return offset;
    }
    
    /**
     * Find an entry in the table
     * @param name Name to find
     * @param offset Output: offset of the entry
     * @param size Output: size of the entry
     * @return true if found, false otherwise
     */
    bool find(std::string_view name, size_t& offset, size_t& size) {
        auto entry = table_->find(name);
        if (entry) {
            offset = entry->offset;
            size = entry->size;
            return true;
        }
        return false;
    }
    
    /**
     * Get the table
     */
    Table* table() { return table_.get(); }
    const Table* table() const { return table_.get(); }
    
    /**
     * Get the size of the shared memory
     */
    size_t size() const { return size_; }
    
    /**
     * Get the name of the shared memory
     */
    const std::string& name() const { return name_; }
    
    /**
     * Check if this instance created the shared memory
     */
    bool is_owner() const { return owner_; }
    
private:
    void create() {
        // Create shared memory
        fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (fd_ < 0) {
            if (errno == EEXIST) {
                // Try to unlink and recreate
                shm_unlink(name_.c_str());
                fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
            }
            
            if (fd_ < 0) {
                throw std::runtime_error("Failed to create shared memory: " + 
                                       std::string(strerror(errno)));
            }
        }
        
        // Set size
        if (ftruncate(fd_, size_) < 0) {
            close(fd_);
            shm_unlink(name_.c_str());
            throw std::runtime_error("Failed to set shared memory size: " + 
                                   std::string(strerror(errno)));
        }
        
        // Map memory
        memory_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (memory_ == MAP_FAILED) {
            close(fd_);
            shm_unlink(name_.c_str());
            throw std::runtime_error("Failed to map shared memory: " + 
                                   std::string(strerror(errno)));
        }
        
        // Zero out the memory
        std::memset(memory_, 0, size_);
    }
    
    void open() {
        // Open existing shared memory
        fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open shared memory: " + 
                                   std::string(strerror(errno)));
        }
        
        // Get size
        struct stat st;
        if (fstat(fd_, &st) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to get shared memory info: " + 
                                   std::string(strerror(errno)));
        }
        size_ = st.st_size;
        
        // Map memory
        memory_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (memory_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to map shared memory: " + 
                                   std::string(strerror(errno)));
        }
    }
    
    std::string name_;
    size_t size_;
    size_t max_entries_;
    int fd_;
    void* memory_;
    std::unique_ptr<Table> table_;
    bool owner_;
};

} // namespace zeroipc