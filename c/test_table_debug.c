#include <stdio.h>
#include "../c/include/zeroipc.h"

int main() {
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_debug", 10*1024*1024, 16);
    
    int created = 0;
    for (int i = 0; i < 20; i++) {
        char name[32];
        snprintf(name, sizeof(name), "array_%d", i);
        
        zeroipc_array_t* arr = zeroipc_array_create(mem, name, sizeof(int), 10);
        if (arr == NULL) {
            printf("Failed to create array_%d\n", i);
            break;
        }
        created++;
        printf("Created array_%d (total: %d)\n", i, created);
    }
    
    printf("Total created: %d\n", created);
    
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_debug");
    return 0;
}
