#define _POSIX_C_SOURCE 199309L
#include "zeroipc.h"
#include "zeroipc_barrier.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* Barrier header in shared memory - matches SPECIFICATION.md */
typedef struct {
    _Atomic int32_t arrived;       /* Number of processes that have arrived */
    _Atomic int32_t generation;    /* Generation counter (for reusability) */
    int32_t num_participants;      /* Total number of participants */
    int32_t _padding;              /* Alignment padding */
} barrier_header_t;

/* Barrier structure */
struct zeroipc_barrier {
    zeroipc_memory_t* memory;
    barrier_header_t* header;
    char name[32];
};

/* Helper function for nanosleep */
static void barrier_sleep_ns(long nanoseconds) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = nanoseconds;
    nanosleep(&ts, NULL);
}

/* Create barrier */
zeroipc_barrier_t* zeroipc_barrier_create(zeroipc_memory_t* mem, const char* name,
                                          int32_t num_participants) {
    if (!mem || !name || num_participants <= 0) {
        return NULL;
    }

    /* Allocate barrier structure */
    zeroipc_barrier_t* barrier = calloc(1, sizeof(zeroipc_barrier_t));
    if (!barrier) {
        return NULL;
    }

    barrier->memory = mem;
    strncpy(barrier->name, name, sizeof(barrier->name) - 1);

    /* Add to table and get offset */
    size_t offset;
    int result = zeroipc_table_add(mem, name, sizeof(barrier_header_t), &offset);
    if (result != ZEROIPC_OK) {
        free(barrier);
        return NULL;
    }

    /* Get header pointer */
    barrier->header = (barrier_header_t*)((char*)zeroipc_memory_base(mem) + offset);

    /* Initialize header */
    atomic_store(&barrier->header->arrived, 0);
    atomic_store(&barrier->header->generation, 0);
    barrier->header->num_participants = num_participants;
    barrier->header->_padding = 0;

    return barrier;
}

/* Open existing barrier */
zeroipc_barrier_t* zeroipc_barrier_open(zeroipc_memory_t* mem, const char* name) {
    if (!mem || !name) {
        return NULL;
    }

    /* Find in table */
    size_t offset, size;
    int result = zeroipc_table_find(mem, name, &offset, &size);
    if (result != ZEROIPC_OK) {
        return NULL;
    }

    /* Verify size */
    if (size != sizeof(barrier_header_t)) {
        return NULL;
    }

    /* Allocate barrier structure */
    zeroipc_barrier_t* barrier = calloc(1, sizeof(zeroipc_barrier_t));
    if (!barrier) {
        return NULL;
    }

    barrier->memory = mem;
    strncpy(barrier->name, name, sizeof(barrier->name) - 1);

    /* Get header pointer */
    barrier->header = (barrier_header_t*)((char*)zeroipc_memory_base(mem) + offset);

    return barrier;
}

/* Close barrier */
void zeroipc_barrier_close(zeroipc_barrier_t* barrier) {
    if (barrier) {
        free(barrier);
    }
}

/* Wait at barrier */
bool zeroipc_barrier_wait(zeroipc_barrier_t* barrier) {
    if (!barrier) {
        return false;
    }

    /* Capture current generation before arriving */
    int32_t my_generation = atomic_load(&barrier->header->generation);

    /* Atomically increment arrived counter */
    int32_t arrived = atomic_fetch_add(&barrier->header->arrived, 1) + 1;

    if (arrived == barrier->header->num_participants) {
        /* Last to arrive - reset and release everyone */
        atomic_store(&barrier->header->arrived, 0);

        /* Increment generation to release waiters */
        atomic_fetch_add(&barrier->header->generation, 1);
        return true;
    } else {
        /* Not last - wait for generation to change */
        int backoff_us = 1;  /* Start with 1 microsecond */
        const int max_backoff_us = 1000;  /* Max 1ms */

        while (atomic_load(&barrier->header->generation) == my_generation) {
            /* Exponential backoff to reduce CPU usage */
            barrier_sleep_ns(backoff_us * 1000);
            if (backoff_us < max_backoff_us) {
                backoff_us *= 2;
            }
        }

        return true;
    }
}

/* Wait at barrier with timeout */
bool zeroipc_barrier_wait_timeout(zeroipc_barrier_t* barrier, int32_t timeout_ms) {
    if (!barrier || timeout_ms < 0) {
        return false;
    }

    /* Get start time */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* Capture current generation before arriving */
    int32_t my_generation = atomic_load(&barrier->header->generation);

    /* Atomically increment arrived counter */
    int32_t arrived = atomic_fetch_add(&barrier->header->arrived, 1) + 1;

    if (arrived == barrier->header->num_participants) {
        /* Last to arrive - reset and release everyone */
        atomic_store(&barrier->header->arrived, 0);
        atomic_fetch_add(&barrier->header->generation, 1);
        return true;
    } else {
        /* Not last - wait for generation to change or timeout */
        int backoff_us = 1;
        const int max_backoff_us = 1000;

        while (atomic_load(&barrier->header->generation) == my_generation) {
            /* Check timeout */
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                             (now.tv_nsec - start.tv_nsec) / 1000000;

            if (elapsed_ms >= timeout_ms) {
                /* Timeout - decrement arrived count */
                /* WARNING: This creates a race if the last participant arrives
                 * during this window. Use with caution. */
                atomic_fetch_sub(&barrier->header->arrived, 1);
                return false;
            }

            /* Backoff */
            barrier_sleep_ns(backoff_us * 1000);
            if (backoff_us < max_backoff_us) {
                backoff_us *= 2;
            }
        }

        return true;
    }
}

/* Get arrived count */
int32_t zeroipc_barrier_arrived(zeroipc_barrier_t* barrier) {
    if (!barrier) {
        return -1;
    }
    return atomic_load(&barrier->header->arrived);
}

/* Get generation */
int32_t zeroipc_barrier_generation(zeroipc_barrier_t* barrier) {
    if (!barrier) {
        return -1;
    }
    return atomic_load(&barrier->header->generation);
}

/* Get num_participants */
int32_t zeroipc_barrier_num_participants(zeroipc_barrier_t* barrier) {
    if (!barrier) {
        return -1;
    }
    return barrier->header->num_participants;
}
