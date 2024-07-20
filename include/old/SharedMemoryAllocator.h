#ifndef SHARED_MEMORY_ALLOCATOR_H
#define SHARED_MEMORY_ALLOCATOR_H

#include "SharedMemoryManager.h"
#include <cstddef>
#include <new>
#include <limits>
#include <atomic>

template <typename T>
class SharedMemoryAllocator {
public:
    SharedMemoryManager& smm;
    struct Metadata {
        std::atomic<size_t> size;
        std::atomic<size_t> capacity;
    };
    Metadata* metadata;

    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    SharedMemoryAllocator(SharedMemoryManager& manager) : smm(manager) {
        metadata = static_cast<Metadata*>(smm.get_metadata_addr());
        if (smm.is_owner()) {
            new (metadata) Metadata{0, 0};
        }
        printf("Allocator created. Metadata at: %p, size: %zu, capacity: %zu\n", 
               metadata, metadata->size.load(), metadata->capacity.load());
    }

    T* allocate(std::size_t n) {
        size_t offset = metadata->capacity.fetch_add(n) * sizeof(T);
        if (offset + n * sizeof(T) > smm.get_data_size()) {
            throw std::bad_alloc();
        }
        return reinterpret_cast<T*>(static_cast<char*>(smm.get_data_addr()) + offset);
    }

    void deallocate(T* p, std::size_t n) {
        // In this simple implementation, we don't actually free memory
    }

    size_t size() const { return metadata->size.load(); }
    void set_size(size_t s) { metadata->size.store(s); }

    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        ::new((void *)p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    struct rebind {
        using other = SharedMemoryAllocator<U>;
    };
};

template <typename T, typename U>
bool operator==(const SharedMemoryAllocator<T>&, const SharedMemoryAllocator<U>&) {
    return true;
}

template <typename T, typename U>
bool operator!=(const SharedMemoryAllocator<T>&, const SharedMemoryAllocator<U>&) {
    return false;
}

#endif // SHARED_MEMORY_ALLOCATOR_H