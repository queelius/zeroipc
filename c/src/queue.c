#include "zeroipc.h"
#include "zeroipc_queue.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Queue header in shared memory */
typedef struct {
    _Atomic uint64_t head;      /* next position to pop */
    _Atomic uint64_t tail;      /* next position to push (reserve) */
    uint32_t capacity;          /* slot count */
    uint32_t elem_size;         /* bytes per element */
    uint32_t slot_size;         /* bytes per slot including metadata */
} queue_header_t;

/* Per-slot metadata preceding the element payload */
typedef struct {
    _Atomic uint64_t seq;       /* sequence number for Vyukov MPMC algorithm */
    /* payload follows */
} queue_slot_t;

/* Queue structure */
struct zeroipc_queue {
    zeroipc_memory_t* memory;
    queue_header_t* header;
    void* data;
    char name[32];
};

static size_t align_up(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

static queue_slot_t* queue_slot_at(zeroipc_queue_t* queue, uint64_t index) {
    uint32_t capacity = queue->header->capacity;
    uint64_t offset = (index % capacity) * queue->header->slot_size;
    return (queue_slot_t*)((char*)queue->data + offset);
}

/* Create queue */
zeroipc_queue_t* zeroipc_queue_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity) {
    if (!mem || !name || elem_size == 0 || capacity == 0) {
        return NULL;
    }
    
    /* Allocate queue structure */
    zeroipc_queue_t* queue = calloc(1, sizeof(zeroipc_queue_t));
    if (!queue) {
        return NULL;
    }
    
    queue->memory = mem;
    strncpy(queue->name, name, sizeof(queue->name) - 1);
    
    /* Calculate total size with overflow check */
    size_t slot_size = align_up(sizeof(queue_slot_t) + elem_size, sizeof(uint64_t));
    if (capacity > (SIZE_MAX - sizeof(queue_header_t)) / slot_size) {
        free(queue);
        return NULL;  // Would overflow
    }
    size_t total_size = sizeof(queue_header_t) + slot_size * capacity;
    
    /* Add to table and get offset */
    size_t offset;
    int result = zeroipc_table_add(mem, name, total_size, &offset);
    if (result != ZEROIPC_OK) {
        free(queue);
        return NULL;
    }
    
    /* Get header and data pointers */
    queue->header = (queue_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    queue->data = (char*)queue->header + sizeof(queue_header_t);
    
    /* Initialize header */
    atomic_store(&queue->header->head, 0);
    atomic_store(&queue->header->tail, 0);
    queue->header->capacity = capacity;
    queue->header->elem_size = elem_size;
    queue->header->slot_size = slot_size;
    /* Initialize per-slot sequence numbers */
    for (uint32_t i = 0; i < capacity; ++i) {
        queue_slot_t* slot = (queue_slot_t*)((char*)queue->data + i * slot_size);
        atomic_store_explicit(&slot->seq, i, memory_order_relaxed);
    }
    
    return queue;
}

/* Open existing queue */
zeroipc_queue_t* zeroipc_queue_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) {
        return NULL;
    }
    
    /* Find in table */
    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) {
        return NULL;
    }
    
    /* Allocate queue structure */
    zeroipc_queue_t* queue = calloc(1, sizeof(zeroipc_queue_t));
    if (!queue) {
        return NULL;
    }
    
    queue->memory = mem;
    strncpy(queue->name, name, sizeof(queue->name) - 1);
    
    /* Get header and data pointers */
    queue->header = (queue_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    queue->data = (char*)queue->header + sizeof(queue_header_t);
    if (queue->header->slot_size == 0 || queue->header->capacity == 0 || queue->header->elem_size == 0) {
        free(queue);
        return NULL;
    }
    
    return queue;
}

/* Close queue */
void zeroipc_queue_close(zeroipc_queue_t* queue) {
    if (queue) {
        free(queue);
    }
}

/* Push to queue (lock-free MPSC - multiple producer, single consumer) */
int zeroipc_queue_push(zeroipc_queue_t* queue, const void* value) {
    if (!queue || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    for (;;) {
        uint64_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
        queue_slot_t* slot = queue_slot_at(queue, tail);
        uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)tail;
        
        if (diff == 0) {
            /* Try to reserve this slot */
            if (atomic_compare_exchange_weak_explicit(
                    &queue->header->tail, &tail, tail + 1,
                    memory_order_acq_rel, memory_order_relaxed)) {
                void* dest = (char*)slot + sizeof(queue_slot_t);
                memcpy(dest, value, queue->header->elem_size);
                /* Publish */
                atomic_store_explicit(&slot->seq, tail + 1, memory_order_release);
                return ZEROIPC_OK;
            }
        } else if (diff < 0) {
            return ZEROIPC_ERROR_SIZE;  /* Queue full */
        }
        /* Otherwise, another producer is ahead; retry */
    }
}

/* Pop from queue (lock-free MPMC - multiple producer, multiple consumer) */
int zeroipc_queue_pop(zeroipc_queue_t* queue, void* value) {
    if (!queue || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    const uint32_t capacity = queue->header->capacity;
    for (;;) {
        uint64_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
        queue_slot_t* slot = queue_slot_at(queue, head);
        uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(head + 1);
        
        if (diff == 0) {
            /* Slot contains data */
            if (atomic_compare_exchange_weak_explicit(
                    &queue->header->head, &head, head + 1,
                    memory_order_acq_rel, memory_order_relaxed)) {
                void* src = (char*)slot + sizeof(queue_slot_t);
                memcpy(value, src, queue->header->elem_size);
                /* Mark slot free for next wrap */
                atomic_store_explicit(&slot->seq, head + capacity, memory_order_release);
                return ZEROIPC_OK;
            }
        } else if (diff < 0) {
            return ZEROIPC_ERROR_NOT_FOUND;  /* Queue empty */
        }
        /* Otherwise, another consumer is ahead; retry */
    }
}

/* Check if empty */
int zeroipc_queue_empty(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    
    uint64_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    queue_slot_t* slot = queue_slot_at(queue, head);
    uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
    return seq != head + 1;
}

/* Check if full */
int zeroipc_queue_full(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    
    uint64_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    queue_slot_t* slot = queue_slot_at(queue, tail);
    uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
    return (intptr_t)(seq - tail) < 0;
}

/* Get size */
size_t zeroipc_queue_size(zeroipc_queue_t* queue) {
    if (!queue) return 0;
    
    uint64_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    uint64_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    uint64_t distance = (tail >= head) ? (tail - head) : 0;
    if (distance > queue->header->capacity) {
        distance = queue->header->capacity;
    }
    return (size_t)distance;
}

/* Get capacity */
size_t zeroipc_queue_capacity(zeroipc_queue_t* queue) {
    return queue ? queue->header->capacity : 0;
}
