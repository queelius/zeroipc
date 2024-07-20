#include <gtest/gtest.h>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include "SharedMemoryManager.h"
#include "SharedMemoryAllocator.h"

TEST(SharedMemoryManagerTest, ParentCreateSharedMemory) {
    const char* SHM_NAME = "/test_create_shm";
    const size_t POOL_SIZE = 1024;

    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {  // Child process
        exit(0);
    } else {  // Parent process
        try {
            SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);  // Create new
            int status;
            ASSERT_NE(-1, waitpid(pid, &status, 0)) << "waitpid failed";
            EXPECT_TRUE(WIFEXITED(status)) << "Child process did not exit normally";
            EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process reported failure";
        } catch (const std::exception& e) {
            FAIL() << "Parent exception: " << e.what();
        }
    }
}

TEST(SharedMemoryManagerTest, ConcurrentAccess) {
    const char* SHM_NAME = "/test_concurrent_access";
    const int NUM_PROCESSES = 5;
    const int ITEMS_PER_PROCESS = 1024 * 1024;
    const size_t POOL_SIZE = NUM_PROCESSES * ITEMS_PER_PROCESS * sizeof(int);

    SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);
    int* data = static_cast<int*>(smm.get_data_addr());

    for (int i = 0; i < NUM_PROCESSES; ++i) {
        pid_t pid = fork();
        ASSERT_NE(pid, -1) << "Fork failed";

        if (pid == 0) {  // Child process
            SharedMemoryManager child_smm(SHM_NAME, POOL_SIZE, false);
            int* child_data = static_cast<int*>(child_smm.get_data_addr());
            for (int j = 0; j < ITEMS_PER_PROCESS; ++j) {
                child_data[i * ITEMS_PER_PROCESS + j] = i * ITEMS_PER_PROCESS + j;
            }
            exit(0);
        }
    }

    // Parent process waits for all children
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        int status;
        wait(&status);
        EXPECT_TRUE(WIFEXITED(status)) << "Child process did not exit normally";
        EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process reported failure";
    }

    // Verify data
    for (int i = 0; i < NUM_PROCESSES; ++i) {
        for (int j = 0; j < ITEMS_PER_PROCESS; ++j) {
            EXPECT_EQ(data[i * ITEMS_PER_PROCESS + j], i * ITEMS_PER_PROCESS + j);
        }
    }
}


TEST(SharedMemoryManagerTest, MultiProcessBasic) {
    const char* SHM_NAME = "/test_multi_process_basic";
    const size_t POOL_SIZE = 1024;
    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {  // Child process
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, false);
        int* data = static_cast<int*>(smm.get_data_addr());
        EXPECT_EQ(*data, 42);
        exit(0);
    } else {  // Parent process
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);
        int* data = static_cast<int*>(smm.get_data_addr());
        *data = 42;
        
        int status;
        ASSERT_NE(-1, waitpid(pid, &status, 0)) << "waitpid failed";
        EXPECT_TRUE(WIFEXITED(status)) << "Child process did not exit normally";
        EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process reported failure";
    }
}

TEST(SharedMemoryManagerTest, MemoryPersistence) {
    const char* SHM_NAME = "/test_persistence_shm";
    const size_t POOL_SIZE = 1024;

    // First, create a SharedMemoryManager and write some data
    {
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);
        int* data = static_cast<int*>(smm.get_data_addr());
        *data = 42;
    } // SharedMemoryManager goes out of scope here, should destroy the shared memory

    // Now, try to open the same shared memory segment
    try {
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, false); // Try to open existing
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "Failed to open shared memory");
    }
}

TEST(SharedMemoryManagerTest, ChildOpenSharedMemory) {
    const char* SHM_NAME = "/test_child_open_shm";
    const size_t POOL_SIZE = 1024;

    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {  // Child process
        try {
            usleep(1000);  // Wait for parent to create shared memory
            SharedMemoryManager smm(SHM_NAME, POOL_SIZE, false);  // Open existing
            exit(0);  // Success
        } catch (const std::exception& e) {
            std::cerr << "Child exception: " << e.what() << std::endl;
            exit(1);  // Failure
        }
    } else {  // Parent process
        try {
            SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);  // Create new
            int status;
            ASSERT_NE(-1, waitpid(pid, &status, 0)) << "waitpid failed";
            EXPECT_TRUE(WIFEXITED(status)) << "Child process did not exit normally";
            EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process reported failure";
        } catch (const std::exception& e) {
            FAIL() << "Parent exception: " << e.what();
        }
    }
}

// Add this new test
TEST(SharedMemoryManagerTest, CreateAndAccess) {
    const char* SHM_NAME = "/test_create_access";
    const size_t POOL_SIZE = 1024;

    try {
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE, true);
        int* data = static_cast<int*>(smm.get_data_addr());
        *data = 42;
        EXPECT_EQ(*data, 42);
    } catch (const std::exception& e) {
        FAIL() << "Exception: " << e.what();
    }
}

TEST(SharedMemoryManagerTest, CreateAndDestroy) {
    const char* SHM_NAME = "/test_create_destroy";
    const size_t POOL_SIZE = 1024;
    EXPECT_NO_THROW({
        SharedMemoryManager smm(SHM_NAME, POOL_SIZE);
    });
}

TEST(SharedMemoryManagerTest, MultipleInstances) {
    const char* SHM_NAME = "/test_multiple_instances";
    const size_t POOL_SIZE = 1024;
    SharedMemoryManager smm1(SHM_NAME, POOL_SIZE);
    EXPECT_NO_THROW({
        SharedMemoryManager smm2(SHM_NAME, POOL_SIZE);
    });
}

TEST(SharedMemoryManagerTest, AllocatorBasicUsage) {
    const char* SHM_NAME = "/test_allocator_basic";
    const size_t POOL_SIZE = 1024;
    SharedMemoryManager smm(SHM_NAME, POOL_SIZE);
    SharedMemoryAllocator<int> alloc(smm);

    std::vector<int, SharedMemoryAllocator<int>> vec(alloc);
    EXPECT_NO_THROW({
        vec.push_back(42);
        EXPECT_EQ(vec[0], 42);
    });
}

TEST(SharedMemoryManagerTest, AllocatorOutOfMemory) {
    const char* SHM_NAME = "/test_allocator_oom";
    const size_t SMALL_POOL_SIZE = 100;
    SharedMemoryManager smm(SHM_NAME, SMALL_POOL_SIZE); // Very small pool
    SharedMemoryAllocator<int> alloc(smm);

    std::vector<int, SharedMemoryAllocator<int>> vec(alloc);
    EXPECT_THROW({
        for (int i = 0; i < 1000; ++i) vec.push_back(i);
    }, std::bad_alloc);
}

TEST(SharedMemoryManagerTest, BasicFork) {

    pid_t pid = fork();
    ASSERT_NE(pid, -1) << "Fork failed";

    if (pid == 0) {  // Child process
        exit(0);
    } else {  // Parent process
        int status;
        ASSERT_NE(-1, waitpid(pid, &status, 0)) << "waitpid failed";
        EXPECT_TRUE(WIFEXITED(status)) << "Child process did not exit normally";
        EXPECT_EQ(0, WEXITSTATUS(status)) << "Child process reported failure";
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}