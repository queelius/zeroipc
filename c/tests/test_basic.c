#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include "zeroipc.h"

void test_memory_create() {
    printf("Testing memory creation...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_mem", 1024*1024, 64);
    assert(mem != NULL);
    assert(zeroipc_memory_size(mem) == 1024*1024);
    assert(zeroipc_memory_base(mem) != NULL);
    
    /* Close and unlink */
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_mem");
    
    printf("  ✓ Memory creation passed\n");
}

void test_memory_open() {
    printf("Testing memory open...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem1 = zeroipc_memory_create("/test_open", 1024*1024, 64);
    assert(mem1 != NULL);
    
    /* Open same memory */
    zeroipc_memory_t* mem2 = zeroipc_memory_open("/test_open");
    assert(mem2 != NULL);
    assert(zeroipc_memory_size(mem2) == 1024*1024);
    
    /* Clean up */
    zeroipc_memory_close(mem1);
    zeroipc_memory_close(mem2);
    zeroipc_memory_unlink("/test_open");
    
    printf("  ✓ Memory open passed\n");
}

void test_table_operations() {
    printf("Testing table operations...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_table", 1024*1024, 64);
    assert(mem != NULL);
    
    /* Add entries */
    size_t offset1, offset2;
    assert(zeroipc_table_add(mem, "entry1", 100, &offset1) == ZEROIPC_OK);
    assert(zeroipc_table_add(mem, "entry2", 200, &offset2) == ZEROIPC_OK);
    assert(offset2 > offset1);
    
    /* Find entries */
    size_t found_offset, found_size;
    assert(zeroipc_table_find(mem, "entry1", &found_offset, &found_size) == ZEROIPC_OK);
    assert(found_offset == offset1);
    assert(found_size == 100);
    
    assert(zeroipc_table_find(mem, "entry2", &found_offset, &found_size) == ZEROIPC_OK);
    assert(found_offset == offset2);
    assert(found_size == 200);
    
    /* Count entries */
    assert(zeroipc_table_count(mem) == 2);
    
    /* Try duplicate */
    assert(zeroipc_table_add(mem, "entry1", 50, NULL) == ZEROIPC_ERROR_ALREADY_EXISTS);
    
    /* Remove entry */
    assert(zeroipc_table_remove(mem, "entry1") == ZEROIPC_OK);
    assert(zeroipc_table_count(mem) == 1);
    assert(zeroipc_table_find(mem, "entry1", NULL, NULL) == ZEROIPC_ERROR_NOT_FOUND);
    
    /* Clean up */
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_table");
    
    printf("  ✓ Table operations passed\n");
}

void test_array_operations() {
    printf("Testing array operations...\n");
    
    /* Create memory */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_array", 1024*1024, 64);
    assert(mem != NULL);
    
    /* Create int array */
    zeroipc_array_t* arr = zeroipc_array_create(mem, "int_array", sizeof(int), 100);
    assert(arr != NULL);
    assert(zeroipc_array_capacity(arr) == 100);
    assert(zeroipc_array_elem_size(arr) == sizeof(int));
    
    /* Set and get values */
    int values[] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; i++) {
        assert(zeroipc_array_set(arr, i, &values[i]) == ZEROIPC_OK);
    }
    
    for (int i = 0; i < 5; i++) {
        int* ptr = (int*)zeroipc_array_get(arr, i);
        assert(ptr != NULL);
        assert(*ptr == values[i]);
    }
    
    /* Direct data access */
    int* data = (int*)zeroipc_array_data(arr);
    assert(data != NULL);
    assert(data[0] == 10);
    assert(data[4] == 50);
    
    /* Close array */
    zeroipc_array_close(arr);
    
    /* Clean up */
    zeroipc_memory_close(mem);
    zeroipc_memory_unlink("/test_array");
    
    printf("  ✓ Array operations passed\n");
}

void test_cross_process() {
    printf("Testing cross-process access...\n");
    
    /* Create memory in parent */
    zeroipc_memory_t* mem = zeroipc_memory_create("/test_cross", 1024*1024, 64);
    assert(mem != NULL);
    
    /* Create array */
    zeroipc_array_t* arr = zeroipc_array_create(mem, "shared_array", sizeof(float), 10);
    assert(arr != NULL);
    
    /* Write data */
    float value = 3.14f;
    zeroipc_array_set(arr, 0, &value);
    
    /* Fork process */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        zeroipc_array_close(arr);
        zeroipc_memory_close(mem);
        
        /* Open shared memory */
        zeroipc_memory_t* child_mem = zeroipc_memory_open("/test_cross");
        assert(child_mem != NULL);
        
        /* Find array in table */
        size_t offset, size;
        assert(zeroipc_table_find(child_mem, "shared_array", &offset, &size) == ZEROIPC_OK);
        
        /* Access data directly */
        float* data = (float*)((char*)zeroipc_memory_base(child_mem) + offset);
        assert(data[0] == 3.14f);
        
        /* Write back */
        data[1] = 2.71f;
        
        zeroipc_memory_close(child_mem);
        exit(0);
    } else {
        /* Parent process */
        int status;
        waitpid(pid, &status, 0);
        assert(WEXITSTATUS(status) == 0);
        
        /* Check child wrote data */
        float* data = (float*)zeroipc_array_data(arr);
        assert(data[1] == 2.71f);
        
        zeroipc_array_close(arr);
        zeroipc_memory_close(mem);
        zeroipc_memory_unlink("/test_cross");
    }
    
    printf("  ✓ Cross-process access passed\n");
}

int main() {
    printf("=== ZeroIPC C Tests ===\n\n");
    
    test_memory_create();
    test_memory_open();
    test_table_operations();
    test_array_operations();
    test_cross_process();
    
    printf("\n✓ All tests passed!\n");
    return 0;
}