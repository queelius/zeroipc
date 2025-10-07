#pragma once

#include "memory.h"
#include <atomic>
#include <cstring>
#include <optional>
#include <functional>

namespace zeroipc {

template<typename K, typename V>
class Map {
public:
    static_assert(std::is_trivially_copyable_v<K>, 
                  "Key type must be trivially copyable for shared memory");
    static_assert(std::is_trivially_copyable_v<V>, 
                  "Value type must be trivially copyable for shared memory");
    
    struct Entry {
        std::atomic<uint32_t> state;  // 0=empty, 1=occupied, 2=deleted
        K key;
        V value;
    };
    
    struct Header {
        std::atomic<uint32_t> size;       // Current number of elements
        uint32_t capacity;                 // Total capacity
        uint32_t key_size;
        uint32_t value_size;
    };
    
    // State values for entries
    static constexpr uint32_t EMPTY = 0;
    static constexpr uint32_t OCCUPIED = 1;
    static constexpr uint32_t DELETED = 2;
    
    // Create new map
    Map(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {
        
        if (capacity == 0) {
            throw std::invalid_argument("Map capacity must be greater than 0");
        }
        
        // Check for overflow
        if (capacity > (SIZE_MAX - sizeof(Header)) / sizeof(Entry)) {
            throw std::overflow_error("Map capacity too large");
        }
        
        size_t total_size = sizeof(Header) + sizeof(Entry) * capacity;
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->size.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->key_size = sizeof(K);
        header_->value_size = sizeof(V);
        
        entries_ = reinterpret_cast<Entry*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
        
        // Initialize all entries as empty
        for (size_t i = 0; i < capacity; ++i) {
            entries_[i].state.store(EMPTY, std::memory_order_relaxed);
        }
    }
    
    // Open existing map
    Map(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Map not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        if (header_->key_size != sizeof(K) || header_->value_size != sizeof(V)) {
            throw std::runtime_error("Type size mismatch");
        }
        
        entries_ = reinterpret_cast<Entry*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }
    
    // Insert or update (lock-free with linear probing)
    [[nodiscard]] bool insert(const K& key, const V& value) {
        size_t hash = hash_key(key);
        size_t capacity = header_->capacity;
        
        // Linear probing
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            Entry& entry = entries_[idx];
            
            uint32_t expected = EMPTY;
            if (entry.state.compare_exchange_strong(expected, OCCUPIED,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                // We acquired an empty slot
                entry.key = key;
                entry.value = value;
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            
            // Check if it's our key (update case)
            if (expected == OCCUPIED) {
                if (keys_equal(entry.key, key)) {
                    entry.value = value;  // Update value
                    return true;
                }
            }
            
            // Try deleted slots too
            expected = DELETED;
            if (entry.state.compare_exchange_strong(expected, OCCUPIED,
                                                   std::memory_order_acquire,
                                                   std::memory_order_relaxed)) {
                entry.key = key;
                entry.value = value;
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        return false;  // Map is full
    }
    
    // Find value by key
    [[nodiscard]] std::optional<V> find(const K& key) const {
        size_t hash = hash_key(key);
        size_t capacity = header_->capacity;
        
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            const Entry& entry = entries_[idx];
            
            uint32_t state = entry.state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return std::nullopt;  // Key not found
            }
            
            if (state == OCCUPIED && keys_equal(entry.key, key)) {
                return entry.value;
            }
            
            // Continue searching through DELETED entries
        }
        
        return std::nullopt;
    }
    
    // Remove key (mark as deleted)
    [[nodiscard]] bool erase(const K& key) {
        size_t hash = hash_key(key);
        size_t capacity = header_->capacity;
        
        for (size_t i = 0; i < capacity; ++i) {
            size_t idx = (hash + i) % capacity;
            Entry& entry = entries_[idx];
            
            uint32_t state = entry.state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return false;  // Key not found
            }
            
            if (state == OCCUPIED && keys_equal(entry.key, key)) {
                // Mark as deleted
                entry.state.store(DELETED, std::memory_order_release);
                header_->size.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        return false;
    }
    
    // Check if key exists
    [[nodiscard]] bool contains(const K& key) const {
        return find(key).has_value();
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
    
    // Clear all entries (not thread-safe with concurrent operations)
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
    
    size_t hash_key(const K& key) const {
        if constexpr (std::is_integral_v<K>) {
            // For integers, use a simple multiplicative hash
            return static_cast<size_t>(key) * 2654435761U;
        } else {
            // For other types, hash the bytes
            std::hash<std::string_view> hasher;
            std::string_view sv(reinterpret_cast<const char*>(&key), sizeof(K));
            return hasher(sv);
        }
    }
    
    bool keys_equal(const K& a, const K& b) const {
        if constexpr (std::is_integral_v<K> || std::is_floating_point_v<K>) {
            return a == b;
        } else {
            return std::memcmp(&a, &b, sizeof(K)) == 0;
        }
    }
};

} // namespace zeroipc