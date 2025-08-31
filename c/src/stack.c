#include "zeroipc.h"
#include "zeroipc_stack.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Stack header in shared memory */
typedef struct {
    _Atomic int32_t top;  /* -1 when empty */
    uint32_t capacity;
    uint32_t elem_size;
} stack_header_t;

/* Stack structure */
struct zeroipc_stack {
    zeroipc_memory_t* memory;
    stack_header_t* header;
    void* data;
    char name[32];
};

/* Create stack */
zeroipc_stack_t* zeroipc_stack_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity) {
    if (!mem || !name || elem_size == 0 || capacity == 0) {
        return NULL;
    }
    
    /* Allocate stack structure */
    zeroipc_stack_t* stack = calloc(1, sizeof(zeroipc_stack_t));
    if (!stack) {
        return NULL;
    }
    
    stack->memory = mem;
    strncpy(stack->name, name, sizeof(stack->name) - 1);
    
    /* Calculate total size with overflow check */
    if (capacity > (SIZE_MAX - sizeof(stack_header_t)) / elem_size) {
        free(stack);
        return NULL;  // Would overflow
    }
    size_t total_size = sizeof(stack_header_t) + elem_size * capacity;
    
    /* Add to table and get offset */
    size_t offset;
    int result = zeroipc_table_add(mem, name, total_size, &offset);
    if (result != ZEROIPC_OK) {
        free(stack);
        return NULL;
    }
    
    /* Get header and data pointers */
    stack->header = (stack_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    stack->data = (char*)stack->header + sizeof(stack_header_t);
    
    /* Initialize header */
    atomic_store(&stack->header->top, -1);  /* Empty */
    stack->header->capacity = capacity;
    stack->header->elem_size = elem_size;
    
    return stack;
}

/* Open existing stack */
zeroipc_stack_t* zeroipc_stack_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) {
        return NULL;
    }
    
    /* Find in table */
    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) {
        return NULL;
    }
    
    /* Allocate stack structure */
    zeroipc_stack_t* stack = calloc(1, sizeof(zeroipc_stack_t));
    if (!stack) {
        return NULL;
    }
    
    stack->memory = mem;
    strncpy(stack->name, name, sizeof(stack->name) - 1);
    
    /* Get header and data pointers */
    stack->header = (stack_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    stack->data = (char*)stack->header + sizeof(stack_header_t);
    
    return stack;
}

/* Close stack */
void zeroipc_stack_close(zeroipc_stack_t* stack) {
    if (stack) {
        free(stack);
    }
}

/* Push to stack (lock-free) */
int zeroipc_stack_push(zeroipc_stack_t* stack, const void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    int32_t current_top, new_top;
    
    /* Reserve a slot atomically */
    do {
        current_top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
        
        /* Check if full */
        if (current_top >= (int32_t)(stack->header->capacity - 1)) {
            return ZEROIPC_ERROR_SIZE;  /* Stack full */
        }
        
        new_top = current_top + 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &stack->header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));
    
    /* We own the slot at new_top, write the value */
    char* dest = (char*)stack->data + (new_top * stack->header->elem_size);
    memcpy(dest, value, stack->header->elem_size);
    
    /* Memory fence to ensure data is written before other threads can read it */
    atomic_thread_fence(memory_order_release);
    
    return ZEROIPC_OK;
}

/* Pop from stack (lock-free) */
int zeroipc_stack_pop(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    int32_t current_top, new_top;
    
    /* Reserve a slot to read atomically */
    do {
        current_top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
        
        /* Check if empty */
        if (current_top < 0) {
            return ZEROIPC_ERROR_NOT_FOUND;  /* Stack empty */
        }
        
        new_top = current_top - 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &stack->header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));
    
    /* We own the slot at current_top, read the value */
    char* src = (char*)stack->data + (current_top * stack->header->elem_size);
    
    /* Memory fence to ensure we read the data that was fully written */
    atomic_thread_fence(memory_order_acquire);
    
    memcpy(value, src, stack->header->elem_size);
    
    return ZEROIPC_OK;
}

/* Peek at top without removing */
int zeroipc_stack_top(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    int32_t current_top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    
    if (current_top < 0) {
        return ZEROIPC_ERROR_NOT_FOUND;
    }
    
    char* src = (char*)stack->data + (current_top * stack->header->elem_size);
    memcpy(value, src, stack->header->elem_size);
    
    return ZEROIPC_OK;
}

/* Check if empty */
int zeroipc_stack_empty(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    
    return atomic_load_explicit(&stack->header->top, memory_order_acquire) < 0;
}

/* Check if full */
int zeroipc_stack_full(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    
    return atomic_load_explicit(&stack->header->top, memory_order_acquire) >= 
           (int32_t)(stack->header->capacity - 1);
}

/* Get size */
size_t zeroipc_stack_size(zeroipc_stack_t* stack) {
    if (!stack) return 0;
    
    int32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    return top < 0 ? 0 : (size_t)(top + 1);
}

/* Get capacity */
size_t zeroipc_stack_capacity(zeroipc_stack_t* stack) {
    return stack ? stack->header->capacity : 0;
}