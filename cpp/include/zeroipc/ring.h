#pragma once

#include "memory.h"
#include <atomic>
#include <optional>
#include <cstring>

namespace zeroipc {

template<typename T>
class Ring {
public:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    struct Header {
        std::atomic<uint64_t> write_pos;   // Total bytes written
        std::atomic<uint64_t> read_pos;    // Total bytes read
        uint32_t capacity;                  // Ring buffer capacity in bytes
        uint32_t elem_size;
    };
    
    // Create new ring buffer
    Ring(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {
        
        if (capacity == 0) {
            throw std::invalid_argument("Ring capacity must be greater than 0");
        }
        
        // Ensure capacity is a multiple of element size for alignment
        capacity = (capacity / sizeof(T)) * sizeof(T);
        if (capacity == 0) {
            capacity = sizeof(T);
        }
        
        // Check for overflow
        if (capacity > SIZE_MAX - sizeof(Header)) {
            throw std::overflow_error("Ring capacity too large");
        }
        
        size_t total_size = sizeof(Header) + capacity;
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->write_pos.store(0, std::memory_order_relaxed);
        header_->read_pos.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        
        buffer_ = reinterpret_cast<char*>(header_) + sizeof(Header);
    }
    
    // Open existing ring buffer
    Ring(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Ring not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }
        
        buffer_ = reinterpret_cast<char*>(header_) + sizeof(Header);
    }
    
    // Write data to ring buffer (lock-free SPSC optimized)
    [[nodiscard]] bool write(const T& value) {
        uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
        uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);
        
        // Check if there's space
        if (write_pos - read_pos + sizeof(T) > header_->capacity) {
            return false;  // Buffer full
        }
        
        // Write the data
        size_t offset = write_pos % header_->capacity;
        
        if (offset + sizeof(T) <= header_->capacity) {
            // Normal case - contiguous write
            std::memcpy(buffer_ + offset, &value, sizeof(T));
        } else {
            // Wrap around case - split write
            size_t first_part = header_->capacity - offset;
            std::memcpy(buffer_ + offset, &value, first_part);
            std::memcpy(buffer_, reinterpret_cast<const char*>(&value) + first_part, 
                       sizeof(T) - first_part);
        }
        
        // Update write position
        header_->write_pos.store(write_pos + sizeof(T), std::memory_order_release);
        return true;
    }
    
    // Read data from ring buffer (lock-free SPSC optimized)
    [[nodiscard]] std::optional<T> read() {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        
        // Check if there's data
        if (read_pos + sizeof(T) > write_pos) {
            return std::nullopt;  // Buffer empty
        }
        
        // Read the data
        T value;
        size_t offset = read_pos % header_->capacity;
        
        if (offset + sizeof(T) <= header_->capacity) {
            // Normal case - contiguous read
            std::memcpy(&value, buffer_ + offset, sizeof(T));
        } else {
            // Wrap around case - split read
            size_t first_part = header_->capacity - offset;
            std::memcpy(&value, buffer_ + offset, first_part);
            std::memcpy(reinterpret_cast<char*>(&value) + first_part, buffer_, 
                       sizeof(T) - first_part);
        }
        
        // Update read position
        header_->read_pos.store(read_pos + sizeof(T), std::memory_order_release);
        return value;
    }
    
    // Write multiple elements (more efficient for bulk operations)
    [[nodiscard]] size_t write_bulk(const T* data, size_t count) {
        uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
        uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);
        
        // Calculate how many we can write
        uint64_t available = header_->capacity - (write_pos - read_pos);
        size_t to_write = std::min(count, available / sizeof(T));
        
        if (to_write == 0) return 0;
        
        size_t bytes_to_write = to_write * sizeof(T);
        size_t offset = write_pos % header_->capacity;
        
        if (offset + bytes_to_write <= header_->capacity) {
            // Normal case - contiguous write
            std::memcpy(buffer_ + offset, data, bytes_to_write);
        } else {
            // Wrap around case - split write
            size_t first_part = header_->capacity - offset;
            std::memcpy(buffer_ + offset, data, first_part);
            std::memcpy(buffer_, reinterpret_cast<const char*>(data) + first_part, 
                       bytes_to_write - first_part);
        }
        
        // Update write position
        header_->write_pos.store(write_pos + bytes_to_write, std::memory_order_release);
        return to_write;
    }
    
    // Read multiple elements
    [[nodiscard]] size_t read_bulk(T* data, size_t count) {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        
        // Calculate how many we can read
        uint64_t available = write_pos - read_pos;
        size_t to_read = std::min(count, available / sizeof(T));
        
        if (to_read == 0) return 0;
        
        size_t bytes_to_read = to_read * sizeof(T);
        size_t offset = read_pos % header_->capacity;
        
        if (offset + bytes_to_read <= header_->capacity) {
            // Normal case - contiguous read
            std::memcpy(data, buffer_ + offset, bytes_to_read);
        } else {
            // Wrap around case - split read
            size_t first_part = header_->capacity - offset;
            std::memcpy(data, buffer_ + offset, first_part);
            std::memcpy(reinterpret_cast<char*>(data) + first_part, buffer_, 
                       bytes_to_read - first_part);
        }
        
        // Update read position
        header_->read_pos.store(read_pos + bytes_to_read, std::memory_order_release);
        return to_read;
    }
    
    // Get number of elements available to read
    [[nodiscard]] size_t available() const {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        return (write_pos - read_pos) / sizeof(T);
    }
    
    // Get free space in elements
    [[nodiscard]] size_t free_space() const {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
        return (header_->capacity - (write_pos - read_pos)) / sizeof(T);
    }
    
    // Get capacity in elements
    [[nodiscard]] size_t capacity() const {
        return header_->capacity / sizeof(T);
    }
    
    // Check if empty
    [[nodiscard]] bool empty() const {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_relaxed);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_acquire);
        return read_pos == write_pos;
    }
    
    // Check if full
    [[nodiscard]] bool full() const {
        uint64_t read_pos = header_->read_pos.load(std::memory_order_acquire);
        uint64_t write_pos = header_->write_pos.load(std::memory_order_relaxed);
        return (write_pos - read_pos) >= header_->capacity;
    }
    
    // Reset the ring buffer (not thread-safe)
    void reset() {
        header_->write_pos.store(0, std::memory_order_relaxed);
        header_->read_pos.store(0, std::memory_order_relaxed);
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    char* buffer_ = nullptr;
};

} // namespace zeroipc