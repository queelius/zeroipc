#define _GNU_SOURCE
#include "zeroipc.h"
#include "zeroipc_stack.h"
#include <sched.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/*
 * Stack binary layout (matches C++/Go/Python, format v2):
 *   [top:i32][capacity:u32][elem_size:u32][reserved:u32]  (16 bytes)
 *   [data[0]][data[1]]...[data[cap-1]]
 *   [pad to 8-byte boundary]
 *   [state[0]:u32][state[1]:u32]...[state[cap-1]:u32]
 *
 * top is signed int32: -1 = empty, otherwise index of top element.
 * Per-slot 4-state CAS protocol: EMPTY(0) -> WRITING(1) -> READY(2) -> READING(3) -> EMPTY(0)
 * The 16-byte header makes the data array 8-aligned; the state array is placed
 * on the next 8-byte boundary so its atomics are always naturally aligned.
 */

/* Round n up to the next multiple of 8 (8-byte section alignment, format v2). */
#define ZIPC_ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

/* Per-slot states */
#define SLOT_EMPTY   0
#define SLOT_WRITING 1
#define SLOT_READY   2
#define SLOT_READING 3

/* Bound on every slot-state spin loop. A crashed peer leaves its slot
 * permanently claimed; bailing out (with a best-effort undo of the top
 * reservation) makes push/pop/top best-effort instead of hanging. */
#define ZIPC_MAX_SPINS 10000

/* Stack header in shared memory — matches C++ Stack::Header */
typedef struct {
    _Atomic int32_t top;       /* index of top element, -1 when empty */
    uint32_t capacity;
    uint32_t elem_size;
    uint32_t reserved;         /* pads header to 16 bytes so the data array is 8-aligned */
} stack_header_t;

/* Stack structure (process-local handle) */
struct zeroipc_stack {
    zeroipc_memory_t* memory;
    stack_header_t* header;
    void* data;                  /* pointer to data array */
    _Atomic uint32_t* state;     /* pointer to per-slot state array */
    char name[32];
};

/* Create stack */
zeroipc_stack_t* zeroipc_stack_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity) {
    if (!mem || !name || elem_size == 0 || capacity == 0) {
        return NULL;
    }

    zeroipc_stack_t* stack = calloc(1, sizeof(zeroipc_stack_t));
    if (!stack) return NULL;

    stack->memory = mem;
    strncpy(stack->name, name, sizeof(stack->name) - 1);

    /* Layout: [header(16)][data: elem_size*capacity][pad][state: uint32*capacity] */
    size_t state_array_size = sizeof(uint32_t) * capacity;
    if (capacity > (SIZE_MAX - sizeof(stack_header_t) - state_array_size) / elem_size) {
        free(stack);
        return NULL;
    }
    size_t state_off = ZIPC_ALIGN8(elem_size * capacity);
    size_t total_size = sizeof(stack_header_t) + state_off + state_array_size;

    size_t offset;
    int result = zeroipc_table_add(mem, name, total_size, &offset);
    if (result != ZEROIPC_OK) {
        free(stack);
        return NULL;
    }

    stack->header = (stack_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    stack->data = (char*)stack->header + sizeof(stack_header_t);
    stack->state = (_Atomic uint32_t*)((char*)stack->data + state_off);

    /* Initialize header (top = -1 means empty) */
    atomic_store(&stack->header->top, -1);
    stack->header->capacity = capacity;
    stack->header->elem_size = elem_size;
    stack->header->reserved = 0;

    /* Initialize per-slot states to EMPTY */
    for (uint32_t i = 0; i < capacity; ++i) {
        atomic_store_explicit(&stack->state[i], SLOT_EMPTY, memory_order_relaxed);
    }

    return stack;
}

/* Open existing stack */
zeroipc_stack_t* zeroipc_stack_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) return NULL;

    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) return NULL;

    zeroipc_stack_t* stack = calloc(1, sizeof(zeroipc_stack_t));
    if (!stack) return NULL;

    stack->memory = mem;
    strncpy(stack->name, name, sizeof(stack->name) - 1);

    stack->header = (stack_header_t*)((char*)zeroipc_memory_base(mem) + offset);
    if (stack->header->capacity == 0 || stack->header->elem_size == 0) {
        free(stack);
        return NULL;
    }

    stack->data = (char*)stack->header + sizeof(stack_header_t);
    stack->state = (_Atomic uint32_t*)((char*)stack->data + ZIPC_ALIGN8(
        (size_t)stack->header->elem_size * stack->header->capacity));

    return stack;
}

void zeroipc_stack_close(zeroipc_stack_t* stack) {
    free(stack);
}

/* Push (lock-free with per-slot 4-state CAS) */
int zeroipc_stack_push(zeroipc_stack_t* stack, const void* value) {
    if (!stack || !value) return ZEROIPC_ERROR_SIZE;

    int32_t current_top, new_top;

    /* Step 1: Reserve slot by CAS-advancing top */
    do {
        current_top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
        if (current_top >= (int32_t)(stack->header->capacity - 1)) {
            return ZEROIPC_ERROR_SIZE;  /* full */
        }
        new_top = current_top + 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &stack->header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot EMPTY -> WRITING. Bounded spin: a crashed peer can
     * leave the slot permanently claimed; bail out rather than hang. */
    uint32_t expected = SLOT_EMPTY;
    int claimed = 0;
    for (int spins = 0; spins < ZIPC_MAX_SPINS; ++spins) {
        if (atomic_compare_exchange_weak_explicit(
                &stack->state[new_top], &expected, SLOT_WRITING,
                memory_order_acq_rel, memory_order_relaxed)) {
            claimed = 1;
            break;
        }
        expected = SLOT_EMPTY;
        sched_yield();
    }
    if (!claimed) {
        /* Best-effort undo of the top reservation; if another push already
         * built on top of ours the CAS fails and the slot stays orphaned. */
        int32_t reserved = new_top;
        atomic_compare_exchange_strong_explicit(
            &stack->header->top, &reserved, current_top,
            memory_order_acq_rel, memory_order_relaxed);
        return ZEROIPC_ERROR_TIMEOUT;
    }

    /* Step 3: Write data */
    void* slot = (char*)stack->data + new_top * stack->header->elem_size;
    memcpy(slot, value, stack->header->elem_size);

    /* Step 4: WRITING -> READY */
    atomic_store_explicit(&stack->state[new_top], SLOT_READY, memory_order_release);

    return ZEROIPC_OK;
}

/* Pop (lock-free with per-slot 4-state CAS) */
int zeroipc_stack_pop(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) return ZEROIPC_ERROR_SIZE;

    int32_t current_top, new_top;

    /* Step 1: Reserve slot by CAS-decrementing top */
    do {
        current_top = atomic_load_explicit(&stack->header->top, memory_order_relaxed);
        if (current_top < 0) {
            return ZEROIPC_ERROR_NOT_FOUND;  /* empty */
        }
        new_top = current_top - 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &stack->header->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot READY -> READING. Bounded spin: normally this only
     * waits for the owning push to finish writing, but a pusher that crashed
     * mid-write leaves the slot stuck in WRITING forever. */
    uint32_t expected = SLOT_READY;
    int claimed = 0;
    for (int spins = 0; spins < ZIPC_MAX_SPINS; ++spins) {
        if (atomic_compare_exchange_weak_explicit(
                &stack->state[current_top], &expected, SLOT_READING,
                memory_order_acq_rel, memory_order_relaxed)) {
            claimed = 1;
            break;
        }
        expected = SLOT_READY;
        sched_yield();
    }
    if (!claimed) {
        /* Best-effort undo: put the item back under top so it is not
         * silently dropped. */
        int32_t reserved = new_top;
        atomic_compare_exchange_strong_explicit(
            &stack->header->top, &reserved, current_top,
            memory_order_acq_rel, memory_order_relaxed);
        return ZEROIPC_ERROR_TIMEOUT;
    }

    /* Step 3: Read data */
    void* slot = (char*)stack->data + current_top * stack->header->elem_size;
    memcpy(value, slot, stack->header->elem_size);

    /* Step 4: READING -> EMPTY */
    atomic_store_explicit(&stack->state[current_top], SLOT_EMPTY, memory_order_release);

    return ZEROIPC_OK;
}

/* Peek at top without removing.
 *
 * Best-effort: the peek must win a CAS on the slot state to read safely, so
 * under heavy contention (or a crashed peer holding the slot) it can return
 * ZEROIPC_ERROR_NOT_FOUND even though the stack is non-empty. Do not treat
 * that as an authoritative emptiness check; use zeroipc_stack_size for that. */
int zeroipc_stack_top(zeroipc_stack_t* stack, void* value) {
    if (!stack || !value) return ZEROIPC_ERROR_SIZE;

    /* A peek cannot passively read the slot: between observing READY and the
     * memcpy, a concurrent pop could recycle the slot and a new push begin
     * overwriting it, racing the read (TOCTOU). Claim the slot exclusively via
     * the same state machine pop uses (READY -> READING), copy, then restore
     * it to READY. The bounded spin preserves crash-safety. */
    for (int spins = 0; spins < ZIPC_MAX_SPINS; ++spins) {
        int32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
        if (top < 0) return ZEROIPC_ERROR_NOT_FOUND;

        uint32_t expected = SLOT_READY;
        if (atomic_compare_exchange_strong_explicit(
                &stack->state[top], &expected, SLOT_READING,
                memory_order_acq_rel, memory_order_relaxed)) {
            void* slot = (char*)stack->data + top * stack->header->elem_size;
            memcpy(value, slot, stack->header->elem_size);
            atomic_store_explicit(&stack->state[top], SLOT_READY, memory_order_release);
            return ZEROIPC_OK;
        }
        sched_yield();
    }
    return ZEROIPC_ERROR_NOT_FOUND;  /* Timed out */
}

int zeroipc_stack_empty(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    return atomic_load_explicit(&stack->header->top, memory_order_acquire) < 0;
}

int zeroipc_stack_full(zeroipc_stack_t* stack) {
    if (!stack) return 1;
    return atomic_load_explicit(&stack->header->top, memory_order_acquire)
           >= (int32_t)(stack->header->capacity - 1);
}

size_t zeroipc_stack_size(zeroipc_stack_t* stack) {
    if (!stack) return 0;
    int32_t top = atomic_load_explicit(&stack->header->top, memory_order_acquire);
    return top < 0 ? 0 : (size_t)(top + 1);
}

size_t zeroipc_stack_capacity(zeroipc_stack_t* stack) {
    return stack ? stack->header->capacity : 0;
}
