#pragma once

#include "memory.h"
#include "detail/hash.h"
#include <atomic>
#include <optional>
#include <thread>

namespace zeroipc {

template<typename K, typename V>
class Map {
public:
    static_assert(std::is_trivially_copyable_v<K>, 
                  "Key type must be trivially copyable for shared memory");
    static_assert(std::is_trivially_copyable_v<V>,
                  "Value type must be trivially copyable for shared memory");
    static_assert(sizeof(V) <= 8,
                  "Value type must be <= 8 bytes for lock-free atomic updates");
    static_assert(alignof(K) <= MAX_ELEM_ALIGN && alignof(V) <= MAX_ELEM_ALIGN,
                  "Key/Value alignment exceeds the 8-byte guarantee of shared memory layout");

    struct Entry {
        std::atomic<uint32_t> state;  // 0=empty, 1=occupied, 2=deleted, 3=inserting
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
    static constexpr uint32_t INSERTING = 3;

    // Bound on waiting for a slot stuck in INSERTING (a crashed peer can
    // leave it that way forever). Matches Stack/Queue MAX_SPINS.
    static constexpr int MAX_SPINS = 10000;
    
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
        
        header_ = memory.ptr_at<Header>(offset);

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
        
        header_ = memory.ptr_at<Header>(offset);

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

        // Two-phase insert. Phase 1 scans the whole probe chain for the
        // key (updating in place if found), remembering the first
        // reusable DELETED slot; the chain ends at the first EMPTY slot.
        // Phase 2 claims the remembered slot. Claiming a slot before the
        // chain is fully scanned would duplicate a key that lives past a
        // DELETED slot, and advancing past a slot whose CAS we lost would
        // duplicate a key a concurrent insert is writing to it — both
        // paths must re-examine, never skip.
        for (;;) {
            Entry* deleted_target = nullptr;  // first reusable slot
            Entry* empty_target = nullptr;    // chain-terminating slot

            for (size_t i = 0; i < capacity && !empty_target; ++i) {
                Entry& entry = entries_[(hash + i) % capacity];

                int spins = 0;
                for (;;) {
                    uint32_t state = entry.state.load(std::memory_order_acquire);

                    if (state == INSERTING) {
                        // Mid-write by another thread; it may be writing
                        // this key. Wait bounded (a crashed peer can leave
                        // the slot stuck forever), then skip the slot.
                        if (++spins >= MAX_SPINS) break;
                        std::this_thread::yield();
                        continue;
                    }

                    if (state == OCCUPIED) {
                        if (!keys_equal(entry.key, key)) break;  // next slot

                        // Update: CAS OCCUPIED -> INSERTING for exclusive access
                        uint32_t expected = OCCUPIED;
                        if (entry.state.compare_exchange_strong(expected, INSERTING,
                                                                std::memory_order_acquire,
                                                                std::memory_order_relaxed)) {
                            entry.value = value;
                            entry.state.store(OCCUPIED, std::memory_order_release);
                            return true;
                        }
                        continue;  // erased or another updater won; re-examine
                    }

                    if (state == DELETED) {
                        if (!deleted_target) deleted_target = &entry;
                        break;  // keep scanning: the key may live further on
                    }

                    // EMPTY: the probe chain ends here; the key is absent
                    empty_target = &entry;
                    break;
                }
            }

            Entry* target = deleted_target ? deleted_target : empty_target;
            if (!target) break;  // map is full

            uint32_t expected = deleted_target ? DELETED : EMPTY;
            if (target->state.compare_exchange_strong(expected, INSERTING,
                                                      std::memory_order_acquire,
                                                      std::memory_order_relaxed)) {
                // We exclusively own this slot; write key and value
                target->key = key;
                target->value = value;
                // Publish the entry: INSERTING -> OCCUPIED (release so readers see data)
                target->state.store(OCCUPIED, std::memory_order_release);
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            // The slot changed under us — a competing operation completed
            // (possibly inserting this very key). Rescan from the top.
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

            int spins = 0;
            for (;;) {
                uint32_t state = entry.state.load(std::memory_order_acquire);

                if (state == EMPTY) {
                    return std::nullopt;  // chain ends; key not found
                }

                if (state == INSERTING) {
                    // Mid-write; an in-place update of THIS key also holds
                    // INSERTING, so skipping would spuriously miss an
                    // existing key. Wait bounded (crashed peers), then skip.
                    if (++spins >= MAX_SPINS) break;
                    std::this_thread::yield();
                    continue;
                }

                if (state == OCCUPIED && keys_equal(entry.key, key)) {
                    return entry.value;
                }

                break;  // DELETED or different key: next slot
            }
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

            int spins = 0;
            for (;;) {
                uint32_t state = entry.state.load(std::memory_order_acquire);

                if (state == EMPTY) {
                    return false;  // chain ends; key not found
                }

                if (state == INSERTING) {
                    // Mid-write; an in-place update of THIS key also holds
                    // INSERTING, so skipping would spuriously miss an
                    // existing key. Wait bounded (crashed peers), then skip.
                    if (++spins >= MAX_SPINS) break;
                    std::this_thread::yield();
                    continue;
                }

                if (state == OCCUPIED && keys_equal(entry.key, key)) {
                    // CAS from OCCUPIED to DELETED; only the winner decrements size
                    uint32_t expected = OCCUPIED;
                    if (entry.state.compare_exchange_strong(expected, DELETED,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed)) {
                        header_->size.fetch_sub(1, std::memory_order_relaxed);
                        return true;
                    }
                    // Lost the CAS: an eraser won (slot now DELETED, next
                    // iteration falls through and probing ends at EMPTY) or
                    // an updater holds INSERTING (wait and erase the
                    // updated entry). Re-examine this slot either way.
                    continue;
                }

                break;  // DELETED or different key: next slot
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
    
    size_t hash_key(const K& key) const { return detail::trivial_hash(key); }
    bool keys_equal(const K& a, const K& b) const { return detail::trivial_equal(a, b); }
};

} // namespace zeroipc