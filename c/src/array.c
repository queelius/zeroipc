#include "zeroipc.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Array structure */
struct zeroipc_array {
    zeroipc_memory_t* memory;
    void* data;
    size_t capacity;
    size_t elem_size;
    char name[32];
};

/* Create array */
zeroipc_array_t* zeroipc_array_create(zeroipc_memory_t* mem, const char* name, 
                                       size_t elem_size, size_t capacity) {
    if (!mem || !name || elem_size == 0 || capacity == 0) {
        return NULL;
    }
    
    /* Allocate array structure */
    zeroipc_array_t* array = calloc(1, sizeof(zeroipc_array_t));
    if (!array) {
        return NULL;
    }
    
    array->memory = mem;
    array->elem_size = elem_size;
    array->capacity = capacity;
    strncpy(array->name, name, sizeof(array->name) - 1);
    
    /* Calculate total size with overflow check */
    if (capacity > SIZE_MAX / elem_size) {
        free(array);
        return NULL;  // Would overflow
    }
    size_t total_size = elem_size * capacity;
    
    /* Add to table and get offset */
    size_t offset;
    int result = zeroipc_table_add(mem, name, total_size, &offset);
    if (result != ZEROIPC_OK) {
        free(array);
        return NULL;
    }
    
    /* Get data pointer */
    array->data = (char*)zeroipc_memory_base(mem) + offset;
    
    /* Initialize to zero */
    memset(array->data, 0, total_size);
    
    return array;
}

/* Open existing array */
zeroipc_array_t* zeroipc_array_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) {
        return NULL;
    }
    
    /* Find in table */
    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) {
        return NULL;
    }
    
    /* Allocate array structure */
    zeroipc_array_t* array = calloc(1, sizeof(zeroipc_array_t));
    if (!array) {
        return NULL;
    }
    
    array->memory = mem;
    strncpy(array->name, name, sizeof(array->name) - 1);
    
    /* Get data pointer */
    array->data = (char*)zeroipc_memory_base(mem) + offset;
    
    /* Note: We don't know elem_size and capacity from metadata alone
       User must know these or we need to store them separately */
    /* For now, we'll store total size and let user specify elem_size */
    array->elem_size = 0;  /* Unknown */
    array->capacity = 0;   /* Unknown */
    
    return array;
}

/* Close array */
void zeroipc_array_close(zeroipc_array_t* array) {
    if (array) {
        free(array);
    }
}

/* Get data pointer */
void* zeroipc_array_data(zeroipc_array_t* array) {
    return array ? array->data : NULL;
}

/* Get capacity */
size_t zeroipc_array_capacity(zeroipc_array_t* array) {
    return array ? array->capacity : 0;
}

/* Get element size */
size_t zeroipc_array_elem_size(zeroipc_array_t* array) {
    return array ? array->elem_size : 0;
}

/* Get element at index */
void* zeroipc_array_get(zeroipc_array_t* array, size_t index) {
    if (!array || index >= array->capacity) {
        return NULL;
    }
    
    return (char*)array->data + (index * array->elem_size);
}

/* Set element at index */
int zeroipc_array_set(zeroipc_array_t* array, size_t index, const void* value) {
    if (!array || !value || index >= array->capacity) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    void* dest = (char*)array->data + (index * array->elem_size);
    memcpy(dest, value, array->elem_size);
    
    return ZEROIPC_OK;
}