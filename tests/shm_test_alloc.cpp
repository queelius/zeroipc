#include <gtest/gtest.h>
#include "SharedMemoryManager.h"
#include "SharedMemoryAllocator.h"
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

TEST(SharedMemoryManagerTest, AllocatorLargeContainer) {
    const char* SHM_NAME = "/test_allocator_large";
    const size_t POOL_SIZE = 1024 * 1024;
    
    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {  // Child process
        sleep(1);  // Give parent time to set up shared memory
        try {
            SharedMemoryManager child_smm(SHM_NAME, POOL_SIZE, false);
            SharedMemoryAllocator<int> child_alloc(child_smm);
            std::vector<int, SharedMemoryAllocator<int>> child_vec(child_alloc);
            
            printf("Child: Vector metadata - size: %zu, capacity: %zu\n", 
                   child_alloc.size(), child_alloc.metadata->capacity.load());

            for (size_t i = 0; i < child_alloc.size(); ++i) {
                printf("Child: Accessing index %zu, value: %d\n", i, child_vec[i]);
                EXPECT_EQ(child_vec[i], i);
            }
        } catch (const std::exception& e) {
            printf("Child exception: %s\n", e.what());
            exit(1);
        }
        exit(0);
    } else {  // Parent process
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);
        SharedMemoryAllocator<int> alloc(smm);
        std::vector<int, SharedMemoryAllocator<int>> vec(alloc);

        for (int i = 0; i < 1000; ++i) {
            vec.push_back(i);
            alloc.set_size(vec.size());  // Explicitly update size in shared memory
        }
        printf("Parent: Vector filled, size: %zu, capacity: %zu\n", 
               alloc.size(), alloc.metadata->capacity.load());

        int status;
        waitpid(pid, &status, 0);
        printf("Parent: Child process exited with status %d\n", WEXITSTATUS(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}