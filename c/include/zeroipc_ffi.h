#ifndef ZEROIPC_FFI_H
#define ZEROIPC_FFI_H

/**
 * Raw FFI functions for Python/ctypes integration.
 *
 * All functions take a base pointer (the mmap'd region start) and a byte
 * offset to the structure's header. They perform C11 atomic operations
 * directly on shared memory, enabling true MPMC safety from Python.
 *
 * Return values:
 *   0 = success
 *  -1 = full (push) or empty (pop/top)
 *  -2 = elem_size mismatch
 *  -3 = invalid header (capacity/elem_size is 0)
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Queue (Vyukov bounded MPMC) */
int zeroipc_raw_queue_push(void* base, size_t offset,
                           const void* value, uint32_t elem_size);
int zeroipc_raw_queue_pop(void* base, size_t offset,
                          void* value_out, uint32_t elem_size);
uint32_t zeroipc_raw_queue_size(void* base, size_t offset);
int zeroipc_raw_queue_empty(void* base, size_t offset);
int zeroipc_raw_queue_full(void* base, size_t offset);

/* Stack (4-state CAS) */
int zeroipc_raw_stack_push(void* base, size_t offset,
                           const void* value, uint32_t elem_size);
int zeroipc_raw_stack_pop(void* base, size_t offset,
                          void* value_out, uint32_t elem_size);
int zeroipc_raw_stack_top(void* base, size_t offset,
                          void* value_out, uint32_t elem_size);
uint32_t zeroipc_raw_stack_size(void* base, size_t offset);
int zeroipc_raw_stack_empty(void* base, size_t offset);
int zeroipc_raw_stack_full(void* base, size_t offset);

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_FFI_H */
