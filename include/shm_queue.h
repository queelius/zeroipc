#pragma once
#include "shm_span.h"
#include "shm_table.h"

template<typename T>
class shm_queue : public shm_span<T> {
    size_t head;
    size_t tail;

public:
    shm_queue(posix_shm& shm, size_t count, const std::string& name)
        : shm_span<T>(shm, 0, count), head(0), tail(0) {
        auto* table = static_cast<shm_table*>(shm.get_base_addr());
        auto* entry = table->find(name);
        if (entry) {
            if (entry->num_elements != count) {
                throw std::runtime_error("Mismatch in queue size");
            }
            this->offset = entry->offset;
        } else {
            this->offset = shared_mem.get_total_size() - count * sizeof(T);
            table->add_entry(name, this->offset, count * sizeof(T), sizeof(T), count);
        }
    }

    shm_queue(PosixSharedMem& shared_mem, const std::string& name)
        : shm_span<T>(shared_mem, 0, 0), head(0), tail(0) {
        auto* table = static_cast<SharedMemoryTable*>(shared_mem.get_base_addr());
        auto* entry = table->find_entry(name);
        if (!entry) {
            throw std::runtime_error("Queue not found");
        }
        this->offset = entry->offset;
        this->num_elements = entry->num_elements;
    }

    // Implement queue operations (push, pop, etc.) here
};
