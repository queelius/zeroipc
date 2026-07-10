/**
 * ZeroIPC Raw FFI — stateless functions for Python ctypes integration.
 *
 * Each function takes (base, offset) and reinterprets the memory at
 * (base + offset) as a queue/stack header. All atomic operations use
 * C11 stdatomic.h, giving Python true cross-process MPMC safety.
 *
 * Struct layouts match SPECIFICATION.md and the C++/Go/Python binary format.
 */

#include "zeroipc_ffi.h"
#include <sched.h>
#include <stdatomic.h>
#include <string.h>

/* Return codes */
#define FFI_OK       0
#define FFI_EMPTY   -1
#define FFI_FULL    -1
#define FFI_MISMATCH -2
#define FFI_INVALID  -3

/* Round n up to the next multiple of 8 (8-byte section alignment, format v2).
 * The atomic side-arrays (queue sequence, stack state) are placed on 8-byte
 * boundaries so their atomics are always naturally aligned. */
#define ZIPC_ALIGN8(n) (((n) + 7u) & ~(size_t)7u)

/* ============================================================================
 * Queue layout (Vyukov bounded MPMC), format v2
 * Header: [head:u32][tail:u32][capacity:u32][elem_size:u32] = 16 bytes
 * Data:   capacity * elem_size bytes
 * Pad:    to next 8-byte boundary
 * Seqs:   capacity * 4 bytes (per-slot sequence numbers, 8-aligned)
 * ============================================================================ */

typedef struct {
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
    uint32_t capacity;
    uint32_t elem_size;
} ffi_queue_header_t;

_Static_assert(sizeof(ffi_queue_header_t) == 16, "Queue header must be 16 bytes");

static inline int q_validate(ffi_queue_header_t* h, uint32_t elem_size) {
    if (h->capacity == 0 || h->elem_size == 0) return FFI_INVALID;
    if (h->elem_size != elem_size) return FFI_MISMATCH;
    return FFI_OK;
}

static inline ffi_queue_header_t* q_header(void* base, size_t offset) {
    return (ffi_queue_header_t*)((char*)base + offset);
}

static inline void* q_data(ffi_queue_header_t* h) {
    return (char*)h + sizeof(ffi_queue_header_t);
}

static inline _Atomic uint32_t* q_seqs(ffi_queue_header_t* h) {
    return (_Atomic uint32_t*)((char*)q_data(h)
                              + ZIPC_ALIGN8((size_t)h->capacity * h->elem_size));
}

int zeroipc_raw_queue_push(void* base, size_t offset,
                           const void* value, uint32_t elem_size) {
    ffi_queue_header_t* h = q_header(base, offset);
    int rc = q_validate(h, elem_size);
    if (rc != FFI_OK) return rc;

    _Atomic uint32_t* seq = q_seqs(h);
    void* data = q_data(h);
    uint32_t cap = h->capacity;
    uint32_t tail, slot;
    int32_t diff;

    for (;;) {
        tail = atomic_load_explicit(&h->tail, memory_order_relaxed);
        slot = tail % cap;
        uint32_t s = atomic_load_explicit(&seq[slot], memory_order_acquire);
        diff = (int32_t)(s - tail);

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &h->tail, &tail, tail + 1,
                    memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (diff < 0) {
            return FFI_FULL;
        }
        /* diff > 0: another producer claimed this slot, retry */
    }

    /* Write data, then publish */
    memcpy((char*)data + slot * elem_size, value, elem_size);
    atomic_store_explicit(&seq[slot], tail + 1, memory_order_release);
    return FFI_OK;
}

int zeroipc_raw_queue_pop(void* base, size_t offset,
                          void* value_out, uint32_t elem_size) {
    ffi_queue_header_t* h = q_header(base, offset);
    int rc = q_validate(h, elem_size);
    if (rc != FFI_OK) return rc;

    _Atomic uint32_t* seq = q_seqs(h);
    void* data = q_data(h);
    uint32_t cap = h->capacity;
    uint32_t head, slot;
    int32_t diff;

    for (;;) {
        head = atomic_load_explicit(&h->head, memory_order_relaxed);
        slot = head % cap;
        uint32_t s = atomic_load_explicit(&seq[slot], memory_order_acquire);
        diff = (int32_t)(s - (head + 1));

        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &h->head, &head, head + 1,
                    memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (diff < 0) {
            return FFI_EMPTY;
        }
    }

    /* Read data, then recycle slot */
    memcpy(value_out, (char*)data + slot * elem_size, elem_size);
    atomic_store_explicit(&seq[slot], head + cap, memory_order_release);
    return FFI_OK;
}

uint32_t zeroipc_raw_queue_size(void* base, size_t offset) {
    ffi_queue_header_t* h = q_header(base, offset);
    uint32_t tail = atomic_load_explicit(&h->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&h->head, memory_order_relaxed);
    return tail - head;
}

int zeroipc_raw_queue_empty(void* base, size_t offset) {
    return zeroipc_raw_queue_size(base, offset) == 0;
}

int zeroipc_raw_queue_full(void* base, size_t offset) {
    ffi_queue_header_t* h = q_header(base, offset);
    uint32_t tail = atomic_load_explicit(&h->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&h->head, memory_order_relaxed);
    return (tail - head) >= h->capacity;
}

/* ============================================================================
 * Stack layout (4-state CAS), format v2
 * Header: [top:i32][capacity:u32][elem_size:u32][reserved:u32] = 16 bytes
 * Data:   capacity * elem_size bytes
 * Pad:    to next 8-byte boundary
 * State:  capacity * 4 bytes (per-slot state: EMPTY/WRITING/READY/READING, 8-aligned)
 * ============================================================================ */

typedef struct {
    _Atomic int32_t top;
    uint32_t capacity;
    uint32_t elem_size;
    uint32_t reserved;  /* pads header to 16 bytes so the data array is 8-aligned */
} ffi_stack_header_t;

_Static_assert(sizeof(ffi_stack_header_t) == 16, "Stack header must be 16 bytes");

static inline int s_validate(ffi_stack_header_t* h, uint32_t elem_size) {
    if (h->capacity == 0 || h->elem_size == 0) return FFI_INVALID;
    if (h->elem_size != elem_size) return FFI_MISMATCH;
    return FFI_OK;
}

#define SLOT_EMPTY   0
#define SLOT_WRITING 1
#define SLOT_READY   2
#define SLOT_READING 3

/* Bound on every slot-state spin loop. A crashed peer leaves its slot
 * permanently claimed; bailing out (with a best-effort undo of the top
 * reservation) makes push/pop/top best-effort instead of hanging. Each
 * iteration yields so a live-but-descheduled peer gets CPU time to finish;
 * without the yield the whole bound elapses in microseconds and merely
 * descheduled writers would trigger spurious bail-outs. */
#define FFI_MAX_SPINS 10000

static inline ffi_stack_header_t* s_header(void* base, size_t offset) {
    return (ffi_stack_header_t*)((char*)base + offset);
}

static inline void* s_data(ffi_stack_header_t* h) {
    return (char*)h + sizeof(ffi_stack_header_t);
}

static inline _Atomic uint32_t* s_state(ffi_stack_header_t* h) {
    return (_Atomic uint32_t*)((char*)s_data(h)
                              + ZIPC_ALIGN8((size_t)h->capacity * h->elem_size));
}

int zeroipc_raw_stack_push(void* base, size_t offset,
                           const void* value, uint32_t elem_size) {
    ffi_stack_header_t* h = s_header(base, offset);
    int rc = s_validate(h, elem_size);
    if (rc != FFI_OK) return rc;

    _Atomic uint32_t* state = s_state(h);
    void* data = s_data(h);
    int32_t current_top, new_top;

    /* Step 1: Reserve slot by CAS-incrementing top */
    do {
        current_top = atomic_load_explicit(&h->top, memory_order_relaxed);
        if (current_top >= (int32_t)(h->capacity - 1))
            return FFI_FULL;
        new_top = current_top + 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &h->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot EMPTY -> WRITING. Bounded spin: a crashed peer can
     * leave the slot permanently claimed; bail out rather than hang. */
    uint32_t expected = SLOT_EMPTY;
    int claimed = 0;
    for (int spins = 0; spins < FFI_MAX_SPINS; ++spins) {
        if (atomic_compare_exchange_weak_explicit(
                &state[new_top], &expected, SLOT_WRITING,
                memory_order_acq_rel, memory_order_relaxed)) {
            claimed = 1;
            break;
        }
        expected = SLOT_EMPTY;
        sched_yield();
    }
    if (!claimed) {
        /* Best-effort undo of the top reservation. */
        int32_t reserved = new_top;
        atomic_compare_exchange_strong_explicit(
            &h->top, &reserved, current_top,
            memory_order_acq_rel, memory_order_relaxed);
        return FFI_FULL;
    }

    /* Step 3: Write data */
    memcpy((char*)data + new_top * elem_size, value, elem_size);

    /* Step 4: Publish WRITING -> READY */
    atomic_store_explicit(&state[new_top], SLOT_READY, memory_order_release);
    return FFI_OK;
}

int zeroipc_raw_stack_pop(void* base, size_t offset,
                          void* value_out, uint32_t elem_size) {
    ffi_stack_header_t* h = s_header(base, offset);
    int rc = s_validate(h, elem_size);
    if (rc != FFI_OK) return rc;

    _Atomic uint32_t* state = s_state(h);
    void* data = s_data(h);
    int32_t current_top, new_top;

    /* Step 1: Reserve slot by CAS-decrementing top */
    do {
        current_top = atomic_load_explicit(&h->top, memory_order_relaxed);
        if (current_top < 0)
            return FFI_EMPTY;
        new_top = current_top - 1;
    } while (!atomic_compare_exchange_weak_explicit(
                &h->top, &current_top, new_top,
                memory_order_acq_rel, memory_order_relaxed));

    /* Step 2: CAS slot READY -> READING. Bounded spin: a pusher that crashed
     * mid-write leaves the slot stuck in WRITING forever. */
    uint32_t expected = SLOT_READY;
    int claimed = 0;
    for (int spins = 0; spins < FFI_MAX_SPINS; ++spins) {
        if (atomic_compare_exchange_weak_explicit(
                &state[current_top], &expected, SLOT_READING,
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
            &h->top, &reserved, current_top,
            memory_order_acq_rel, memory_order_relaxed);
        return FFI_EMPTY;
    }

    /* Step 3: Read data */
    memcpy(value_out, (char*)data + current_top * elem_size, elem_size);

    /* Step 4: Release READING -> EMPTY */
    atomic_store_explicit(&state[current_top], SLOT_EMPTY, memory_order_release);
    return FFI_OK;
}

/* Best-effort peek: must win a CAS on the slot state to read safely, so under
 * heavy contention (or a crashed peer holding the slot) it can return
 * FFI_EMPTY even though the stack is non-empty. Callers should not treat that
 * as an authoritative emptiness check. */
int zeroipc_raw_stack_top(void* base, size_t offset,
                          void* value_out, uint32_t elem_size) {
    ffi_stack_header_t* h = s_header(base, offset);
    int rc = s_validate(h, elem_size);
    if (rc != FFI_OK) return rc;

    _Atomic uint32_t* state = s_state(h);
    void* data = s_data(h);

    /* A peek cannot passively read the slot: between observing READY and the
     * memcpy, a concurrent pop could recycle the slot and a new push begin
     * overwriting it, racing the read (TOCTOU). Claim the slot exclusively via
     * the same state machine pop uses (READY -> READING), copy, then restore
     * it to READY. The bounded spin preserves crash-safety. */
    for (int spins = 0; spins < FFI_MAX_SPINS; ++spins) {
        int32_t top = atomic_load_explicit(&h->top, memory_order_acquire);
        if (top < 0) return FFI_EMPTY;

        uint32_t expected = SLOT_READY;
        if (atomic_compare_exchange_strong_explicit(
                &state[top], &expected, SLOT_READING,
                memory_order_acq_rel, memory_order_relaxed)) {
            memcpy(value_out, (char*)data + top * elem_size, elem_size);
            atomic_store_explicit(&state[top], SLOT_READY, memory_order_release);
            return FFI_OK;
        }
        sched_yield();
    }
    return FFI_EMPTY;
}

uint32_t zeroipc_raw_stack_size(void* base, size_t offset) {
    ffi_stack_header_t* h = s_header(base, offset);
    int32_t top = atomic_load_explicit(&h->top, memory_order_relaxed);
    return top < 0 ? 0 : (uint32_t)(top + 1);
}

int zeroipc_raw_stack_empty(void* base, size_t offset) {
    return zeroipc_raw_stack_size(base, offset) == 0;
}

int zeroipc_raw_stack_full(void* base, size_t offset) {
    ffi_stack_header_t* h = s_header(base, offset);
    int32_t top = atomic_load_explicit(&h->top, memory_order_relaxed);
    return top >= (int32_t)(h->capacity - 1);
}
