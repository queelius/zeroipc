#define _GNU_SOURCE
#include "zeroipc.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ZEROIPC_MAGIC 0x5A49504D  /* 'ZIPM' */
#define ZEROIPC_VERSION 1
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

/* Memory structure */
struct zeroipc_memory {
    void* base;
    size_t size;
    int fd;
    char* name;
    size_t max_entries;
    int last_error;
};

/* Calculate table size */
static size_t calculate_table_size(size_t max_entries) {
    return sizeof(table_header_t) + (sizeof(table_entry_t) * max_entries);
}

/* Get table header */
static table_header_t* get_header(zeroipc_memory_t* mem) {
    return (table_header_t*)mem->base;
}

/* Get table entries - currently unused but kept for future use */
__attribute__((unused))
static table_entry_t* get_entries(zeroipc_memory_t* mem) {
    return (table_entry_t*)((char*)mem->base + sizeof(table_header_t));
}

/* Initialize table */
static void init_table(zeroipc_memory_t* mem) {
    table_header_t* header = get_header(mem);
    header->magic = ZEROIPC_MAGIC;
    header->version = ZEROIPC_VERSION;
    header->entry_count = 0;
    header->next_offset = calculate_table_size(mem->max_entries);
    header->max_entries = mem->max_entries;
}

/* Create or open shared memory */
zeroipc_memory_t* zeroipc_memory_create(const char* name, size_t size, size_t max_entries) {
    if (!name || size == 0 || max_entries == 0) {
        return NULL;
    }
    
    zeroipc_memory_t* mem = calloc(1, sizeof(zeroipc_memory_t));
    if (!mem) {
        return NULL;
    }
    
    mem->name = strdup(name);
    mem->size = size;
    mem->max_entries = max_entries;
    
    /* Open shared memory */
    mem->fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (mem->fd == -1) {
        mem->last_error = ZEROIPC_ERROR_OPEN;
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Set size */
    if (ftruncate(mem->fd, size) == -1) {
        mem->last_error = ZEROIPC_ERROR_SIZE;
        close(mem->fd);
        shm_unlink(name);
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Map memory */
    mem->base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem->fd, 0);
    if (mem->base == MAP_FAILED) {
        mem->last_error = ZEROIPC_ERROR_MMAP;
        close(mem->fd);
        shm_unlink(name);
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Initialize table */
    init_table(mem);
    
    return mem;
}

/* Open existing shared memory */
zeroipc_memory_t* zeroipc_memory_open(const char* name) {
    if (!name) {
        return NULL;
    }
    
    zeroipc_memory_t* mem = calloc(1, sizeof(zeroipc_memory_t));
    if (!mem) {
        return NULL;
    }
    
    mem->name = strdup(name);
    
    /* Open shared memory */
    mem->fd = shm_open(name, O_RDWR, 0666);
    if (mem->fd == -1) {
        mem->last_error = ZEROIPC_ERROR_OPEN;
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Get size */
    struct stat st;
    if (fstat(mem->fd, &st) == -1) {
        mem->last_error = ZEROIPC_ERROR_SIZE;
        close(mem->fd);
        free(mem->name);
        free(mem);
        return NULL;
    }
    mem->size = st.st_size;
    
    /* Map memory */
    mem->base = mmap(NULL, mem->size, PROT_READ | PROT_WRITE, MAP_SHARED, mem->fd, 0);
    if (mem->base == MAP_FAILED) {
        mem->last_error = ZEROIPC_ERROR_MMAP;
        close(mem->fd);
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Verify magic and version */
    table_header_t* header = get_header(mem);
    if (header->magic != ZEROIPC_MAGIC) {
        mem->last_error = ZEROIPC_ERROR_INVALID_MAGIC;
        munmap(mem->base, mem->size);
        close(mem->fd);
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    if (header->version != ZEROIPC_VERSION) {
        mem->last_error = ZEROIPC_ERROR_VERSION_MISMATCH;
        munmap(mem->base, mem->size);
        close(mem->fd);
        free(mem->name);
        free(mem);
        return NULL;
    }
    
    /* Get max_entries from header */
    mem->max_entries = header->max_entries;
    
    return mem;
}

/* Close memory */
void zeroipc_memory_close(zeroipc_memory_t* mem) {
    if (!mem) return;
    
    if (mem->base && mem->base != MAP_FAILED) {
        munmap(mem->base, mem->size);
    }
    
    if (mem->fd != -1) {
        close(mem->fd);
    }
    
    free(mem->name);
    free(mem);
}

/* Unlink shared memory */
void zeroipc_memory_unlink(const char* name) {
    if (name) {
        shm_unlink(name);
    }
}

/* Get base pointer */
void* zeroipc_memory_base(zeroipc_memory_t* mem) {
    return mem ? mem->base : NULL;
}

/* Get size */
size_t zeroipc_memory_size(zeroipc_memory_t* mem) {
    return mem ? mem->size : 0;
}

/* Get last error */
int zeroipc_memory_error(zeroipc_memory_t* mem) {
    return mem ? mem->last_error : ZEROIPC_ERROR_OPEN;
}