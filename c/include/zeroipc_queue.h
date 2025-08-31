#ifndef ZEROIPC_QUEUE_H
#define ZEROIPC_QUEUE_H

#include "zeroipc.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Queue structure */
typedef struct zeroipc_queue zeroipc_queue_t;

/* Queue operations */
zeroipc_queue_t* zeroipc_queue_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity);
zeroipc_queue_t* zeroipc_queue_open(zeroipc_memory_t* mem, const char* name);
void zeroipc_queue_close(zeroipc_queue_t* queue);

/* Push/pop operations */
int zeroipc_queue_push(zeroipc_queue_t* queue, const void* value);
int zeroipc_queue_pop(zeroipc_queue_t* queue, void* value);

/* Queue status */
int zeroipc_queue_empty(zeroipc_queue_t* queue);
int zeroipc_queue_full(zeroipc_queue_t* queue);
size_t zeroipc_queue_size(zeroipc_queue_t* queue);
size_t zeroipc_queue_capacity(zeroipc_queue_t* queue);

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_QUEUE_H */