/**
 * ZeroIPC Core - Elegant C API for zero-copy IPC
 *
 * Design principles:
 * - Simple, composable interfaces
 * - Consistent naming and patterns
 * - Zero-allocation where possible
 * - Clear ownership semantics
 * - Binary compatible with C++ and Python
 */

#ifndef ZEROIPC_CORE_H
#define ZEROIPC_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Core Types and Constants
 * ============================================================================ */

/* Result type for operations that can fail */
typedef enum {
    ZIPC_OK = 0,
    ZIPC_ERROR = -1,
    ZIPC_NOT_FOUND = -2,
    ZIPC_EXISTS = -3,
    ZIPC_FULL = -4,
    ZIPC_EMPTY = -5,
    ZIPC_INVALID = -6,
    ZIPC_NO_MEMORY = -7,
    ZIPC_IO_ERROR = -8,
} zipc_result;

/* Opaque handle types for better type safety */
typedef struct zipc_shm* zipc_shm_t;
typedef struct zipc_view* zipc_view_t;

/* Structure types for type-safe access */
typedef enum {
    ZIPC_TYPE_ARRAY,
    ZIPC_TYPE_QUEUE,
    ZIPC_TYPE_STACK,
    ZIPC_TYPE_RING,
    ZIPC_TYPE_MAP,
    ZIPC_TYPE_CHANNEL,
    ZIPC_TYPE_POOL,
    ZIPC_TYPE_STREAM,
} zipc_type;

/* ============================================================================
 * Shared Memory Management
 * ============================================================================ */

/**
 * Create or open shared memory segment
 *
 * @param name    Shared memory name (e.g., "/mydata")
 * @param size    Size in bytes (0 to open existing)
 * @param entries Max table entries (0 for default of 64)
 * @return        Handle to shared memory or NULL on failure
 */
zipc_shm_t zipc_open(const char* name, size_t size, size_t entries);

/**
 * Close shared memory handle (does not destroy the memory)
 */
void zipc_close(zipc_shm_t shm);

/**
 * Destroy shared memory segment (unlink from system)
 */
void zipc_destroy(const char* name);

/**
 * Get raw memory access for advanced usage
 */
void* zipc_raw(zipc_shm_t shm);
size_t zipc_size(zipc_shm_t shm);

/* ============================================================================
 * Structure Views - Type-safe access to shared structures
 * ============================================================================ */

/**
 * Create a new structure in shared memory
 *
 * @param shm      Shared memory handle
 * @param name     Structure name (max 31 chars)
 * @param type     Structure type
 * @param elemsize Size of each element in bytes
 * @param capacity Number of elements
 * @return         View handle or NULL on failure
 */
zipc_view_t zipc_create(zipc_shm_t shm, const char* name,
                        zipc_type type, size_t elemsize, size_t capacity);

/**
 * Open existing structure in shared memory
 *
 * @param shm      Shared memory handle
 * @param name     Structure name
 * @param type     Expected structure type (for validation)
 * @param elemsize Expected element size (for validation)
 * @return         View handle or NULL on failure
 */
zipc_view_t zipc_get(zipc_shm_t shm, const char* name,
                     zipc_type type, size_t elemsize);

/**
 * Close a view (structure remains in shared memory)
 */
void zipc_view_close(zipc_view_t view);

/**
 * Get view properties
 */
size_t zipc_view_capacity(zipc_view_t view);
size_t zipc_view_elemsize(zipc_view_t view);
void* zipc_view_data(zipc_view_t view);

/* ============================================================================
 * Array Operations - Fixed-size contiguous storage
 * ============================================================================ */

/**
 * Array element access
 */
void* zipc_array_at(zipc_view_t array, size_t index);
zipc_result zipc_array_get(zipc_view_t array, size_t index, void* dest);
zipc_result zipc_array_set(zipc_view_t array, size_t index, const void* src);

/**
 * Atomic array operations
 */
zipc_result zipc_array_cas(zipc_view_t array, size_t index,
                           const void* expected, const void* desired);

/* ============================================================================
 * Queue Operations - Lock-free MPMC circular buffer
 * ============================================================================ */

/**
 * Queue operations
 */
zipc_result zipc_queue_push(zipc_view_t queue, const void* data);
zipc_result zipc_queue_pop(zipc_view_t queue, void* data);
bool zipc_queue_empty(zipc_view_t queue);
bool zipc_queue_full(zipc_view_t queue);
size_t zipc_queue_size(zipc_view_t queue);

/* ============================================================================
 * Stack Operations - Lock-free LIFO
 * ============================================================================ */

/**
 * Stack operations
 */
zipc_result zipc_stack_push(zipc_view_t stack, const void* data);
zipc_result zipc_stack_pop(zipc_view_t stack, void* data);
zipc_result zipc_stack_top(zipc_view_t stack, void* data);
bool zipc_stack_empty(zipc_view_t stack);
bool zipc_stack_full(zipc_view_t stack);
size_t zipc_stack_size(zipc_view_t stack);

/* ============================================================================
 * Ring Buffer Operations - Fixed-size circular buffer
 * ============================================================================ */

/**
 * Ring buffer operations
 */
zipc_result zipc_ring_write(zipc_view_t ring, const void* data, size_t count);
zipc_result zipc_ring_read(zipc_view_t ring, void* data, size_t count);
size_t zipc_ring_available(zipc_view_t ring);
size_t zipc_ring_space(zipc_view_t ring);

/* ============================================================================
 * Table Operations - Structure discovery
 * ============================================================================ */

/**
 * Iterate over all structures in shared memory
 */
typedef bool (*zipc_iter_fn)(const char* name, size_t offset, size_t size, void* ctx);
void zipc_iterate(zipc_shm_t shm, zipc_iter_fn fn, void* ctx);

/**
 * Count structures in shared memory
 */
size_t zipc_count(zipc_shm_t shm);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * Get human-readable error string
 */
const char* zipc_strerror(zipc_result result);

/**
 * Type-safe generic macros for common operations
 */
#define ZIPC_ARRAY(type) \
    struct { type* data; size_t capacity; }

#define ZIPC_QUEUE(type) \
    struct { type* data; _Atomic size_t head, tail; size_t capacity; }

/* Type-safe array access */
#define zipc_array_typed_at(array, type, index) \
    ((type*)zipc_array_at(array, index))

/* Type-safe queue operations */
#define zipc_queue_typed_push(queue, type, value) \
    ({ type _val = (value); zipc_queue_push(queue, &_val); })

#define zipc_queue_typed_pop(queue, type) \
    ({ type _val; zipc_queue_pop(queue, &_val) == ZIPC_OK ? _val : (type){0}; })

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_CORE_H */