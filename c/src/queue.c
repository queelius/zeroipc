#include "zeroipc.h"
#include "zeroipc_queue.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Queue header in shared memory */
typedef struct {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    uint32_t capacity;
    uint32_t elem_size;
} queue_header_t;

/* Queue structure */
struct zeroipc_queue {
    zeroipc_memory_t* memory;
    queue_header_t* header;
    void* data;
    char name[32];
};

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
    if (capacity > (SIZE_MAX - sizeof(queue_header_t)) / elem_size) {
        free(queue);
        return NULL;  // Would overflow
    }
    size_t total_size = sizeof(queue_header_t) + elem_size * capacity;
    
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
    
    uint32_t current_tail, next_tail;
    
    /* Reserve a slot atomically */
    do {
        current_tail = atomic_load_explicit(&queue->header->tail, memory_order_relaxed);
        next_tail = (current_tail + 1) % queue->header->capacity;
        
        /* Check if full */
        if (next_tail == atomic_load_explicit(&queue->header->head, memory_order_acquire)) {
            return ZEROIPC_ERROR_SIZE;  /* Queue full */
        }
    } while (!atomic_compare_exchange_weak_explicit(
                &queue->header->tail, &current_tail, next_tail,
                memory_order_acq_rel, memory_order_relaxed));
    
    /* We own the slot at current_tail, write the value */
    char* dest = (char*)queue->data + (current_tail * queue->header->elem_size);
    memcpy(dest, value, queue->header->elem_size);
    
    /* Memory fence to ensure data is written before other threads can read it */
    atomic_thread_fence(memory_order_release);
    
    return ZEROIPC_OK;
}

/* Pop from queue (lock-free MPMC - multiple producer, multiple consumer) */
int zeroipc_queue_pop(zeroipc_queue_t* queue, void* value) {
    if (!queue || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    uint32_t current_head, next_head;
    
    /* Reserve a slot to read atomically */
    do {
        current_head = atomic_load_explicit(&queue->header->head, memory_order_relaxed);
        
        /* Check if empty */
        if (current_head == atomic_load_explicit(&queue->header->tail, memory_order_acquire)) {
            return ZEROIPC_ERROR_NOT_FOUND;  /* Queue empty */
        }
        
        next_head = (current_head + 1) % queue->header->capacity;
    } while (!atomic_compare_exchange_weak_explicit(
                &queue->header->head, &current_head, next_head,
                memory_order_acq_rel, memory_order_relaxed));
    
    /* We own the slot at current_head, read the value */
    char* src = (char*)queue->data + (current_head * queue->header->elem_size);
    
    /* Memory fence to ensure we read the data that was fully written */
    atomic_thread_fence(memory_order_acquire);
    
    memcpy(value, src, queue->header->elem_size);
    
    return ZEROIPC_OK;
}

/* Check if empty */
int zeroipc_queue_empty(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    
    return atomic_load_explicit(&queue->header->head, memory_order_acquire) ==
           atomic_load_explicit(&queue->header->tail, memory_order_acquire);
}

/* Check if full */
int zeroipc_queue_full(zeroipc_queue_t* queue) {
    if (!queue) return 1;
    
    uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    uint32_t next_tail = (tail + 1) % queue->header->capacity;
    return next_tail == atomic_load_explicit(&queue->header->head, memory_order_acquire);
}

/* Get size */
size_t zeroipc_queue_size(zeroipc_queue_t* queue) {
    if (!queue) return 0;
    
    uint32_t head = atomic_load_explicit(&queue->header->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&queue->header->tail, memory_order_acquire);
    
    if (tail >= head) {
        return tail - head;
    } else {
        return queue->header->capacity - head + tail;
    }
}

/* Get capacity */
size_t zeroipc_queue_capacity(zeroipc_queue_t* queue) {
    return queue ? queue->header->capacity : 0;
}