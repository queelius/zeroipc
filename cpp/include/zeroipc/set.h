#pragma once

#include "memory.h"
#include <atomic>
#include <cstring>
#include <functional>

namespace zeroipc {

template<typename T>
class Set {
public:
    static_assert(std::is_trivially_copyable_v<T>, 
                  "T must be trivially copyable for shared memory");
    
    struct Entry {
        std::atomic<uint32_t> state;  // 0=empty, 1=occupied, 2=deleted
        T value;
    };
    
    struct Header {
        std::atomic<uint32_t> size;       // Current number of elements
        uint32_t capacity;                 // Total capacity
        uint32_t elem_size;
    };
    
    // State values for entries
    static constexpr uint32_t EMPTY = 0;
    static constexpr uint32_t OCCUPIED = 1;
    static constexpr uint32_t DELETED = 2;
    
    // Create new set
    Set(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {
        
        if (capacity == 0) {
            throw std::invalid_argument("Set capacity must be greater than 0");
        }
        
        // Check for overflow
        if (capacity > (SIZE_MAX - sizeof(Header)) / sizeof(Entry)) {
            throw std::overflow_error("Set capacity too large");
        }
        
        size_t total_size = sizeof(Header) + sizeof(Entry) * capacity;
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->size.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        
        entries_ = reinterpret_cast<Entry*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
        
        // Initialize all entries as empty
        for (size_t i = 0; i < capacity; ++i) {
            entries_[i].state.store(EMPTY, std::memory_order_relaxed);
        }
    }
    
    // Open existing set
    Set(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Set not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }
        
        entries_ = reinterpret_cast<Entry*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }
    
    // Insert element (lock-free with linear probing)
    [[nodiscard]] bool insert(const T& value) {
        size_t hash = hash_value(value);
        size_t capacity = header_->capacity;
        
        // Linear probing
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            Entry& entry = entries_[idx];
            
            // Check if already exists
            uint32_t state = entry.state.load(std::memory_order_acquire);
            if (state == OCCUPIED && values_equal(entry.value, value)) {
                return false;  // Already exists
            }
            
            // Try to insert into empty slot
            uint32_t expected = EMPTY;
            if (entry.state.compare_exchange_strong(expected, OCCUPIED,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                entry.value = value;
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // Try deleted slots
            expected = DELETED;
            if (entry.state.compare_exchange_strong(expected, OCCUPIED,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                entry.value = value;
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        return false;  // Set is full
    }
    
    // Check if element exists
    [[nodiscard]] bool contains(const T& value) const {
        size_t hash = hash_value(value);
        size_t capacity = header_->capacity;
        
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            const Entry& entry = entries_[idx];
            
            uint32_t state = entry.state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return false;  // Not found
            }
            
            if (state == OCCUPIED && values_equal(entry.value, value)) {
                return true;
            }
            
            // Continue searching through DELETED entries
        }
        
        return false;
    }
    
    // Remove element
    [[nodiscard]] bool erase(const T& value) {
        size_t hash = hash_value(value);
        size_t capacity = header_->capacity;
        
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            Entry& entry = entries_[idx];
            
            uint32_t state = entry.state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return false;  // Not found
            }
            
            if (state == OCCUPIED && values_equal(entry.value, value)) {
                // Mark as deleted
                entry.state.store(DELETED, std::memory_order_release);
                header_->size.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        return false;
    }
    
    // Get current size
    [[nodiscard]] size_t size() const {
        return header_->size.load(std::memory_order_relaxed);
    }
    
    // Get capacity
    [[nodiscard]] size_t capacity() const {
        return header_->capacity;
    }
    
    // Check if empty
    [[nodiscard]] bool empty() const {
        return size() == 0;
    }
    
    // Clear all elements (not thread-safe with concurrent operations)
    void clear() {
        for (size_t i = 0; i < header_->capacity; ++i) {
            entries_[i].state.store(EMPTY, std::memory_order_relaxed);
        }
        header_->size.store(0, std::memory_order_relaxed);
    }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_ = nullptr;
    Entry* entries_ = nullptr;
    
    size_t hash_value(const T& value) const {
        if constexpr (std::is_integral_v<T>) {
            // For integers, use a simple multiplicative hash
            return static_cast<size_t>(value) * 2654435761U;
        } else {
            // For other types, hash the bytes
            std::hash<std::string_view> hasher;
            std::string_view sv(reinterpret_cast<const char*>(&value), sizeof(T));
            return hasher(sv);
        }
    }
    
    bool values_equal(const T& a, const T& b) const {
        if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
            return a == b;
        } else {
            return std::memcmp(&a, &b, sizeof(T)) == 0;
        }
    }
};

} // namespace zeroipc