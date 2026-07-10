#pragma once

#include "memory.h"
#include "detail/hash.h"
#include <atomic>
#include <thread>

namespace zeroipc {

template<typename T>
class Set {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for shared memory");
    static_assert(alignof(T) <= MAX_ELEM_ALIGN,
                  "T alignment exceeds the 8-byte guarantee of shared memory layout");

    struct Entry {
        std::atomic<uint32_t> state;  // 0=empty, 1=occupied, 2=deleted, 3=inserting
        T value;
    };

    struct Header {
        std::atomic<uint32_t> size;       // Current number of elements
        uint32_t capacity;                 // Total capacity
        uint32_t elem_size;
        uint32_t reserved;                 // pads header to 16 bytes so Entry array is 8-aligned
    };
    
    // State values for entries
    static constexpr uint32_t EMPTY = 0;
    static constexpr uint32_t OCCUPIED = 1;
    static constexpr uint32_t DELETED = 2;
    static constexpr uint32_t INSERTING = 3;

    // Bound on waiting for a slot stuck in INSERTING (a crashed peer can
    // leave it that way forever). Matches Stack/Queue/Map MAX_SPINS.
    static constexpr int MAX_SPINS = 10000;
    
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
        
        header_ = memory.ptr_at<Header>(offset);

        // Initialize header
        header_->size.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        header_->reserved = 0;

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
        
        header_ = memory.ptr_at<Header>(offset);

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

        // Two-phase insert. Phase 1 scans the whole probe chain for the
        // value (returning false if present), remembering the first
        // reusable DELETED slot; the chain ends at the first EMPTY slot.
        // Phase 2 claims the remembered slot. Claiming before the chain
        // is fully scanned would duplicate a value that lives past a
        // DELETED slot, and advancing past a slot whose CAS we lost would
        // duplicate a value a concurrent insert is writing to it — both
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
                        // this value. Wait bounded (a crashed peer can
                        // leave the slot stuck forever), then skip it.
                        if (++spins >= MAX_SPINS) break;
                        std::this_thread::yield();
                        continue;
                    }

                    if (state == OCCUPIED) {
                        if (values_equal(entry.value, value)) {
                            return false;  // Already exists
                        }
                        break;  // different value: next slot
                    }

                    if (state == DELETED) {
                        if (!deleted_target) deleted_target = &entry;
                        break;  // keep scanning: the value may live further on
                    }

                    // EMPTY: the probe chain ends here; the value is absent
                    empty_target = &entry;
                    break;
                }
            }

            Entry* target = deleted_target ? deleted_target : empty_target;
            if (!target) break;  // set is full

            uint32_t expected = deleted_target ? DELETED : EMPTY;
            if (target->state.compare_exchange_strong(expected, INSERTING,
                                                      std::memory_order_acquire,
                                                      std::memory_order_relaxed)) {
                // We exclusively own this slot; write the value
                target->value = value;
                // Publish the entry: INSERTING -> OCCUPIED (release so readers see data)
                target->state.store(OCCUPIED, std::memory_order_release);
                header_->size.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            // The slot changed under us — a competing operation completed
            // (possibly inserting this very value). Rescan from the top.
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

            // Continue searching through DELETED and INSERTING entries
            // INSERTING slots are being written by another thread; skip them
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
                // CAS from OCCUPIED to DELETED; only the winner decrements size
                uint32_t expected = OCCUPIED;
                if (entry.state.compare_exchange_strong(expected, DELETED,
                                                       std::memory_order_release,
                                                       std::memory_order_relaxed)) {
                    header_->size.fetch_sub(1, std::memory_order_relaxed);
                    return true;
                }
                // CAS failed: another thread already erased or is modifying this slot
                if (expected == DELETED) {
                    return false;
                }
                // Otherwise (e.g., INSERTING), continue probing
            }

            // Continue searching through DELETED and INSERTING entries
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
    
    size_t hash_value(const T& value) const { return detail::trivial_hash(value); }
    bool values_equal(const T& a, const T& b) const { return detail::trivial_equal(a, b); }
};

} // namespace zeroipc