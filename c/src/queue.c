#include "zeroipc.h"
#include "zeroipc_queue.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * Queue binary layout (matches C++/Go/Python):
 *   [head:u32][tail:u32][capacity:u32][elem_size:u32]  (16 bytes)
 *   [data[0]][data[1]]...[data[cap-1]]
 *   [seq[0]:u32][seq[1]:u32]...[seq[cap-1]:u32]
 *
 * Uses Vyukov bounded MPMC queue algorithm with per-slot sequence numbers.
 */

/* Queue header in shared memory — matches C++ Queue::Header */
typedef struct {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    uint32_t capacity;
    uint32_t elem_size;
} queue_header_t;

/* Queue structure (process-local handle) */
struct zeroipc_queue {
    zeroipc_memory_t* memory;
    queue_header_t* header;
    void* data;                  /* pointer to data array */
    _Atomic uint32_t* seq;       /* pointer to sequence array */
    char name[32];
};

/* Create queue */
zeroipc_queue_t* zeroipc_queue_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity) {
    if (!mem || !name || elem_size == 0 || capacity == 0) {
        return NULL;
    }

    zeroipc_queue_t* queue = calloc(1, sizeof(zeroipc_queue_t));
    if (!queue) return NULL;

    queue->memory = mem;
    strncpy(queue->name, name, sizeof(queue->name) - 1);

    /* Layout: [header][data: elem_size * capacity][seq: uint32 * capacity] */
    size_t seq_array_size = sizeof(uint32_t) * capacity;
    if (capacity > (SIZE_MAX - sizeof(queue_header_t) - seq_array_size) / elem_size) {
        free(queue);
        return NULL;
    }
    size_t total_size = sizeof(queue_header_t) + elem_size * capacity + seq_array_size;

    size_t offset;
    int result = zeroipc_table_add(mem, name, total_size, &offset);
    if (result != ZEROIPC_OK) {
        free(queue);
        return NULL;
    }

    queue->header = (queue_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    queue->data = (char*)queue->header + sizeof(queue_header_t);
    queue->seq = (_Atomic uint32_t*)((char*)queue->data + elem_size * capacity);

    /* Initialize header */
    atomic_store(&queue->header->head, 0);
    atomic_store(&queue->header->tail, 0);
    queue->header->capacity = capacity;
    queue->header->elem_size = elem_size;

    /* Initialize per-slot sequence numbers: seq[i] = i */
    for (uint32_t i = 0; i < capacity; ++i) {
        atomic_store_explicit(&queue->seq[i], i, memory_order_relaxed);
    }

    return queue;
}

/* Open existing queue */
zeroipc_queue_t* zeroipc_queue_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) return NULL;

    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) return NULL;

    zeroipc_queue_t* queue = calloc(1, sizeof(zeroipc_queue_t));
    if (!queue) return NULL;

    queue->memory = mem;
    strncpy(queue->name, name, sizeof(queue->name) - 1);

    queue->header = (queue_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    if (queue->header->capacity == 0 || queue->header->elem_size == 0) {
        free(queue);
        return NULL;
    }

    queue->data = (char*)queue->header + sizeof(queue_header_t);
    queue->seq = (_Atomic uint32_t*)(
        (char*)queue->data + queue->header->elem_size * queue->header->capacity);

    return queue;
}

void zeroipc_queue_close(zeroipc_queue_t* queue) {
    free(queue);
}

/* Push (Vyukov bounded MPMC) */
int zeroipc_queue_push(zeroipc_queue_t* queue, const void* value) {
    if (!queue || !value) return ZEROIPC_ERROR_SIZE;

    uint32_t cap = queue->header->capacity;

    for (;;) {
        uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_relaxed);
        uint32_t slot = tail % cap;
        uint32_t s = atomic_load_explicit(&queue->seq[slot], memory_order_acquire);
        int32_t diff = (int32_t)s - (int32_t)tail;

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &queue->header->tail, &tail, tail + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                void* dest = (char*)queue->data + slot * queue->header->elem_size;
                memcpy(dest, value, queue->header->elem_size);
                atomic_store_explicit(&queue->seq[slot], tail + 1, memory_order_release);
                return ZEROIPC_OK;
            }
        } else if (diff < 0) {
            return ZEROIPC_ERROR_SIZE;  /* full */
        }
    }
}

/* Pop (Vyukov bounded MPMC) */
int zeroipc_queue_pop(zeroipc_queue_t* queue, void* value) {
    if (!queue || !value) return ZEROIPC_ERROR_SIZE;

    uint32_t cap = queue->header->capacity;

    for (;;) {
        uint32_t head = atomic_load_explicit(&queue->header->head, memory_order_relaxed);
        uint32_t slot = head % cap;
        uint32_t s = atomic_load_explicit(&queue->seq[slot], memory_order_acquire);
        int32_t diff = (int32_t)s - (int32_t)(head + 1);

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &queue->header->head, &head, head + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                void* src = (char*)queue->data + slot * queue->header->elem_size;
                memcpy(value, src, queue->header->elem_size);
                atomic_store_explicit(&queue->seq[slot], head + cap, memory_order_release);
                return ZEROIPC_OK;
            }
        } else if (diff < 0) {
            return ZEROIPC_ERROR_NOT_FOUND;  /* empty */
        }
    }
}

int zeroipc_queue_empty(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    uint32_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    return head == tail;
}

int zeroipc_queue_full(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    uint32_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    return (tail - head) >= queue->header->capacity;
}

size_t zeroipc_queue_size(zeroipc_queue_t* queue) {
    if (!queue) return 0;
    uint32_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    /* uint32_t subtraction handles wraparound correctly */
    return (size_t)(tail - head);
}

size_t zeroipc_queue_capacity(zeroipc_queue_t* queue) {
    return queue ? queue->header->capacity : 0;
}
