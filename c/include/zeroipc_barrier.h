/**
 * ZeroIPC Barrier - Lock-free barrier for cross-process synchronization
 */

#ifndef ZEROIPC_BARRIER_H
#define ZEROIPC_BARRIER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct zeroipc_memory zeroipc_memory_t;
typedef struct zeroipc_barrier zeroipc_barrier_t;

/**
 * Create a new barrier in shared memory
 *
 * @param mem              Memory instance
 * @param name             Barrier identifier (max 31 chars)
 * @param num_participants Number of processes that must arrive before releasing
 * @return                 Barrier handle or NULL on failure
 */
zeroipc_barrier_t* zeroipc_barrier_create(zeroipc_memory_t* mem, const char* name,
                                          int32_t num_participants);

/**
 * Open existing barrier in shared memory
 *
 * @param mem  Memory instance
 * @param name Barrier identifier
 * @return     Barrier handle or NULL on failure
 */
zeroipc_barrier_t* zeroipc_barrier_open(zeroipc_memory_t* mem, const char* name);

/**
 * Close barrier handle (barrier remains in shared memory)
 *
 * @param barrier Barrier handle
 */
void zeroipc_barrier_close(zeroipc_barrier_t* barrier);

/**
 * Wait for all participants to arrive at the barrier
 *
 * Blocks until all num_participants processes have called wait().
 * Once all arrive, all waiters are released simultaneously and the
 * barrier automatically resets for the next cycle.
 *
 * @param barrier Barrier handle
 * @return        true on success, false on error
 */
bool zeroipc_barrier_wait(zeroipc_barrier_t* barrier);

/**
 * Wait for all participants with a timeout
 *
 * @param barrier    Barrier handle
 * @param timeout_ms Timeout in milliseconds
 * @return           true if barrier released, false if timed out or error
 *
 * WARNING: If a timeout occurs, the barrier state may be inconsistent.
 * The caller is responsible for coordinating recovery with other processes.
 */
bool zeroipc_barrier_wait_timeout(zeroipc_barrier_t* barrier, int32_t timeout_ms);

/**
 * Get number of processes currently waiting at the barrier
 *
 * @param barrier Barrier handle
 * @return        Number of arrived processes, or -1 on error
 */
int32_t zeroipc_barrier_arrived(zeroipc_barrier_t* barrier);

/**
 * Get current generation number
 *
 * The generation increments each time all participants pass through.
 *
 * @param barrier Barrier handle
 * @return        Current generation, or -1 on error
 */
int32_t zeroipc_barrier_generation(zeroipc_barrier_t* barrier);

/**
 * Get number of participants required
 *
 * @param barrier Barrier handle
 * @return        Number of participants, or -1 on error
 */
int32_t zeroipc_barrier_num_participants(zeroipc_barrier_t* barrier);

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_BARRIER_H */
