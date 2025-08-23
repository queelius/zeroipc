#include <gtest/gtest.h>
#include <zeroipc/memory.h>
#include <unistd.h>
#include <cstring>

using namespace zeroipc;

class MemoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Generate unique name using PID
        test_name = "/test_mem_" + std::to_string(getpid());
    }
    
    void TearDown() override {
        // Clean up any leftover shared memory
        shm_unlink(test_name.c_str());
    }
    
    std::string test_name;
};

TEST_F(MemoryTest, CreateNewMemory) {
    size_t size = 1024 * 1024; // 1MB
    Memory mem(test_name, size);
    
    EXPECT_EQ(mem.size(), size);
    EXPECT_EQ(mem.name(), test_name);
    EXPECT_TRUE(mem.is_owner());
    EXPECT_NE(mem.data(), nullptr);
    EXPECT_NE(mem.table(), nullptr);
    
    // Table should be initialized
    EXPECT_EQ(mem.table()->entry_count(), 0);
    EXPECT_EQ(mem.table()->max_entries(), 64);
    
    mem.unlink();
}

TEST_F(MemoryTest, OpenExistingMemory) {
    size_t size = 512 * 1024; // 512KB
    
    // Create memory
    {
        Memory mem1(test_name, size);
        mem1.table()->add("test_entry", 1000, 100);
    }
    
    // Open existing memory
    {
        Memory mem2(test_name); // size = 0 means open existing
        EXPECT_EQ(mem2.size(), size);
        EXPECT_FALSE(mem2.is_owner());
        
        // Should see the existing table entry
        EXPECT_EQ(mem2.table()->entry_count(), 1);
        auto* entry = mem2.table()->find("test_entry");
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->offset, 1000);
    }
    
    shm_unlink(test_name.c_str());
}

TEST_F(MemoryTest, CustomTableSize) {
    Memory mem(test_name, 1024 * 1024, 128); // 128 max entries
    
    EXPECT_EQ(mem.table()->max_entries(), 128);
    
    mem.unlink();
}

TEST_F(MemoryTest, AtMethod) {
    Memory mem(test_name, 1024);
    
    // Write at offset
    int* ptr = static_cast<int*>(mem.at(100));
    *ptr = 42;
    
    // Read from offset
    const int* cptr = static_cast<const int*>(mem.at(100));
    EXPECT_EQ(*cptr, 42);
    
    // Out of bounds should throw
    EXPECT_THROW(mem.at(2000), std::out_of_range);
    
    mem.unlink();
}

TEST_F(MemoryTest, MoveSemantics) {
    Memory mem1(test_name, 1024);
    mem1.table()->add("entry1", 100, 50);
    
    // Move constructor
    Memory mem2(std::move(mem1));
    EXPECT_EQ(mem2.size(), 1024);
    EXPECT_EQ(mem2.table()->entry_count(), 1);
    
    // Move assignment
    Memory mem3("/dummy", 512);
    mem3 = std::move(mem2);
    EXPECT_EQ(mem3.size(), 1024);
    EXPECT_EQ(mem3.table()->entry_count(), 1);
    
    mem3.unlink();
}

TEST_F(MemoryTest, DataPersistence) {
    // Write data
    {
        Memory mem(test_name, 4096);
        char* data = static_cast<char*>(mem.at(1000));
        std::strcpy(data, "Hello, ZeroIPC!");
    }
    
    // Read data back
    {
        Memory mem(test_name);
        const char* data = static_cast<const char*>(mem.at(1000));
        EXPECT_STREQ(data, "Hello, ZeroIPC!");
    }
    
    shm_unlink(test_name.c_str());
}

TEST_F(MemoryTest, TableIntegration) {
    Memory mem(test_name, 10 * 1024);
    
    // Use table to allocate space
    uint32_t offset1 = mem.table()->allocate(100);
    mem.table()->add("data1", offset1, 100);
    
    uint32_t offset2 = mem.table()->allocate(200);
    mem.table()->add("data2", offset2, 200);
    
    // Verify allocations don't overlap
    EXPECT_GE(offset2, offset1 + 100);
    
    // Write to allocated spaces
    int* data1 = static_cast<int*>(mem.at(offset1));
    *data1 = 123;
    
    double* data2 = static_cast<double*>(mem.at(offset2));
    *data2 = 3.14159;
    
    // Verify data
    EXPECT_EQ(*data1, 123);
    EXPECT_DOUBLE_EQ(*data2, 3.14159);
    
    mem.unlink();
}

TEST_F(MemoryTest, NonExistentMemoryThrows) {
    EXPECT_THROW(Memory("/nonexistent_shm_12345"), std::runtime_error);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}