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

    struct Header {
        std::atomic<int32_t> top;  // -1 when empty
        uint32_t capacity;
        uint32_t elem_size;
    };

    // Per-slot states for the 4-state CAS protocol:
    //   EMPTY(0)  -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)
    // Push: CAS(EMPTY -> WRITING), write data, store(READY)
    // Pop:  CAS(READY -> READING), read data, store(EMPTY)
    static constexpr uint32_t SLOT_EMPTY   = 0;
    static constexpr uint32_t SLOT_WRITING = 1;
    static constexpr uint32_t SLOT_READY   = 2;
    static constexpr uint32_t SLOT_READING = 3;

    // Create new stack
    Stack(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {

        // Layout: [Header][data: T * capacity][state: atomic<uint32_t> * capacity]
        size_t total_size = sizeof(Header)
                          + sizeof(T) * capacity
                          + sizeof(std::atomic<uint32_t>) * capacity;
        size_t offset = memory.allocate(name, total_size);

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);

        // Initialize header
        header_->top.store(-1, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        state_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + sizeof(T) * capacity);

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

        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);

        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        state_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + sizeof(T) * header_->capacity);
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

        // Step 2: Exclusively claim the slot for writing: CAS(EMPTY -> WRITING)
        // If another push/pop is still using this slot, spin until EMPTY.
        uint32_t expected = SLOT_EMPTY;
        while (!state_[new_top].compare_exchange_weak(
                    expected, SLOT_WRITING,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
            expected = SLOT_EMPTY;
            std::this_thread::yield();
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

        // Step 2: Exclusively claim the slot for reading: CAS(READY -> READING)
        // If the push hasn't finished writing yet, spin until READY.
        uint32_t expected = SLOT_READY;
        while (!state_[current_top].compare_exchange_weak(
                    expected, SLOT_READING,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
            expected = SLOT_READY;
            std::this_thread::yield();
        }

        // Step 3: Read the value (we have exclusive ownership)
        T value = data_[current_top];

        // Step 4: Release slot: READING -> EMPTY
        state_[current_top].store(SLOT_EMPTY, std::memory_order_release);

        return value;
    }

    // Peek at top without removing
    std::optional<T> top() const {
        int32_t current_top = header_->top.load(std::memory_order_acquire);
        if (current_top < 0) {
            return std::nullopt;
        }
        // Wait for the slot to have data
        while (state_[current_top].load(std::memory_order_acquire) != SLOT_READY) {
            std::this_thread::yield();
        }
        return data_[current_top];
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
