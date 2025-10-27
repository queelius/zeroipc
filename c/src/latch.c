#define _POSIX_C_SOURCE 199309L
#include "zeroipc.h"
#include "zeroipc_latch.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* Latch header in shared memory - matches SPECIFICATION.md */
typedef struct {
    _Atomic int32_t count;       /* Current count (counts down to zero) */
    int32_t initial_count;       /* Initial count value (immutable) */
    int32_t _padding[2];         /* Alignment padding */
} latch_header_t;

/* Latch structure */
struct zeroipc_latch {
    zeroipc_memory_t* memory;
    latch_header_t* header;
    char name[32];
};

/* Helper function for nanosleep */
static void latch_sleep_ns(long nanoseconds) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = nanoseconds;
    nanosleep(&ts, NULL);
}

/* Create latch */
zeroipc_latch_t* zeroipc_latch_create(zeroipc_memory_t* mem, const char* name,
                                      int32_t count) {
    if (!mem || !name || count < 0) {
        return NULL;
    }

    /* Allocate latch structure */
    zeroipc_latch_t* latch = calloc(1, sizeof(zeroipc_latch_t));
    if (!latch) {
        return NULL;
    }

    latch->memory = mem;
    strncpy(latch->name, name, sizeof(latch->name) - 1);

    /* Add to table and get offset */
    size_t offset;
    int result = zeroipc_table_add(mem, name, sizeof(latch_header_t), &offset);
    if (result != ZEROIPC_OK) {
        free(latch);
        return NULL;
    }

    /* Get header pointer */
    latch->header = (latch_header_t*)((char*)zeroipc_memory_base(mem) + offset);

    /* Initialize header */
    atomic_store(&latch->header->count, count);
    latch->header->initial_count = count;
    latch->header->_padding[0] = 0;
    latch->header->_padding[1] = 0;

    return latch;
}

/* Open existing latch */
zeroipc_latch_t* zeroipc_latch_open(zeroipc_memory_t* mem, const char* name) {
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
    if (size != sizeof(latch_header_t)) {
        return NULL;
    }

    /* Allocate latch structure */
    zeroipc_latch_t* latch = calloc(1, sizeof(zeroipc_latch_t));
    if (!latch) {
        return NULL;
    }

    latch->memory = mem;
    strncpy(latch->name, name, sizeof(latch->name) - 1);

    /* Get header pointer */
    latch->header = (latch_header_t*)((char*)zeroipc_memory_base(mem) + offset);

    return latch;
}

/* Close latch */
void zeroipc_latch_close(zeroipc_latch_t* latch) {
    if (latch) {
        free(latch);
    }
}

/* Count down */
void zeroipc_latch_count_down(zeroipc_latch_t* latch, int32_t n) {
    if (!latch || n <= 0) {
        return;
    }

    int32_t current = atomic_load(&latch->header->count);

    while (current > 0) {
        int32_t new_count = (current >= n) ? (current - n) : 0;

        if (atomic_compare_exchange_weak(&latch->header->count,
                                         &current, new_count)) {
            return;
        }
        // CAS failed, current was updated, retry
    }
}

/* Wait at latch */
void zeroipc_latch_wait(zeroipc_latch_t* latch) {
    if (!latch) {
        return;
    }

    int backoff_us = 1;  /* Start with 1 microsecond */
    const int max_backoff_us = 1000;  /* Max 1ms */

    while (atomic_load(&latch->header->count) > 0) {
        /* Exponential backoff to reduce CPU usage */
        latch_sleep_ns(backoff_us * 1000);
        if (backoff_us < max_backoff_us) {
            backoff_us *= 2;
        }
    }
}

/* Try wait */
bool zeroipc_latch_try_wait(zeroipc_latch_t* latch) {
    if (!latch) {
        return false;
    }

    return atomic_load(&latch->header->count) == 0;
}

/* Wait with timeout */
bool zeroipc_latch_wait_timeout(zeroipc_latch_t* latch, int32_t timeout_ms) {
    if (!latch || timeout_ms < 0) {
        return false;
    }

    /* Get start time */
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int backoff_us = 1;
    const int max_backoff_us = 1000;

    while (atomic_load(&latch->header->count) > 0) {
        /* Check timeout */
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                         (now.tv_nsec - start.tv_nsec) / 1000000;

        if (elapsed_ms >= timeout_ms) {
            return false;
        }

        /* Backoff */
        latch_sleep_ns(backoff_us * 1000);
        if (backoff_us < max_backoff_us) {
            backoff_us *= 2;
        }
    }

    return true;
}

/* Get count */
int32_t zeroipc_latch_count(zeroipc_latch_t* latch) {
    if (!latch) {
        return -1;
    }
    return atomic_load(&latch->header->count);
}

/* Get initial count */
int32_t zeroipc_latch_initial_count(zeroipc_latch_t* latch) {
    if (!latch) {
        return -1;
    }
    return latch->header->initial_count;
}
