#pragma once

#include "memory.h"
#include <atomic>
#include <optional>

namespace zeroipc {

template<typename T>
class Queue {
public:
    static_assert(std::is_trivially_copyable_v<T>,
                  "T must be trivially copyable for shared memory");
    static_assert(alignof(T) <= MAX_ELEM_ALIGN,
                  "T alignment exceeds the 8-byte guarantee of shared memory layout");

    struct Header {
        std::atomic<uint32_t> head;
        std::atomic<uint32_t> tail;
        uint32_t capacity;
        uint32_t elem_size;
    };

    // Create new queue
    Queue(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {

        if (capacity == 0) {
            throw std::invalid_argument("Queue capacity must be greater than 0");
        }

        // Check for overflow
        size_t seq_array_size = sizeof(std::atomic<uint32_t>) * capacity;
        if (capacity > (SIZE_MAX - sizeof(Header) - seq_array_size) / sizeof(T)) {
            throw std::overflow_error("Queue capacity too large");
        }

        // The sequence array is 8-aligned so its atomics are naturally aligned.
        size_t seq_off = align_up(sizeof(T) * capacity, 8);
        size_t total_size = sizeof(Header) + seq_off + seq_array_size;
        size_t offset = memory.allocate(name, total_size);

        header_ = memory.ptr_at<Header>(offset);

        // Initialize header
        header_->head.store(0, std::memory_order_relaxed);
        header_->tail.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        // Sequence array lives after the data array (8-aligned)
        sequence_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + seq_off);

        // Initialize per-slot sequence numbers: sequence[i] = i
        for (size_t i = 0; i < capacity; i++) {
            sequence_[i].store(static_cast<uint32_t>(i), std::memory_order_relaxed);
        }
    }

    // Open existing queue
    Queue(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {

        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Queue not found: " + std::string(name));
        }

        header_ = memory.ptr_at<Header>(offset);

        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }

        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));

        // Sequence array lives after the data array (8-aligned)
        sequence_ = reinterpret_cast<std::atomic<uint32_t>*>(
            reinterpret_cast<char*>(data_) + align_up(sizeof(T) * header_->capacity, 8));
    }

    // Enqueue (lock-free MPMC, Vyukov-style bounded queue)
    [[nodiscard]] bool push(const T& value) {
        const uint32_t cap = header_->capacity;

        for (;;) {
            uint32_t tail = header_->tail.load(std::memory_order_relaxed);
            uint32_t slot = tail % cap;
            uint32_t seq = sequence_[slot].load(std::memory_order_acquire);
            auto diff = static_cast<int32_t>(seq) - static_cast<int32_t>(tail);

            if (diff == 0) {
                // Slot is ready for writing; try to claim it
                if (header_->tail.compare_exchange_weak(
                        tail, tail + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    // We own this slot - write the data
                    data_[slot] = value;
                    // Publish: set sequence to tail + 1 so consumers can see it
                    sequence_[slot].store(tail + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed, another producer got it; retry
            } else if (diff < 0) {
                // Queue is full
                return false;
            }
            // diff > 0: another producer claimed this slot but hasn't
            // finished yet, or we read a stale tail; retry
        }
    }

    // Dequeue (lock-free MPMC, Vyukov-style bounded queue)
    [[nodiscard]] std::optional<T> pop() {
        const uint32_t cap = header_->capacity;

        for (;;) {
            uint32_t head = header_->head.load(std::memory_order_relaxed);
            uint32_t slot = head % cap;
            uint32_t seq = sequence_[slot].load(std::memory_order_acquire);
            auto diff = static_cast<int32_t>(seq) - static_cast<int32_t>(head + 1);

            if (diff == 0) {
                // Slot contains data; try to claim it
                if (header_->head.compare_exchange_weak(
                        head, head + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    // We own this slot - read the data
                    T value = data_[slot];
                    // Release: set sequence to head + capacity so producers
                    // can reuse this slot on the next wrap-around
                    sequence_[slot].store(head + cap, std::memory_order_release);
                    return value;
                }
                // CAS failed, another consumer got it; retry
            } else if (diff < 0) {
                // Queue is empty
                return std::nullopt;
            }
            // diff > 0: another consumer claimed this slot but hasn't
            // finished yet, or we read a stale head; retry
        }
    }

    // Check if empty (approximate in concurrent context)
    bool empty() const {
        uint32_t head = header_->head.load(std::memory_order_acquire);
        uint32_t tail = header_->tail.load(std::memory_order_acquire);
        return head == tail;
    }

    // Check if full (approximate in concurrent context)
    bool full() const {
        uint32_t head = header_->head.load(std::memory_order_acquire);
        uint32_t tail = header_->tail.load(std::memory_order_acquire);
        return (tail - head) >= header_->capacity;
    }

    // Get current size (approximate in concurrent context)
    size_t size() const {
        uint32_t head = header_->head.load(std::memory_order_acquire);
        uint32_t tail = header_->tail.load(std::memory_order_acquire);
        // uint32_t subtraction handles wraparound correctly
        return static_cast<size_t>(tail - head);
    }

    size_t capacity() const { return header_->capacity; }

private:
    Memory& memory_;
    std::string name_;
    Header* header_;
    T* data_;
    std::atomic<uint32_t>* sequence_;
};

} // namespace zeroipc