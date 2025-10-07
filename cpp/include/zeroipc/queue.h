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
        if (capacity > (SIZE_MAX - sizeof(Header)) / sizeof(T)) {
            throw std::overflow_error("Queue capacity too large");
        }
        
        size_t total_size = sizeof(Header) + sizeof(T) * capacity;
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->head.store(0, std::memory_order_relaxed);
        header_->tail.store(0, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        
        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }
    
    // Open existing queue
    Queue(Memory& memory, std::string_view name)
        : memory_(memory), name_(name) {
        
        size_t offset, size;
        if (!memory.find(name, offset, size)) {
            throw std::runtime_error("Queue not found: " + std::string(name));
        }
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        if (header_->elem_size != sizeof(T)) {
            throw std::runtime_error("Type size mismatch");
        }
        
        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
    }
    
    // Enqueue (lock-free MPMC)
    [[nodiscard]] bool push(const T& value) {
        uint32_t current_tail, next_tail;
        
        // Reserve a slot atomically
        do {
            current_tail = header_->tail.load(std::memory_order_relaxed);
            next_tail = (current_tail + 1) % header_->capacity;
            
            // Check if full
            if (next_tail == header_->head.load(std::memory_order_acquire)) {
                return false;
            }
        } while (!header_->tail.compare_exchange_weak(
                    current_tail, next_tail,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed));
        
        // We own the slot at current_tail, write the value
        data_[current_tail] = value;
        
        // Memory fence to ensure data is written before other threads can read it
        std::atomic_thread_fence(std::memory_order_release);
        
        return true;
    }
    
    // Dequeue (lock-free MPMC)
    [[nodiscard]] std::optional<T> pop() {
        uint32_t current_head, next_head;
        
        // Reserve a slot to read atomically
        do {
            current_head = header_->head.load(std::memory_order_relaxed);
            
            // Check if empty
            if (current_head == header_->tail.load(std::memory_order_acquire)) {
                return std::nullopt;
            }
            
            next_head = (current_head + 1) % header_->capacity;
        } while (!header_->head.compare_exchange_weak(
                    current_head, next_head,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed));
        
        // Memory fence to ensure we read the data that was fully written
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // We own the slot at current_head, read the value
        T value = data_[current_head];
        
        return value;
    }
    
    // Check if empty
    bool empty() const {
        return header_->head.load(std::memory_order_acquire) == 
               header_->tail.load(std::memory_order_acquire);
    }
    
    // Check if full
    bool full() const {
        uint32_t tail = header_->tail.load(std::memory_order_acquire);
        uint32_t next_tail = (tail + 1) % header_->capacity;
        return next_tail == header_->head.load(std::memory_order_acquire);
    }
    
    // Get current size (approximate in concurrent context)
    size_t size() const {
        uint32_t head = header_->head.load(std::memory_order_acquire);
        uint32_t tail = header_->tail.load(std::memory_order_acquire);
        if (tail >= head) {
            return tail - head;
        } else {
            return header_->capacity - head + tail;
        }
    }
    
    size_t capacity() const { return header_->capacity; }
    
private:
    Memory& memory_;
    std::string name_;
    Header* header_;
    T* data_;
};

} // namespace zeroipc