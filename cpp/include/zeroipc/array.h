#pragma once

#include <zeroipc/memory.h>
#include <type_traits>
#include <stdexcept>
#include <string_view>
#include <cstring>

namespace zeroipc {

/**
 * Fixed-size array in shared memory.
 * 
 * @tparam T Element type (must be trivially copyable)
 * 
 * The array stores only its capacity in the header.
 * No type information is stored - users are responsible for type consistency.
 */
template<typename T>
class Array {
    static_assert(std::is_trivially_copyable_v<T>, 
                  "Array elements must be trivially copyable");
    
public:
    struct Header {
        uint64_t capacity;
    };
    
    /**
     * Create or open an array
     * @param memory Shared memory instance
     * @param name Name of the array
     * @param capacity Number of elements (0 to open existing)
     */
    Array(Memory& memory, std::string_view name, size_t capacity = 0)
        : memory_(memory) {
        
        if (name.size() >= 32) {
            throw std::invalid_argument("Name too long (max 31 characters)");
        }
        
        auto* entry = memory.table()->find(name);
        
        if (entry) {
            // Open existing array
            if (capacity != 0) {
                // Optionally validate capacity if provided
                Header* hdr = static_cast<Header*>(memory.at(entry->offset));
                if (hdr->capacity != capacity) {
                    throw std::runtime_error(
                        "Capacity mismatch: array has " + 
                        std::to_string(hdr->capacity) + 
                        " but requested " + std::to_string(capacity));
                }
            }
            
            offset_ = entry->offset;
            header_ = static_cast<Header*>(memory.at(offset_));
            data_ = static_cast<T*>(memory.at(offset_ + sizeof(Header)));
            capacity_ = header_->capacity;
            name_ = name;
        } else {
            // Create new array
            if (capacity == 0) {
                throw std::invalid_argument("Capacity required to create new array");
            }
            
            size_t total_size = sizeof(Header) + capacity * sizeof(T);
            offset_ = memory.table()->allocate(total_size);
            
            if (!memory.table()->add(name, offset_, total_size)) {
                throw std::runtime_error("Failed to add array to table");
            }
            
            // Initialize header
            header_ = static_cast<Header*>(memory.at(offset_));
            header_->capacity = capacity;
            
            // Get data pointer
            data_ = static_cast<T*>(memory.at(offset_ + sizeof(Header)));
            
            // Zero-initialize elements
            std::memset(data_, 0, capacity * sizeof(T));
            
            capacity_ = capacity;
            name_ = name;
        }
    }
    
    /**
     * Access element by index
     */
    T& operator[](size_t index) {
        if (index >= capacity_) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }
    
    const T& operator[](size_t index) const {
        if (index >= capacity_) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }
    
    /**
     * Access element with bounds checking
     */
    T& at(size_t index) {
        if (index >= capacity_) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }
    
    const T& at(size_t index) const {
        if (index >= capacity_) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[index];
    }
    
    /**
     * Get pointer to data
     */
    T* data() { return data_; }
    const T* data() const { return data_; }
    
    /**
     * Get array capacity
     */
    size_t capacity() const { return capacity_; }
    
    /**
     * Get array name
     */
    std::string_view name() const { return name_; }
    
    /**
     * Iterator support
     */
    T* begin() { return data_; }
    T* end() { return data_ + capacity_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + capacity_; }
    
    /**
     * Fill array with value
     */
    void fill(const T& value) {
        for (size_t i = 0; i < capacity_; ++i) {
            data_[i] = value;
        }
    }
    
private:
    Memory& memory_;
    Header* header_;
    T* data_;
    size_t capacity_;
    uint32_t offset_;
    std::string name_;
};

} // namespace zeroipc