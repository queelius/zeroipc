#pragma once

#include "memory.h"
#include <atomic>
#include <optional>
#include <thread>

namespace zeroipc {

template<typename T>
class Stack {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for shared memory");
    static_assert(alignof(T) <= MAX_ELEM_ALIGN,
                  "T alignment exceeds the 8-byte guarantee of shared memory layout");

    struct Header {
        std::atomic<int32_t> top;  // -1 when empty
        uint32_t capacity;
        uint32_t elem_size;
        uint32_t reserved;  // pads header to 16 bytes so the data array is 8-aligned
    };

    // Per-slot states for the 4-state CAS protocol:
    //   EMPTY(0)  -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)
    // Push: CAS(EMPTY -> WRITING), write data, store(READY)
    // Pop:  CAS(READY -> READING), read data, store(EMPTY)
    static constexpr uint32_t SLOT_EMPTY   = 0;
    static constexpr uint32_t SLOT_WRITING = 1;
    static constexpr uint32_t SLOT_READY   = 2;
    static constexpr uint32_t SLOT_READING = 3;

    // Bound on every slot-state spin loop. A peer that crashes mid-operation
    // leaves its slot permanently claimed; an unbounded spin would then hang
    // every later operation that lands on that slot. Bailing out makes
    // push/pop/top best-effort: they may fail spuriously if a peer died (or
    // under pathological contention), but they never hang. On bail-out the
    // operation undoes its top reservation when possible and returns failure.
    static constexpr int MAX_SPINS = 10000;

    // Create new stack
    Stack(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {

        // Layout: [Header(16)][data: T*capacity][pad][state: atomic<uint32_t>*capacity]
        // The state array is 8-aligned so its atomics are always naturally aligned.
        size_t state_off = align_up(sizeof(T) * capacity, 8);
        size_t total_size = sizeof(Header)
                          + state_off
                          + sizeof(std::atomic<uint32_t>) * capacity;
        size_t offset = memory.allocate(name, total_size);

        header_ = memory.ptr_at<Header>(offset);

        // Initialize header
        header_->top.store(-1, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        header_->reserved = 0;

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        state_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + state_off);

        // Initialize all slot states to EMPTY
        for (size_t i = 0; i < capacity; ++i) {
            state_[i].store(SLOT_EMPTY, std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_release);
    }

    // Open existing stack
    Stack(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Stack not found: " + std::string(name));
        }

        header_ = memory.ptr_at<Header>(offset);

        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        state_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + align_up(sizeof(T) * header_->capacity, 8));
    }

    // Push (lock-free with per-slot CAS)
    bool push(const T& value) {
        int32_t current_top, new_top;

        // Step 1: Reserve a slot atomically by CAS-advancing top
        do {
            current_top = header_->top.load(std::memory_order_relaxed);

            // Check if full
            if (current_top >= static_cast<int32_t>(header_->capacity - 1)) {
                return false;
            }

            new_top = current_top + 1;
        } while (!header_->top.compare_exchange_weak(
                    current_top, new_top,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed));

        // Step 2: Exclusively claim the slot for writing: CAS(EMPTY -> WRITING).
        // Bounded spin (see MAX_SPINS): a crashed peer can leave the slot
        // permanently claimed.
        uint32_t expected = SLOT_EMPTY;
        bool claimed = false;
        for (int spins = 0; spins < MAX_SPINS; ++spins) {
            if (state_[new_top].compare_exchange_weak(
                    expected, SLOT_WRITING,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                claimed = true;
                break;
            }
            expected = SLOT_EMPTY;
            std::this_thread::yield();
        }
        if (!claimed) {
            // Best-effort undo of the reservation. If another push already
            // built on top of ours, the CAS fails and top stays advanced
            // over the stuck slot; operations landing on that slot also
            // fail bounded rather than hang.
            int32_t reserved = new_top;
            header_->top.compare_exchange_strong(
                reserved, current_top,
                std::memory_order_acq_rel, std::memory_order_relaxed);
            return false;
        }

        // Step 3: Write data (we have exclusive ownership)
        data_[new_top] = value;

        // Step 4: Publish data: WRITING -> READY
        state_[new_top].store(SLOT_READY, std::memory_order_release);

        return true;
    }

    // Pop (lock-free with per-slot CAS)
    std::optional<T> pop() {
        int32_t current_top, new_top;

        // Step 1: Reserve a slot to read by CAS-decrementing top
        do {
            current_top = header_->top.load(std::memory_order_relaxed);

            // Check if empty
            if (current_top < 0) {
                return std::nullopt;
            }

            new_top = current_top - 1;
        } while (!header_->top.compare_exchange_weak(
                    current_top, new_top,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed));

        // Step 2: Exclusively claim the slot for reading: CAS(READY -> READING).
        // Bounded spin (see MAX_SPINS): normally this only waits for the
        // owning push to finish writing, but a pusher that crashed mid-write
        // would leave the slot stuck in WRITING forever.
        uint32_t expected = SLOT_READY;
        bool claimed = false;
        for (int spins = 0; spins < MAX_SPINS; ++spins) {
            if (state_[current_top].compare_exchange_weak(
                    expected, SLOT_READING,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                claimed = true;
                break;
            }
            expected = SLOT_READY;
            std::this_thread::yield();
        }
        if (!claimed) {
            // Best-effort undo: put the item back under top so it is not
            // silently dropped. If another operation moved top meanwhile,
            // the CAS fails and the stuck slot stays orphaned.
            int32_t reserved = new_top;
            header_->top.compare_exchange_strong(
                reserved, current_top,
                std::memory_order_acq_rel, std::memory_order_relaxed);
            return std::nullopt;
        }

        // Step 3: Read the value (we have exclusive ownership)
        T value = data_[current_top];

        // Step 4: Release slot: READING -> EMPTY
        state_[current_top].store(SLOT_EMPTY, std::memory_order_release);

        return value;
    }

    // Peek at top without removing.
    //
    // A peek cannot passively read the slot: between observing READY and
    // reading the payload, a concurrent pop could recycle the slot and a new
    // push begin overwriting it, racing the read (a TOCTOU data race). So we
    // claim the slot exclusively through the same state machine pop uses
    // (READY -> READING), copy the value, then restore it to READY. The copy
    // therefore happens under exclusive ownership and never races push/pop.
    // The bounded spin preserves crash-safety: a peer that died mid-write
    // (slot stuck WRITING/READING) cannot hang the peek indefinitely.
    std::optional<T> top() const {
        for (int spins = 0; spins < MAX_SPINS; ++spins) {
            int32_t current_top = header_->top.load(std::memory_order_acquire);
            if (current_top < 0) {
                return std::nullopt;
            }
            // Try to claim the top slot exclusively for reading.
            uint32_t expected = SLOT_READY;
            if (state_[current_top].compare_exchange_strong(
                    expected, SLOT_READING,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                T value = data_[current_top];          // exclusive ownership
                state_[current_top].store(SLOT_READY,  // hand the slot back
                                          std::memory_order_release);
                return value;
            }
            // Slot busy (push writing, pop reading, or already consumed):
            // retry until it settles or the top moves on.
            std::this_thread::yield();
        }
        return std::nullopt;
    }

    // Check if empty
    bool empty() const {
        return header_->top.load(std::memory_order_acquire) < 0;
    }

    // Check if full
    bool full() const {
        return header_->top.load(std::memory_order_acquire) >=
               static_cast<int32_t>(header_->capacity - 1);
    }

    // Get current size
    size_t size() const {
        int32_t top = header_->top.load(std::memory_order_acquire);
        return top < 0 ? 0 : static_cast<size_t>(top + 1);
    }

    size_t capacity() const { return header_->capacity; }

private:
    Memory& memory_;
    std::string name_;
    Header* header_;
    T* data_;
    std::atomic<uint32_t>* state_;
};

} // namespace zeroipc
