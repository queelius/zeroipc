/**
 * ZeroIPC Core Implementation
 *
 * Elegant, composable implementation following Unix philosophy
 */

#define _GNU_SOURCE
#include "zeroipc_core.h"
#include "table_layout.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ============================================================================
 * Internal Constants and Types
 * ============================================================================ */

#define ZIPC_MAGIC      0x5A49504D  /* 'ZIPM' */
#define ZIPC_VERSION    1
#define MAX_NAME_SIZE   32
#define DEFAULT_ENTRIES 64

/* Shared memory handle */
struct zipc_shm {
    void*   base;
    size_t  size;
    int     fd;
    char*   name;
    bool    owner;
};

/* View handle for type-safe structure access */
struct zipc_view {
    zipc_shm_t    shm;
    void*         data;
    zipc_type     type;
    size_t        capacity;
    size_t        elemsize;
    char          name[MAX_NAME_SIZE];
};

/* Structure headers for different types */
typedef struct {
    uint64_t capacity;
} array_header_t;

typedef struct {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    uint32_t capacity;
    uint32_t elem_size;
} queue_header_t;

/* Per-slot state values for stack 4-state CAS protocol */
#define SLOT_EMPTY   0
#define SLOT_WRITING 1
#define SLOT_READY   2
#define SLOT_READING 3

typedef struct {
    _Atomic int32_t top;        /* index of top element, -1 when empty */
    uint32_t capacity;
    uint32_t elem_size;
} stack_header_t;

typedef struct {
    _Atomic uint32_t write_pos;
    _Atomic uint32_t read_pos;
    uint32_t capacity;
    uint32_t elem_size;
} ring_header_t;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

static inline zipc_table_header_t* get_header(zipc_shm_t shm) {
    return (zipc_table_header_t*)shm->base;
}

static inline zipc_table_entry_t* get_entries(zipc_shm_t shm) {
    return (zipc_table_entry_t*)((char*)shm->base + sizeof(zipc_table_header_t));
}

static size_t calculate_table_size(size_t entries) {
    if (entries == 0) entries = DEFAULT_ENTRIES;
    return sizeof(zipc_table_header_t) + entries * sizeof(zipc_table_entry_t);
}

/* Align offset to 8-byte boundary */
static size_t align_offset(size_t offset) {
    return (offset + 7) & ~7;
}

/* ============================================================================
 * Shared Memory Management
 * ============================================================================ */

zipc_shm_t zipc_open(const char* name, size_t size, size_t entries) {
    if (!name) return NULL;

    zipc_shm_t shm = calloc(1, sizeof(*shm));
    if (!shm) return NULL;

    shm->name = strdup(name);
    if (!shm->name) {
        free(shm);
        return NULL;
    }

    if (size > 0) {
        /* Create new shared memory */
        shm->owner = true;
        shm->size = size;

        /* Ensure size is large enough for table */
        size_t table_size = calculate_table_size(entries);
        if (shm->size < table_size) {
            shm->size = table_size;
        }

        /* Create shared memory */
        shm->fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm->fd == -1) {
            /* Try without O_EXCL in case it exists */
            shm->fd = shm_open(name, O_CREAT | O_RDWR, 0666);
            if (shm->fd == -1) goto error;
        }

        /* Set size */
        if (ftruncate(shm->fd, shm->size) == -1) goto error;

        /* Map memory */
        shm->base = mmap(NULL, shm->size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
        if (shm->base == MAP_FAILED) goto error;

        /* Initialize table if we created it */
        zipc_table_header_t* header = get_header(shm);
        if (header->magic != ZIPC_MAGIC) {
            memset(shm->base, 0, table_size);
            header->magic = ZIPC_MAGIC;
            header->version = ZIPC_VERSION;
            header->entry_count = 0;
            header->max_entries = entries ? entries : DEFAULT_ENTRIES;
            header->memory_size = shm->size;
            header->next_offset = table_size;
        }
    } else {
        /* Open existing shared memory */
        shm->owner = false;

        shm->fd = shm_open(name, O_RDWR, 0666);
        if (shm->fd == -1) goto error;

        /* Get size */
        struct stat st;
        if (fstat(shm->fd, &st) == -1) goto error;
        shm->size = st.st_size;

        /* Map memory */
        shm->base = mmap(NULL, shm->size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->fd, 0);
        if (shm->base == MAP_FAILED) goto error;

        /* Validate magic */
        zipc_table_header_t* header = get_header(shm);
        if (header->magic != ZIPC_MAGIC) goto error;

        /* C++/Go write 0 for max_entries; use DEFAULT_ENTRIES in that case */
        if (header->max_entries == 0) {
            header->max_entries = DEFAULT_ENTRIES;
        }
    }

    return shm;

error:
    if (shm->base && shm->base != MAP_FAILED) munmap(shm->base, shm->size);
    if (shm->fd >= 0) close(shm->fd);
    free(shm->name);
    free(shm);
    return NULL;
}

void zipc_close(zipc_shm_t shm) {
    if (!shm) return;

    if (shm->base) munmap(shm->base, shm->size);
    if (shm->fd >= 0) close(shm->fd);
    free(shm->name);
    free(shm);
}

void zipc_destroy(const char* name) {
    if (name) shm_unlink(name);
}

void* zipc_raw(zipc_shm_t shm) {
    return shm ? shm->base : NULL;
}

size_t zipc_size(zipc_shm_t shm) {
    return shm ? shm->size : 0;
}

/* ============================================================================
 * Table Management
 * ============================================================================ */

static zipc_table_entry_t* find_entry(zipc_shm_t shm, const char* name) {
    if (!shm || !name) return NULL;

    zipc_table_header_t* header = get_header(shm);
    zipc_table_entry_t* entries = get_entries(shm);

    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (strncmp(entries[i].name, name, MAX_NAME_SIZE - 1) == 0) {
            return &entries[i];
        }
    }

    return NULL;
}

static size_t allocate_space(zipc_shm_t shm, const char* name, size_t size) {
    if (!shm || !name || size == 0) return 0;

    zipc_table_header_t* header = get_header(shm);

    /* Check if name already exists */
    if (find_entry(shm, name)) return 0;

    /* Check if table is full */
    if (header->entry_count >= header->max_entries) return 0;

    /* Align offset */
    size_t offset = align_offset((size_t)header->next_offset);

    /* Check if there's enough space */
    if (offset + size > shm->size) return 0;

    /* Add entry */
    zipc_table_entry_t* entries = get_entries(shm);
    zipc_table_entry_t* entry = &entries[header->entry_count];

    strncpy(entry->name, name, MAX_NAME_SIZE - 1);
    entry->name[MAX_NAME_SIZE - 1] = '\0';
    entry->offset = offset;
    entry->size = size;

    /* Update header */
    header->entry_count++;
    header->next_offset = offset + size;

    return offset;
}

/* ============================================================================
 * Structure Views
 * ============================================================================ */

zipc_view_t zipc_create(zipc_shm_t shm, const char* name,
                        zipc_type type, size_t elemsize, size_t capacity) {
    if (!shm || !name || elemsize == 0 || capacity == 0) return NULL;
    if (strlen(name) >= MAX_NAME_SIZE) return NULL;

    /* Calculate total size based on type */
    size_t header_size = 0;
    switch (type) {
        case ZIPC_TYPE_ARRAY:
            header_size = sizeof(array_header_t);
            break;
        case ZIPC_TYPE_QUEUE:
            header_size = sizeof(queue_header_t);
            break;
        case ZIPC_TYPE_STACK:
            header_size = sizeof(stack_header_t);
            break;
        case ZIPC_TYPE_RING:
            header_size = sizeof(ring_header_t);
            break;
        default:
            return NULL;
    }

    /* Queue and stack need extra space for per-slot metadata arrays */
    size_t extra_per_slot = 0;
    if (type == ZIPC_TYPE_QUEUE || type == ZIPC_TYPE_STACK) {
        extra_per_slot = sizeof(uint32_t);  /* sequence (queue) or state (stack) */
    }

    /* Check for overflow */
    if (capacity > (SIZE_MAX - header_size) / (elemsize + extra_per_slot)) return NULL;

    size_t total_size = header_size + (elemsize + extra_per_slot) * capacity;
    size_t offset = allocate_space(shm, name, total_size);
    if (offset == 0) return NULL;

    /* Create view */
    zipc_view_t view = calloc(1, sizeof(*view));
    if (!view) return NULL;

    view->shm = shm;
    view->type = type;
    view->capacity = capacity;
    view->elemsize = elemsize;
    strcpy(view->name, name);

    /* Initialize header based on type */
    void* header = (char*)shm->base + offset;
    view->data = (char*)header + header_size;

    switch (type) {
        case ZIPC_TYPE_ARRAY: {
            array_header_t* ah = (array_header_t*)header;
            ah->capacity = capacity;
            break;
        }
        case ZIPC_TYPE_QUEUE: {
            queue_header_t* qh = (queue_header_t*)header;
            atomic_store(&qh->head, 0);
            atomic_store(&qh->tail, 0);
            qh->capacity = capacity;
            qh->elem_size = elemsize;
            /* Initialize per-slot sequence numbers: seq[i] = i
             * Layout: [header][data: T * capacity][seq: uint32 * capacity] */
            _Atomic uint32_t* seq = (_Atomic uint32_t*)(
                (char*)view->data + elemsize * capacity);
            for (size_t i = 0; i < capacity; i++) {
                atomic_store_explicit(&seq[i], (uint32_t)i, memory_order_relaxed);
            }
            break;
        }
        case ZIPC_TYPE_STACK: {
            stack_header_t* sh = (stack_header_t*)header;
            atomic_store(&sh->top, -1);  /* -1 = empty (signed) */
            sh->capacity = capacity;
            sh->elem_size = elemsize;
            /* Initialize per-slot state array to SLOT_EMPTY(0)
             * Layout: [header][data: T * capacity][state: uint32 * capacity] */
            _Atomic uint32_t* state = (_Atomic uint32_t*)(
                (char*)view->data + elemsize * capacity);
            for (size_t i = 0; i < capacity; i++) {
                atomic_store_explicit(&state[i], SLOT_EMPTY, memory_order_relaxed);
            }
            break;
        }
        case ZIPC_TYPE_RING: {
            ring_header_t* rh = (ring_header_t*)header;
            atomic_store(&rh->write_pos, 0);
            atomic_store(&rh->read_pos, 0);
            rh->capacity = capacity;
            rh->elem_size = elemsize;
            break;
        }
        default:
            free(view);
            return NULL;
    }

    return view;
}

zipc_view_t zipc_get(zipc_shm_t shm, const char* name,
                    zipc_type type, size_t elemsize) {
    if (!shm || !name) return NULL;

    zipc_table_entry_t* entry = find_entry(shm, name);
    if (!entry) return NULL;

    /* Create view */
    zipc_view_t view = calloc(1, sizeof(*view));
    if (!view) return NULL;

    view->shm = shm;
    view->type = type;
    view->elemsize = elemsize;
    strcpy(view->name, name);

    /* Get header and validate */
    void* header = (char*)shm->base + entry->offset;

    switch (type) {
        case ZIPC_TYPE_ARRAY: {
            array_header_t* ah = (array_header_t*)header;
            view->capacity = ah->capacity;
            view->data = (char*)header + sizeof(array_header_t);
            break;
        }
        case ZIPC_TYPE_QUEUE: {
            queue_header_t* qh = (queue_header_t*)header;
            if (qh->elem_size != elemsize) goto error;
            view->capacity = qh->capacity;
            view->data = (char*)header + sizeof(queue_header_t);
            break;
        }
        case ZIPC_TYPE_STACK: {
            stack_header_t* sh = (stack_header_t*)header;
            if (sh->elem_size != elemsize) goto error;
            view->capacity = sh->capacity;
            view->data = (char*)header + sizeof(stack_header_t);
            break;
        }
        case ZIPC_TYPE_RING: {
            ring_header_t* rh = (ring_header_t*)header;
            if (rh->elem_size != elemsize) goto error;
            view->capacity = rh->capacity;
            view->data = (char*)header + sizeof(ring_header_t);
            break;
        }
        default:
            goto error;
    }

    return view;

error:
    free(view);
    return NULL;
}

void zipc_view_close(zipc_view_t view) {
    free(view);
}

size_t zipc_view_capacity(zipc_view_t view) {
    return view ? view->capacity : 0;
}

size_t zipc_view_elemsize(zipc_view_t view) {
    return view ? view->elemsize : 0;
}

void* zipc_view_data(zipc_view_t view) {
    return view ? view->data : NULL;
}

/* ============================================================================
 * Array Operations
 * ============================================================================ */

void* zipc_array_at(zipc_view_t array, size_t index) {
    if (!array || array->type != ZIPC_TYPE_ARRAY) return NULL;
    if (index >= array->capacity) return NULL;

    return (char*)array->data + index * array->elemsize;
}

zipc_result zipc_array_get(zipc_view_t array, size_t index, void* dest) {
    void* src = zipc_array_at(array, index);
    if (!src || !dest) return ZIPC_INVALID;

    memcpy(dest, src, array->elemsize);
    return ZIPC_OK;
}

zipc_result zipc_array_set(zipc_view_t array, size_t index, const void* src) {
    void* dest = zipc_array_at(array, index);
    if (!dest || !src) return ZIPC_INVALID;

    memcpy(dest, src, array->elemsize);
    return ZIPC_OK;
}

/* ============================================================================
 * Queue Operations (Lock-free MPMC)
 * ============================================================================ */

static queue_header_t* get_queue_header(zipc_view_t queue) {
    if (!queue || queue->type != ZIPC_TYPE_QUEUE) return NULL;
    return (queue_header_t*)((char*)queue->data - sizeof(queue_header_t));
}

/* Get pointer to sequence array (after data) */
static _Atomic uint32_t* get_queue_seq(zipc_view_t queue) {
    queue_header_t* header = get_queue_header(queue);
    return (_Atomic uint32_t*)(
        (char*)queue->data + queue->elemsize * header->capacity);
}

zipc_result zipc_queue_push(zipc_view_t queue, const void* data) {
    if (!queue || !data) return ZIPC_INVALID;

    queue_header_t* header = get_queue_header(queue);
    if (!header) return ZIPC_INVALID;

    _Atomic uint32_t* seq = get_queue_seq(queue);
    uint32_t cap = header->capacity;

    for (;;) {
        uint32_t tail = atomic_load_explicit(&header->tail, memory_order_relaxed);
        uint32_t slot = tail % cap;
        uint32_t s = atomic_load_explicit(&seq[slot], memory_order_acquire);
        int32_t diff = (int32_t)s - (int32_t)tail;

        if (diff == 0) {
            /* Slot ready for writing — try to claim it */
            if (atomic_compare_exchange_weak_explicit(
                    &header->tail, &tail, tail + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                void* dest = (char*)queue->data + slot * queue->elemsize;
                memcpy(dest, data, queue->elemsize);
                /* Publish: seq = tail + 1 */
                atomic_store_explicit(&seq[slot], tail + 1, memory_order_release);
                return ZIPC_OK;
            }
        } else if (diff < 0) {
            return ZIPC_FULL;
        }
        /* diff > 0: another producer claimed this slot; retry */
    }
}

zipc_result zipc_queue_pop(zipc_view_t queue, void* data) {
    if (!queue || !data) return ZIPC_INVALID;

    queue_header_t* header = get_queue_header(queue);
    if (!header) return ZIPC_INVALID;

    _Atomic uint32_t* seq = get_queue_seq(queue);
    uint32_t cap = header->capacity;

    for (;;) {
        uint32_t head = atomic_load_explicit(&header->head, memory_order_relaxed);
        uint32_t slot = head % cap;
        uint32_t s = atomic_load_explicit(&seq[slot], memory_order_acquire);
        int32_t diff = (int32_t)s - (int32_t)(head + 1);

        if (diff == 0) {
            /* Slot contains data — try to claim it */
            if (atomic_compare_exchange_weak_explicit(
                    &header->head, &head, head + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                void* src = (char*)queue->data + slot * queue->elemsize;
                memcpy(data, src, queue->elemsize);
                /* Release: seq = head + capacity */
                atomic_store_explicit(&seq[slot], head + cap, memory_order_release);
                return ZIPC_OK;
            }
        } else if (diff < 0) {
            return ZIPC_EMPTY;
        }
        /* diff > 0: another consumer claimed this slot; retry */
    }
}

bool zipc_queue_empty(zipc_view_t queue) {
    queue_header_t* header = get_queue_header(queue);
    if (!header) return true;

    return atomic_load(&header->head) == atomic_load(&header->tail);
}

bool zipc_queue_full(zipc_view_t queue) {
    queue_header_t* header = get_queue_header(queue);
    if (!header) return true;

    uint32_t head = atomic_load(&header->head);
    uint32_t tail = atomic_load(&header->tail);
    return (tail - head) >= header->capacity;
}

size_t zipc_queue_size(zipc_view_t queue) {
    queue_header_t* header = get_queue_header(queue);
    if (!header) return 0;

    uint32_t head = atomic_load(&header->head);
    uint32_t tail = atomic_load(&header->tail);
    /* uint32_t subtraction handles wraparound correctly */
    return (size_t)(tail - head);
}

/* ============================================================================
 * Stack Operations (Lock-free LIFO)
 * ============================================================================ */

static stack_header_t* get_stack_header(zipc_view_t stack) {
    if (!stack || stack->type != ZIPC_TYPE_STACK) return NULL;
    return (stack_header_t*)((char*)stack->data - sizeof(stack_header_t));
}

/* Get pointer to per-slot state array (after data) */
static _Atomic uint32_t* get_stack_state(zipc_view_t stack) {
    stack_header_t* header = get_stack_header(stack);
    return (_Atomic uint32_t*)(
        (char*)stack->data + stack->elemsize * header->capacity);
}

zipc_result zipc_stack_push(zipc_view_t stack, const void* data) {
    if (!stack || !data) return ZIPC_INVALID;

    stack_header_t* header = get_stack_header(stack);
    if (!header) return ZIPC_INVALID;

    _Atomic uint32_t* state = get_stack_state(stack);
    int32_t current_top, new_top;

    /* Step 1: Reserve a slot by CAS-advancing top */
    do {
        current_top = atomic_load_explicit(&header->top, memory_order_relaxed);
        if (current_top >= (int32_t)(header->capacity - 1)) {
            return ZIPC_FULL;
        }
        new_top = current_top + 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot state EMPTY -> WRITING */
    uint32_t expected = SLOT_EMPTY;
    while (!atomic_compare_exchange_weak_explicit(
                &state[new_top], &expected, SLOT_WRITING,
                memory_order_acq_rel, memory_order_relaxed)) {
        expected = SLOT_EMPTY;
        /* Yield to avoid busy-spin */
    }

    /* Step 3: Write data (exclusive ownership) */
    void* slot = (char*)stack->data + new_top * stack->elemsize;
    memcpy(slot, data, stack->elemsize);

    /* Step 4: Publish: WRITING -> READY */
    atomic_store_explicit(&state[new_top], SLOT_READY, memory_order_release);

    return ZIPC_OK;
}

zipc_result zipc_stack_pop(zipc_view_t stack, void* data) {
    if (!stack || !data) return ZIPC_INVALID;

    stack_header_t* header = get_stack_header(stack);
    if (!header) return ZIPC_INVALID;

    _Atomic uint32_t* state = get_stack_state(stack);
    int32_t current_top, new_top;

    /* Step 1: Reserve a slot by CAS-decrementing top */
    do {
        current_top = atomic_load_explicit(&header->top, memory_order_relaxed);
        if (current_top < 0) {
            return ZIPC_EMPTY;
        }
        new_top = current_top - 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot state READY -> READING */
    uint32_t expected = SLOT_READY;
    while (!atomic_compare_exchange_weak_explicit(
                &state[current_top], &expected, SLOT_READING,
                memory_order_acq_rel, memory_order_relaxed)) {
        expected = SLOT_READY;
        /* Yield to avoid busy-spin */
    }

    /* Step 3: Read data (exclusive ownership) */
    void* slot = (char*)stack->data + current_top * stack->elemsize;
    memcpy(data, slot, stack->elemsize);

    /* Step 4: Release: READING -> EMPTY */
    atomic_store_explicit(&state[current_top], SLOT_EMPTY, memory_order_release);

    return ZIPC_OK;
}

zipc_result zipc_stack_top(zipc_view_t stack, void* data) {
    if (!stack || !data) return ZIPC_INVALID;

    stack_header_t* header = get_stack_header(stack);
    if (!header) return ZIPC_INVALID;

    _Atomic uint32_t* state = get_stack_state(stack);
    int32_t top = atomic_load(&header->top);
    if (top < 0) return ZIPC_EMPTY;

    /* Bounded spin: bail out if slot never becomes READY or top changes */
    for (int spins = 0; spins < 10000; ++spins) {
        if (atomic_load_explicit(&state[top], memory_order_acquire) == SLOT_READY) {
            void* slot = (char*)stack->data + top * stack->elemsize;
            memcpy(data, slot, stack->elemsize);
            return ZIPC_OK;
        }
        if (atomic_load_explicit(&header->top, memory_order_acquire) != top) {
            return ZIPC_EMPTY;
        }
    }
    return ZIPC_EMPTY;
}

bool zipc_stack_empty(zipc_view_t stack) {
    stack_header_t* header = get_stack_header(stack);
    return !header || atomic_load(&header->top) < 0;
}

bool zipc_stack_full(zipc_view_t stack) {
    stack_header_t* header = get_stack_header(stack);
    return !header || atomic_load(&header->top) >= (int32_t)(header->capacity - 1);
}

size_t zipc_stack_size(zipc_view_t stack) {
    stack_header_t* header = get_stack_header(stack);
    if (!header) return 0;
    int32_t top = atomic_load(&header->top);
    return top < 0 ? 0 : (size_t)(top + 1);
}

/* ============================================================================
 * Table Iteration
 * ============================================================================ */

void zipc_iterate(zipc_shm_t shm, zipc_iter_fn fn, void* ctx) {
    if (!shm || !fn) return;

    zipc_table_header_t* header = get_header(shm);
    zipc_table_entry_t* entries = get_entries(shm);

    for (uint32_t i = 0; i < header->entry_count; i++) {
        if (!fn(entries[i].name, (size_t)entries[i].offset, (size_t)entries[i].size, ctx)) {
            break;
        }
    }
}

size_t zipc_count(zipc_shm_t shm) {
    if (!shm) return 0;
    zipc_table_header_t* header = get_header(shm);
    return header->entry_count;
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char* zipc_strerror(zipc_result result) {
    switch (result) {
        case ZIPC_OK:         return "Success";
        case ZIPC_ERROR:      return "General error";
        case ZIPC_NOT_FOUND:  return "Not found";
        case ZIPC_EXISTS:     return "Already exists";
        case ZIPC_FULL:       return "Full";
        case ZIPC_EMPTY:      return "Empty";
        case ZIPC_INVALID:    return "Invalid argument";
        case ZIPC_NO_MEMORY:  return "Out of memory";
        case ZIPC_IO_ERROR:   return "I/O error";
        default:              return "Unknown error";
    }
}