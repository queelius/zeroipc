#include "zeroipc.h"
#include "zeroipc_stack.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Stack header in shared memory */
typedef struct {
    _Atomic uint32_t top;   /* number of elements currently stored */
    uint32_t capacity;      /* number of slots */
    uint32_t elem_size;     /* bytes per element */
    atomic_flag lock;       /* spin lock for concurrent access */
} stack_header_t;

/* Stack structure */
struct zeroipc_stack {
    zeroipc_memory_t* memory;
    stack_header_t* header;
    void* data;
    char name[32];
};

static void stack_lock(stack_header_t* header) {
    while (atomic_flag_test_and_set_explicit(&header->lock, memory_order_acquire)) {
        ; /* spin */
    }
}

static void stack_unlock(stack_header_t* header) {
    atomic_flag_clear_explicit(&header->lock, memory_order_release);
}

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
    atomic_store(&stack->header->top, 0);  /* Empty size */
    stack->header->capacity = capacity;
    stack->header->elem_size = elem_size;
    atomic_flag_clear(&stack->header->lock);
    
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
    if (stack->header->capacity == 0 || stack->header->elem_size == 0) {
        free(stack);
        return NULL;
    }
    
    return stack;
}

/* Close stack */
void zeroipc_stack_close(zeroipc_stack_t* stack) {
    if (stack) {
        free(stack);
    }
}

/* Push to stack (spin-locked for correctness) */
int zeroipc_stack_push(zeroipc_stack_t* stack, const void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }

    stack_lock(stack->header);
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
    if (top >= stack->header->capacity) {
        stack_unlock(stack->header);
        return ZEROIPC_ERROR_SIZE;  /* Stack full */
    }

    char* dest = (char*)stack->data + (top * stack->header->elem_size);
    memcpy(dest, value, stack->header->elem_size);
    atomic_store_explicit(&stack->header->top, top + 1, memory_order_release);
    stack_unlock(stack->header);
    return ZEROIPC_OK;
}

/* Pop from stack (spin-locked for correctness) */
int zeroipc_stack_pop(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }

    stack_lock(stack->header);
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
    if (top == 0) {
        stack_unlock(stack->header);
        return ZEROIPC_ERROR_NOT_FOUND;  /* Stack empty */
    }

    uint32_t index = top - 1;
    char* src = (char*)stack->data + (index * stack->header->elem_size);
    memcpy(value, src, stack->header->elem_size);
    atomic_store_explicit(&stack->header->top, top - 1, memory_order_release);
    stack_unlock(stack->header);
    return ZEROIPC_OK;
}

/* Peek at top without removing */
int zeroipc_stack_top(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    if (top == 0) {
        return ZEROIPC_ERROR_NOT_FOUND;
    }

    uint32_t index = top - 1;
    char* src = (char*)stack->data + (index * stack->header->elem_size);
    memcpy(value, src, stack->header->elem_size);
    return ZEROIPC_OK;
}

/* Check if empty */
int zeroipc_stack_empty(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    return top == 0;
}

/* Check if full */
int zeroipc_stack_full(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    return top >= stack->header->capacity;
}

/* Get size */
size_t zeroipc_stack_size(zeroipc_stack_t* stack) {
    if (!stack) return 0;
    
    uint32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    uint32_t capacity = stack->header->capacity;
    if (top > capacity) top = capacity;
    return (size_t)top;
}

/* Get capacity */
size_t zeroipc_stack_capacity(zeroipc_stack_t* stack) {
    return stack ? stack->header->capacity : 0;
}
