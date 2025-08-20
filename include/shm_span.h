#pragma once
#include <type_traits>

/**
 * @brief Base class for shared memory data structures that span a region
 * 
 * @tparam T Type of elements (must be trivially copyable)
 * @tparam ShmType Type of shared memory manager
 */
template<typename T, typename ShmType>
class shm_span {
protected:
    ShmType& shm;
    size_t offset;
    size_t num_elem;

public:
    shm_span(ShmType& shm, size_t off, size_t count)
        : shm(shm), offset(off), num_elem(count) {}

    T* data() { 
        return reinterpret_cast<T*>(
            static_cast<char*>(shm.get_base_addr()) + offset
        );
    }
    
    const T* data() const { 
        return reinterpret_cast<const T*>(
            static_cast<const char*>(shm.get_base_addr()) + offset
        );
    }
    
    size_t size() const { return num_elem; }

    T& operator[](size_t index) { return data()[index]; }
    const T& operator[](size_t index) const { return data()[index]; }
};