#include "zeroipc.h"
#include <string.h>
#include <stddef.h>

#define MAX_NAME_SIZE 32

/* Table header structure */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t next_offset;
    uint32_t max_entries;
} table_header_t;

/* Table entry structure */
typedef struct {
    char name[MAX_NAME_SIZE];
    uint32_t offset;
    uint32_t size;
} table_entry_t;

/* Internal: Get header */
static table_header_t* get_header(zeroipc_memory_t* mem) {
    return (table_header_t*)zeroipc_memory_base(mem);
}

/* Internal: Get entries */
static table_entry_t* get_entries(zeroipc_memory_t* mem) {
    char* base = (char*)zeroipc_memory_base(mem);
    return (table_entry_t*)(base + sizeof(table_header_t));
}

/* Add entry to table */
int zeroipc_table_add(zeroipc_memory_t* mem, const char* name, size_t size, size_t* offset) {
    if (!mem || !name || size == 0) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    if (strlen(name) >= MAX_NAME_SIZE) {
        return ZEROIPC_ERROR_NAME_TOO_LONG;
    }
    
    table_header_t* header = get_header(mem);
    table_entry_t* entries = get_entries(mem);
    
    /* Check if name already exists */
    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            return ZEROIPC_ERROR_ALREADY_EXISTS;
        }
    }
    
    /* Check if table is full */
    if (header->entry_count >= header->max_entries) {
        return ZEROIPC_ERROR_TABLE_FULL;
    }
    
    /* Check if enough space */
    if (header->next_offset + size > zeroipc_memory_size(mem)) {
        return ZEROIPC_ERROR_SIZE;
    }
    
    /* Add entry */
    table_entry_t* entry = &entries[header->entry_count];
    strncpy(entry->name, name, MAX_NAME_SIZE - 1);
    entry->name[MAX_NAME_SIZE - 1] = '\0';
    entry->offset = header->next_offset;
    entry->size = size;
    
    /* Update header */
    if (offset) {
        *offset = header->next_offset;
    }
    header->next_offset += size;
    header->entry_count++;
    
    return ZEROIPC_OK;
}

/* Find entry in table */
int zeroipc_table_find(zeroipc_memory_t* mem, const char* name, size_t* offset, size_t* size) {
    if (!mem || !name) {
        return ZEROIPC_ERROR_NOT_FOUND;
    }
    
    table_header_t* header = get_header(mem);
    table_entry_t* entries = get_entries(mem);
    
    /* Search for entry */
    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            if (offset) {
                *offset = entries[i].offset;
            }
            if (size) {
                *size = entries[i].size;
            }
            return ZEROIPC_OK;
        }
    }
    
    return ZEROIPC_ERROR_NOT_FOUND;
}

/* Remove entry from table */
int zeroipc_table_remove(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) {
        return ZEROIPC_ERROR_NOT_FOUND;
    }
    
    table_header_t* header = get_header(mem);
    table_entry_t* entries = get_entries(mem);
    
    /* Find entry */
    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (strcmp(entries[i].name, name) == 0) {
            /* Move remaining entries */
            for (uint32_t j = i; j < header->entry_count - 1; j++) {
                entries[j] = entries[j + 1];
            }
            header->entry_count--;
            return ZEROIPC_OK;
        }
    }
    
    return ZEROIPC_ERROR_NOT_FOUND;
}

/* Get entry count */
size_t zeroipc_table_count(zeroipc_memory_t* mem) {
    if (!mem) {
        return 0;
    }
    
    table_header_t* header = get_header(mem);
    return header->entry_count;
}