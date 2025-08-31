#ifndef ZEROIPC_STACK_H
#define ZEROIPC_STACK_H

#include "zeroipc.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stack structure */
typedef struct zeroipc_stack zeroipc_stack_t;

/* Stack operations */
zeroipc_stack_t* zeroipc_stack_create(zeroipc_memory_t* mem, const char* name,
                                       size_t elem_size, size_t capacity);
zeroipc_stack_t* zeroipc_stack_open(zeroipc_memory_t* mem, const char* name);
void zeroipc_stack_close(zeroipc_stack_t* stack);

/* Push/pop operations */
int zeroipc_stack_push(zeroipc_stack_t* stack, const void* value);
int zeroipc_stack_pop(zeroipc_stack_t* stack, void* value);
int zeroipc_stack_top(zeroipc_stack_t* stack, void* value);

/* Stack status */
int zeroipc_stack_empty(zeroipc_stack_t* stack);
int zeroipc_stack_full(zeroipc_stack_t* stack);
size_t zeroipc_stack_size(zeroipc_stack_t* stack);
size_t zeroipc_stack_capacity(zeroipc_stack_t* stack);

#ifdef __cplusplus
}
#endif

#endif /* ZEROIPC_STACK_H */