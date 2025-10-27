/**
 * ZeroIPC Latch - Lock-free latch for one-time countdown synchronization
 */

#ifndef ZEROIPC_LATCH_H
#define ZEROIPC_LATCH_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct zeroipc_memory zeroipc_memory_t;
typedef struct zeroipc_latch zeroipc_latch_t;

/**
 * Create a new latch in shared memory
 *
 * @param mem   Memory instance
 * @param name  Latch identifier (max 31 chars)
 * @param count Initial count value (must be >= 0)
 * @return      Latch handle or NULL on failure
 */
zeroipc_latch_t* zeroipc_latch_create(zeroipc_memory_t* mem, const char* name,
                                      int32_t count);

/**
 * Open existing latch in shared memory
 *
 * @param mem  Memory instance
 * @param name Latch identifier
 * @return     Latch handle or NULL on failure
 */
zeroipc_latch_t* zeroipc_latch_open(zeroipc_memory_t* mem, const char* name);

/**
 * Close latch handle (latch remains in shared memory)
 *
 * @param latch Latch handle
 */
void zeroipc_latch_close(zeroipc_latch_t* latch);

/**
 * Decrement the count by n (default 1)
 *
 * Atomically decrements the count, saturating at 0. If the count
 * reaches 0, all waiting processes are released.
 *
 * @param latch Latch handle
 * @param n     Amount to decrement (must be > 0)
 */
void zeroipc_latch_count_down(zeroipc_latch_t* latch, int32_t n);

/**
 * Wait for the count to reach zero
 *
 * Blocks until the latch count reaches 0. If the count is already 0,
 * returns immediately.
 *
 * @param latch Latch handle
 */
void zeroipc_latch_wait(zeroipc_latch_t* latch);

/**
 * Try to wait without blocking
 *
 * @param latch Latch handle
 * @return      true if count is 0, false if still counting down
 */
bool zeroipc_latch_try_wait(zeroipc_latch_t* latch);

/**
 * Wait for the count to reach zero with a timeout
 *
 * @param latch      Latch handle
 * @param timeout_ms Timeout in milliseconds
 * @return           true if count reached 0, false if timed out or error
 */
bool zeroipc_latch_wait_timeout(zeroipc_latch_t* latch, int32_t timeout_ms);

/**
 * Get current count value
 *
 * @param latch Latch handle
 * @return      Current count, or -1 on error
 */
int32_t zeroipc_latch_count(zeroipc_latch_t* latch);

/**
 * Get initial count value
 *
 * @param latch Latch handle
 * @return      Initial count that the latch was created with, or -1 on error
 */
int32_t zeroipc_latch_initial_count(zeroipc_latch_t* latch);

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_LATCH_H */
