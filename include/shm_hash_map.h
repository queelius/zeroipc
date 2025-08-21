/**
 * @file shm_hash_map.h
 * @brief Lock-free hash map implementation for POSIX shared memory
 * @author Alex Towell
 * 
 * @details
 * Provides a thread-safe hash map that can be shared across processes.
 * Uses open addressing with linear probing and atomic operations for
 * lock-free access patterns.
 * 
 * @par Thread Safety
 * All operations are thread-safe using atomic compare-and-swap.
 * 
 * @par Example
 * @code
 * posix_shm shm("simulation", 10 * 1024 * 1024);
 * shm_hash_map<uint32_t, Particle> particles(shm, "particle_map", 10000);
 * 
 * // Insert
 * particles.insert(42, particle);
 * 
 * // Find
 * if (auto* p = particles.find(42)) {
 *     process(*p);
 * }
 * @endcode
 */

#pragma once

#include "posix_shm.h"
#include "shm_table.h"
#include "shm_span.h"
#include <atomic>
#include <optional>
#include <functional>
#include <cstring>
#include <concepts>

/**
 * @brief Lock-free hash map for shared memory
 * 
 * @tparam Key Key type (must be trivially copyable)
 * @tparam Value Value type (must be trivially copyable)
 * @tparam Hash Hash function (default std::hash<Key>)
 * @tparam TableType Table implementation for metadata
 * 
 * @details
 * Implements a bounded hash map using open addressing with linear probing.
 * Deletions use tombstones to maintain probe chains.
 * 
 * @par Memory Layout
 * @code
 * [Header]
 *   ├─ size_t bucket_count
 *   ├─ atomic<size_t> size
 *   └─ atomic<uint64_t> version
 * [Bucket][Bucket]...[Bucket]
 * 
 * Bucket:
 *   ├─ atomic<State> state (EMPTY/OCCUPIED/TOMBSTONE)
 *   ├─ Key key
 *   └─ Value value
 * @endcode
 */
template<typename Key, typename Value, 
         typename Hash = std::hash<Key>,
         typename TableType = shm_table>
    requires std::is_trivially_copyable_v<Key> && 
             std::is_trivially_copyable_v<Value>
class shm_hash_map : public shm_span<std::pair<Key, Value>, posix_shm_impl<TableType>> {
private:
    enum State : uint8_t {
        EMPTY = 0,
        OCCUPIED = 1,
        TOMBSTONE = 2
    };

    struct alignas(64) Bucket {
        std::atomic<State> state{EMPTY};
        Key key;
        Value value;
        char padding[64 - sizeof(std::atomic<State>) - sizeof(Key) - sizeof(Value)];
    };

    struct MapHeader {
        size_t bucket_count;
        std::atomic<size_t> size{0};
        std::atomic<uint64_t> version{0};  // For iterator invalidation
        float max_load_factor{0.75f};
    };

    MapHeader* header() {
        return reinterpret_cast<MapHeader*>(
            static_cast<char*>(this->shm.get_base_addr()) + this->offset
        );
    }

    const MapHeader* header() const {
        return reinterpret_cast<const MapHeader*>(
            static_cast<const char*>(this->shm.get_base_addr()) + this->offset
        );
    }

    Bucket* buckets() {
        return reinterpret_cast<Bucket*>(
            reinterpret_cast<char*>(header()) + sizeof(MapHeader)
        );
    }

    const Bucket* buckets() const {
        return reinterpret_cast<const Bucket*>(
            reinterpret_cast<const char*>(header()) + sizeof(MapHeader)
        );
    }

    size_t hash_key(const Key& key) const {
        size_t bucket_count = header()->bucket_count;
        if (bucket_count == 0) {
            return 0;  // Safety check - should not happen in normal operation
        }
        return Hash{}(key) % bucket_count;
    }

    const typename TableType::entry* _table_entry{nullptr};

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;

    /**
     * @brief Check if a hash map exists in shared memory
     */
    template<typename ShmType>
    static bool exists(ShmType& shm, std::string_view name) {
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        return table->find(name_buf) != nullptr;
    }

    /**
     * @brief Create or open a shared memory hash map
     * 
     * @param shm Shared memory segment
     * @param name Unique identifier
     * @param capacity Maximum number of elements (0 to open existing)
     * 
     * @throws std::runtime_error if capacity is 0 and map doesn't exist
     */
    template<typename ShmType>
    shm_hash_map(ShmType& shm, std::string_view name, size_t capacity = 0)
        : shm_span<std::pair<Key, Value>, posix_shm_impl<TableType>>(shm, 0, 0) {
        static_assert(std::is_same_v<typename ShmType::table_type, TableType>,
                      "SharedMemory table type must match hash map table type");
        
        auto* table = static_cast<TableType*>(shm.get_base_addr());
        
        char name_buf[TableType::MAX_NAME_SIZE]{};
        size_t copy_len = std::min(name.size(), sizeof(name_buf) - 1);
        std::copy_n(name.begin(), copy_len, name_buf);
        
        auto* entry = table->find(name_buf);
        
        if (entry) {
            // Open existing hash map
            if (capacity != 0 && entry->num_elem != capacity) {
                throw std::runtime_error("Hash map capacity mismatch");
            }
            this->offset = entry->offset;
            this->num_elem = entry->num_elem;
            _table_entry = entry;
            
            // Validate header is initialized
            auto* hdr = header();
            if (hdr->bucket_count == 0) {
                // Header wasn't properly initialized, fix it
                size_t bucket_count = 1;
                while (bucket_count < entry->num_elem * 1.5) {
                    bucket_count <<= 1;
                }
                hdr->bucket_count = bucket_count;
                hdr->size.store(0, std::memory_order_relaxed);
                hdr->version.store(0, std::memory_order_relaxed);
                hdr->max_load_factor = 0.75f;
            }
        } else if (capacity > 0) {
            // Create new hash map
            // Use next power of 2 for bucket count (better hash distribution)
            size_t bucket_count = 1;
            while (bucket_count < capacity * 1.5) {
                bucket_count <<= 1;
            }
            
            size_t required_size = sizeof(MapHeader) + bucket_count * sizeof(Bucket);
            size_t current_used = table->get_total_allocated_size();
            
            this->offset = sizeof(TableType) + current_used;
            this->num_elem = capacity;
            
            // Initialize header
            auto* hdr = header();
            new (hdr) MapHeader{};
            hdr->bucket_count = bucket_count;
            hdr->size.store(0, std::memory_order_relaxed);
            
            // Initialize buckets
            auto* bkts = buckets();
            for (size_t i = 0; i < bucket_count; ++i) {
                new (&bkts[i]) Bucket{};
            }
            
            // Register in table
            table->add(name_buf, this->offset, required_size, 
                      sizeof(std::pair<Key, Value>), capacity);
            _table_entry = table->find(name_buf);
        } else {
            throw std::runtime_error("Hash map not found and capacity not specified");
        }
    }

    /**
     * @brief Insert a key-value pair
     * 
     * @return true if inserted, false if key already exists or map is full
     */
    [[nodiscard]] bool insert(const Key& key, const Value& value) noexcept {
        auto* hdr = header();
        auto* bkts = buckets();
        
        // Check load factor
        if (hdr->size.load(std::memory_order_acquire) >= 
            hdr->bucket_count * hdr->max_load_factor) {
            return false;  // Would need resize
        }
        
        size_t idx = hash_key(key);
        size_t original_idx = idx;
        
        // Linear probing
        while (true) {
            State expected = EMPTY;
            
            // Try to claim empty slot
            if (bkts[idx].state.compare_exchange_weak(expected, OCCUPIED,
                                                       std::memory_order_acq_rel)) {
                // We got an empty slot
                bkts[idx].key = key;
                bkts[idx].value = value;
                hdr->size.fetch_add(1, std::memory_order_relaxed);
                hdr->version.fetch_add(1, std::memory_order_release);
                return true;
            }
            
            // Check if it's a tombstone we can reuse
            if (expected == TOMBSTONE) {
                if (bkts[idx].state.compare_exchange_weak(expected, OCCUPIED,
                                                          std::memory_order_acq_rel)) {
                    bkts[idx].key = key;
                    bkts[idx].value = value;
                    hdr->size.fetch_add(1, std::memory_order_relaxed);
                    hdr->version.fetch_add(1, std::memory_order_release);
                    return true;
                }
            }
            
            // Check if key already exists
            if (expected == OCCUPIED && bkts[idx].key == key) {
                return false;  // Key already exists
            }
            
            // Move to next bucket
            idx = (idx + 1) % hdr->bucket_count;
            
            // Check if we've searched all buckets
            if (idx == original_idx) {
                return false;  // Map is full
            }
        }
    }

    /**
     * @brief Find a value by key
     * 
     * @return Pointer to value or nullptr if not found
     */
    [[nodiscard]] Value* find(const Key& key) noexcept {
        auto* hdr = header();
        auto* bkts = buckets();
        
        size_t idx = hash_key(key);
        size_t original_idx = idx;
        
        while (true) {
            State state = bkts[idx].state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return nullptr;  // Not found
            }
            
            if (state == OCCUPIED && bkts[idx].key == key) {
                return &bkts[idx].value;  // Found
            }
            
            // Continue searching (skip tombstones)
            idx = (idx + 1) % hdr->bucket_count;
            
            if (idx == original_idx) {
                return nullptr;  // Searched all buckets
            }
        }
    }

    /**
     * @brief Find a value by key (const version)
     */
    [[nodiscard]] const Value* find(const Key& key) const noexcept {
        return const_cast<shm_hash_map*>(this)->find(key);
    }

    /**
     * @brief Update existing value
     * 
     * @return true if updated, false if key not found
     */
    [[nodiscard]] bool update(const Key& key, const Value& value) noexcept {
        if (auto* val = find(key)) {
            *val = value;
            header()->version.fetch_add(1, std::memory_order_release);
            return true;
        }
        return false;
    }

    /**
     * @brief Insert or update
     */
    void insert_or_update(const Key& key, const Value& value) noexcept {
        if (!update(key, value)) {
            insert(key, value);
        }
    }

    /**
     * @brief Remove a key-value pair
     * 
     * @return true if removed, false if not found
     */
    [[nodiscard]] bool erase(const Key& key) noexcept {
        auto* hdr = header();
        auto* bkts = buckets();
        
        size_t idx = hash_key(key);
        size_t original_idx = idx;
        
        while (true) {
            State state = bkts[idx].state.load(std::memory_order_acquire);
            
            if (state == EMPTY) {
                return false;  // Not found
            }
            
            if (state == OCCUPIED && bkts[idx].key == key) {
                // Mark as tombstone
                State expected = OCCUPIED;
                if (bkts[idx].state.compare_exchange_strong(expected, TOMBSTONE,
                                                            std::memory_order_release)) {
                    hdr->size.fetch_sub(1, std::memory_order_relaxed);
                    hdr->version.fetch_add(1, std::memory_order_release);
                    return true;
                }
                // Someone else modified it, retry
                continue;
            }
            
            idx = (idx + 1) % hdr->bucket_count;
            
            if (idx == original_idx) {
                return false;  // Searched all buckets
            }
        }
    }

    /**
     * @brief Check if key exists
     */
    [[nodiscard]] bool contains(const Key& key) const noexcept {
        return find(key) != nullptr;
    }

    /**
     * @brief Get current number of elements
     */
    [[nodiscard]] size_t size() const noexcept {
        return header()->size.load(std::memory_order_acquire);
    }

    /**
     * @brief Check if empty
     */
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /**
     * @brief Get bucket count
     */
    [[nodiscard]] size_t bucket_count() const noexcept {
        return header()->bucket_count;
    }

    /**
     * @brief Get load factor
     */
    [[nodiscard]] float load_factor() const noexcept {
        auto* hdr = header();
        return static_cast<float>(size()) / hdr->bucket_count;
    }

    /**
     * @brief Clear all elements (not thread-safe!)
     */
    void clear() noexcept {
        auto* hdr = header();
        auto* bkts = buckets();
        
        for (size_t i = 0; i < hdr->bucket_count; ++i) {
            bkts[i].state.store(EMPTY, std::memory_order_relaxed);
        }
        
        hdr->size.store(0, std::memory_order_release);
        hdr->version.fetch_add(1, std::memory_order_release);
    }

    /**
     * @brief Apply function to all key-value pairs
     * 
     * @warning May see inconsistent state during concurrent modifications
     */
    template<typename Func>
    void for_each(Func&& func) const {
        auto* hdr = header();
        auto* bkts = buckets();
        
        for (size_t i = 0; i < hdr->bucket_count; ++i) {
            if (bkts[i].state.load(std::memory_order_acquire) == OCCUPIED) {
                func(bkts[i].key, bkts[i].value);
            }
        }
    }

    /**
     * @brief Get name of the hash map
     */
    std::string_view name() const {
        return _table_entry ? std::string_view(_table_entry->name.data()) : std::string_view{};
    }
};

// Type aliases for common hash maps
template<typename K, typename V>
using shm_map = shm_hash_map<K, V>;

using shm_map_int_int = shm_hash_map<int, int>;
using shm_map_uint32_uint32 = shm_hash_map<uint32_t, uint32_t>;
using shm_map_uint64_uint64 = shm_hash_map<uint64_t, uint64_t>;