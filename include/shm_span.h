#pragma once
#include "posix_shm.h"

template<typename T>
class shm_span {
protected:
    posix_shm& shm;
    size_t offset;
    size_t num_elem;

public:
    shm_span(posix_shm& shm, size_t off, size_t count)
        : shm(shm), offset(off), num_elem(count) {}

    T* data() { return static_cast<T*>(shm.get_base_addr()) + offset; }
    const T* data() const { return static_cast<const T*>(shm.get_base_addr()) + offset; }
    size_t size() const { return num_elem; }

    T& operator[](size_t index) { return data()[index]; }
    const T& operator[](size_t index) const { return data()[index]; }
};
