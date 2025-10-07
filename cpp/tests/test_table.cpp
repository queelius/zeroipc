#include <gtest/gtest.h>
#include <zeroipc/table.h>
#include <vector>
#include <cstring>

using namespace zeroipc;

class TableTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Allocate memory for testing
        buffer_size = Table::calculate_size(64) + 10000; // Extra space for allocations
        buffer.resize(buffer_size);
        std::memset(buffer.data(), 0, buffer_size);
    }
    
    std::vector<char> buffer;
    size_t buffer_size;
};

TEST_F(TableTest, CreateNewTable) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    EXPECT_EQ(table.entry_count(), 0);
    EXPECT_EQ(table.max_entries(), 64);
    EXPECT_EQ(table.next_offset(), Table::calculate_size(64));
}

TEST_F(TableTest, AddAndFindEntry) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    EXPECT_TRUE(table.add("test_entry", 1000, 500));
    EXPECT_EQ(table.entry_count(), 1);
    
    auto* entry = table.find("test_entry");
    ASSERT_NE(entry, nullptr);
    EXPECT_STREQ(entry->name, "test_entry");
    EXPECT_EQ(entry->offset, 1000);
    EXPECT_EQ(entry->size, 500);
    
    // Non-existent entry
    EXPECT_EQ(table.find("nonexistent"), nullptr);
}

TEST_F(TableTest, MultipleEntries) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    EXPECT_TRUE(table.add("entry1", 1000, 100));
    EXPECT_TRUE(table.add("entry2", 2000, 200));
    EXPECT_TRUE(table.add("entry3", 3000, 300));
    
    EXPECT_EQ(table.entry_count(), 3);
    
    auto* e1 = table.find("entry1");
    auto* e2 = table.find("entry2");
    auto* e3 = table.find("entry3");
    
    ASSERT_NE(e1, nullptr);
    ASSERT_NE(e2, nullptr);
    ASSERT_NE(e3, nullptr);
    
    EXPECT_EQ(e1->offset, 1000);
    EXPECT_EQ(e2->offset, 2000);
    EXPECT_EQ(e3->offset, 3000);
}

TEST_F(TableTest, DuplicateNameThrows) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    EXPECT_TRUE(table.add("test", 1000, 100));
    EXPECT_THROW(table.add("test", 2000, 200), std::invalid_argument);
}

TEST_F(TableTest, LongNameThrows) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    std::string long_name(32, 'x'); // 32 chars, too long
    EXPECT_THROW(table.add(long_name, 1000, 100), std::invalid_argument);
    
    std::string max_name(31, 'y'); // 31 chars, maximum allowed
    EXPECT_TRUE(table.add(max_name, 1000, 100));
}

TEST_F(TableTest, TableFull) {
    Table table(buffer.data(), 4, buffer.size(), true); // Small table for testing
    
    EXPECT_TRUE(table.add("entry1", 1000, 100));
    EXPECT_TRUE(table.add("entry2", 2000, 100));
    EXPECT_TRUE(table.add("entry3", 3000, 100));
    EXPECT_TRUE(table.add("entry4", 4000, 100));
    
    // Table is now full
    EXPECT_FALSE(table.add("entry5", 5000, 100));
    EXPECT_EQ(table.entry_count(), 4);
}

TEST_F(TableTest, Allocation) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    uint32_t initial = table.next_offset();
    
    // Allocate with default alignment (8)
    uint32_t offset1 = table.allocate(100);
    EXPECT_EQ(offset1, initial);
    EXPECT_EQ(table.next_offset(), initial + 100);
    
    // Allocate with specific alignment
    uint32_t offset2 = table.allocate(50, 16);
    EXPECT_EQ(offset2 % 16, 0); // Should be aligned to 16
    EXPECT_GE(offset2, initial + 100);
}

TEST_F(TableTest, OpenExistingTable) {
    // Create a table
    {
        Table table(buffer.data(), 64, buffer.size(), true);
        table.add("persistent", 1000, 500);
    }
    
    // Open the existing table
    {
        Table table(buffer.data(), 64, buffer.size(), false);
        EXPECT_EQ(table.entry_count(), 1);
        
        auto* entry = table.find("persistent");
        ASSERT_NE(entry, nullptr);
        EXPECT_STREQ(entry->name, "persistent");
        EXPECT_EQ(entry->offset, 1000);
        EXPECT_EQ(entry->size, 500);
    }
}

TEST_F(TableTest, InvalidMagicThrows) {
    // Create a table and corrupt the magic number
    Table table(buffer.data(), 64, buffer.size(), true);
    *reinterpret_cast<uint32_t*>(buffer.data()) = 0xDEADBEEF;
    
    EXPECT_THROW(Table(buffer.data(), 64, false), std::runtime_error);
}

TEST_F(TableTest, CalculateSize) {
    // Size should be header + entries
    size_t size_64 = Table::calculate_size(64);
    size_t size_128 = Table::calculate_size(128);
    
    EXPECT_EQ(size_64, sizeof(Table::Header) + 64 * sizeof(Table::Entry));
    EXPECT_EQ(size_128, sizeof(Table::Header) + 128 * sizeof(Table::Entry));
    EXPECT_GT(size_128, size_64);
}

TEST_F(TableTest, AlignmentWorks) {
    Table table(buffer.data(), 64, buffer.size(), true);
    
    // Start at unaligned position
    table.allocate(7); // Now next_offset is not aligned to 8
    
    // Request 8-byte alignment
    uint32_t aligned = table.allocate(100, 8);
    EXPECT_EQ(aligned % 8, 0);
    
    // Request 16-byte alignment
    table.allocate(7); // Misalign again
    uint32_t aligned16 = table.allocate(100, 16);
    EXPECT_EQ(aligned16 % 16, 0);
    
    // Request 64-byte alignment
    table.allocate(7); // Misalign again
    uint32_t aligned64 = table.allocate(100, 64);
    EXPECT_EQ(aligned64 % 64, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}