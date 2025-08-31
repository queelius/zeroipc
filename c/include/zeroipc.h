#ifndef ZEROIPC_H
#define ZEROIPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
    ZEROIPC_OK = 0,
    ZEROIPC_ERROR_OPEN = -1,
    ZEROIPC_ERROR_MMAP = -2,
    ZEROIPC_ERROR_SIZE = -3,
    ZEROIPC_ERROR_NOT_FOUND = -4,
    ZEROIPC_ERROR_TABLE_FULL = -5,
    ZEROIPC_ERROR_NAME_TOO_LONG = -6,
    ZEROIPC_ERROR_INVALID_MAGIC = -7,
    ZEROIPC_ERROR_VERSION_MISMATCH = -8,
    ZEROIPC_ERROR_ALREADY_EXISTS = -9
} zeroipc_error_t;

/* Forward declarations */
typedef struct zeroipc_memory zeroipc_memory_t;
typedef struct zeroipc_array zeroipc_array_t;

/* Memory management */
zeroipc_memory_t* zeroipc_memory_create(const char* name, size_t size, size_t max_entries);
zeroipc_memory_t* zeroipc_memory_open(const char* name);
void zeroipc_memory_close(zeroipc_memory_t* mem);
void zeroipc_memory_unlink(const char* name);
void* zeroipc_memory_base(zeroipc_memory_t* mem);
size_t zeroipc_memory_size(zeroipc_memory_t* mem);
int zeroipc_memory_error(zeroipc_memory_t* mem);

/* Table operations */
int zeroipc_table_add(zeroipc_memory_t* mem, const char* name, size_t size, size_t* offset);
int zeroipc_table_find(zeroipc_memory_t* mem, const char* name, size_t* offset, size_t* size);
int zeroipc_table_remove(zeroipc_memory_t* mem, const char* name);
size_t zeroipc_table_count(zeroipc_memory_t* mem);

/* Array operations */
zeroipc_array_t* zeroipc_array_create(zeroipc_memory_t* mem, const char* name, 
                                       size_t elem_size, size_t capacity);
zeroipc_array_t* zeroipc_array_open(zeroipc_memory_t* mem, const char* name);
void zeroipc_array_close(zeroipc_array_t* array);
void* zeroipc_array_data(zeroipc_array_t* array);
size_t zeroipc_array_capacity(zeroipc_array_t* array);
size_t zeroipc_array_elem_size(zeroipc_array_t* array);
void* zeroipc_array_get(zeroipc_array_t* array, size_t index);
int zeroipc_array_set(zeroipc_array_t* array, size_t index, const void* value);

/* Utility functions */
const char* zeroipc_error_string(zeroipc_error_t error);

/* Include Queue and Stack headers */
#include "zeroipc_queue.h"
#include "zeroipc_stack.h"

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_H */