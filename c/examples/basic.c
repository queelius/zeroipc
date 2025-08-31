#include <stdio.h>
#include <stdlib.h>
#include "zeroipc.h"

int main() {
    printf("=== ZeroIPC C Example ===\n\n");
    
    /* Create shared memory */
    printf("Creating shared memory '/example_data' (10MB)...\n");
    zeroipc_memory_t* mem = zeroipc_memory_create("/example_data", 10*1024*1024, 128);
    if (!mem) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    
    /* Create float array for sensor data */
    printf("Creating float array 'temperatures' with 1000 elements...\n");
    zeroipc_array_t* temps = zeroipc_array_create(mem, "temperatures", sizeof(float), 1000);
    if (!temps) {
        fprintf(stderr, "Failed to create array\n");
        zeroipc_memory_close(mem);
        return 1;
    }
    
    /* Write some data */
    printf("Writing temperature data...\n");
    float temp_values[] = {20.5f, 21.0f, 22.3f, 23.1f, 22.8f};
    for (int i = 0; i < 5; i++) {
        zeroipc_array_set(temps, i, &temp_values[i]);
    }
    
    /* Create int array for counters */
    printf("Creating int array 'counters' with 100 elements...\n");
    zeroipc_array_t* counters = zeroipc_array_create(mem, "counters", sizeof(int), 100);
    if (!counters) {
        fprintf(stderr, "Failed to create counters array\n");
        zeroipc_array_close(temps);
        zeroipc_memory_close(mem);
        return 1;
    }
    
    /* Initialize counters */
    for (int i = 0; i < 10; i++) {
        int value = i * 10;
        zeroipc_array_set(counters, i, &value);
    }
    
    /* Show table contents */
    printf("\nTable contents:\n");
    printf("  Entry count: %zu\n", zeroipc_table_count(mem));
    
    /* Verify we can find entries */
    size_t offset, size;
    if (zeroipc_table_find(mem, "temperatures", &offset, &size) == ZEROIPC_OK) {
        printf("  - 'temperatures' at offset %zu, size %zu bytes\n", offset, size);
    }
    if (zeroipc_table_find(mem, "counters", &offset, &size) == ZEROIPC_OK) {
        printf("  - 'counters' at offset %zu, size %zu bytes\n", offset, size);
    }
    
    /* Read back data */
    printf("\nReading back data:\n");
    printf("  Temperatures: ");
    float* temp_data = (float*)zeroipc_array_data(temps);
    for (int i = 0; i < 5; i++) {
        printf("%.1f ", temp_data[i]);
    }
    printf("\n");
    
    printf("  Counters: ");
    int* counter_data = (int*)zeroipc_array_data(counters);
    for (int i = 0; i < 10; i++) {
        printf("%d ", counter_data[i]);
    }
    printf("\n");
    
    printf("\nShared memory '/example_data' is ready for other processes to access.\n");
    printf("Press Enter to clean up...");
    getchar();
    
    /* Clean up */
    zeroipc_array_close(temps);
    zeroipc_array_close(counters);
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/example_data");
    
    printf("Cleaned up.\n");
    return 0;
}