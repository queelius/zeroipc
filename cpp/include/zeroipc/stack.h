#ifndef ZEROIPC_STACK_H
#define ZEROIPC_STACK_H

#include "memory.h"
#include <atomic>
#include <optional>

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
    
    // Create new stack
    Stack(Memory& memory, std::string_view name, size_t capacity)
        : memory_(memory), name_(name) {
        
        size_t total_size = sizeof(Header) + sizeof(T) * capacity;
        size_t offset = memory.allocate(name, total_size);
        
        header_ = reinterpret_cast<Header*>(
            static_cast<char*>(memory.base()) + offset);
        
        // Initialize header
        header_->top.store(-1, std::memory_order_relaxed);
        header_->capacity = capacity;
        header_->elem_size = sizeof(T);
        
        data_ = reinterpret_cast<T*>(
            reinterpret_cast<char*>(header_) + sizeof(Header));
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
    }
    
    // Push (lock-free)
    bool push(const T& value) {
        int32_t current_top, new_top;
        
        // Reserve a slot atomically
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
        
        // We own the slot at new_top, write the value
        data_[new_top] = value;
        
        // Memory fence to ensure data is written before other threads can read it
        std::atomic_thread_fence(std::memory_order_release);
        
        return true;
    }
    
    // Pop (lock-free)
    std::optional<T> pop() {
        int32_t current_top, new_top;
        
        // Reserve a slot to read atomically
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
        
        // Memory fence to ensure we read the data that was fully written
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // We own the slot at current_top, read the value
        T value = data_[current_top];
        
        return value;
    }
    
    // Peek at top without removing
    std::optional<T> top() const {
        int32_t current_top = header_->top.load(std::memory_order_acquire);
        if (current_top < 0) {
            return std::nullopt;
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
};

} // namespace zeroipc

#endif // ZEROIPC_STACK_H