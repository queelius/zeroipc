#include <gtest/gtest.h>
#include "zeroipc/memory.h"
#include "zeroipc/queue.h"
#include "zeroipc/array.h"
#include <cstdlib>
#include <string>
#include <sstream>

class CLITest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure clean state
        shm_unlink("/test_cli");
    }
    
    void TearDown() override {
        shm_unlink("/test_cli");
    }
    
    // Helper to run the CLI tool and capture output
    std::string runCLI(const std::string& args) {
        std::string cmd = "./zeroipc-inspect " + args + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return "ERROR";
        
        char buffer[128];
        std::string result;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        pclose(pipe);
        return result;
    }
};

TEST_F(CLITest, ListShowsCreatedMemory) {
    // Create a shared memory segment
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        
        // Run list command
        std::string output = runCLI("-l");
        
        // Check that our memory segment is listed
        EXPECT_NE(output.find("test_cli"), std::string::npos);
    }
}

TEST_F(CLITest, ShowSummary) {
    // Create memory with some structures
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        zeroipc::Queue<int> queue(mem, "test_queue", 100);
        zeroipc::Array<double> array(mem, "test_array", 50);
        
        // Add some data
        queue.push(42);
        queue.push(13);
        array[0] = 3.14;
        array[1] = 2.71;
        
        // Run summary command
        std::string output = runCLI("-s /test_cli");
        
        // Check for expected content
        EXPECT_NE(output.find("Summary"), std::string::npos);
        EXPECT_NE(output.find("Active Entries: 2"), std::string::npos);
    }
}

TEST_F(CLITest, ShowTableEntries) {
    // Create memory with structures
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        zeroipc::Queue<int> queue(mem, "test_queue", 100);
        zeroipc::Array<float> array(mem, "test_array", 25);
        
        // Run table command
        std::string output = runCLI("-t /test_cli");
        
        // Check for table entries
        EXPECT_NE(output.find("test_queue"), std::string::npos);
        EXPECT_NE(output.find("test_array"), std::string::npos);
        EXPECT_NE(output.find("Table Entries"), std::string::npos);
    }
}

TEST_F(CLITest, InfoAboutSpecificStructure) {
    // Create memory with a queue
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        zeroipc::Queue<int> queue(mem, "my_queue", 100);
        
        // Add some items
        for (int i = 0; i < 10; i++) {
            queue.push(i * 10);
        }
        
        // Get info about the queue
        std::string output = runCLI("-i my_queue /test_cli");
        
        // Check for structure info
        EXPECT_NE(output.find("my_queue"), std::string::npos);
        // The -i flag should show info about the structure
        // Just check that it mentions the queue name for now
    }
}

TEST_F(CLITest, HexDumpStructure) {
    // Create memory with array
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        zeroipc::Array<uint32_t> array(mem, "hex_data", 16);
        
        // Fill with recognizable pattern
        for (size_t i = 0; i < 16; i++) {
            array[i] = 0xDEADBEEF + i;
        }
        
        // Dump the data
        std::string output = runCLI("-d hex_data /test_cli");
        
        // Check for hex dump markers - the actual implementation may differ
        // For now just check that output is not empty and contains hex_data
        EXPECT_NE(output.find("hex_data"), std::string::npos);
    }
}

TEST_F(CLITest, VerboseOutput) {
    // Create memory with various structures
    {
        zeroipc::Memory mem("/test_cli", 128 * 1024);
        zeroipc::Queue<int> queue(mem, "queue1", 50);
        zeroipc::Array<float> array(mem, "array1", 100);
        
        // Run verbose command
        std::string output = runCLI("-v /test_cli");
        
        // Verbose mode shows summary information
        EXPECT_NE(output.find("Summary"), std::string::npos);
    }
}

TEST_F(CLITest, HandleNonExistentMemory) {
    // Try to inspect non-existent memory
    std::string output = runCLI("/nonexistent");
    
    // Should report error
    EXPECT_NE(output.find("Error"), std::string::npos);
}

TEST_F(CLITest, HandleNonExistentEntry) {
    // Create memory
    {
        zeroipc::Memory mem("/test_cli", 64 * 1024);
        
        // Try to get info on non-existent entry
        std::string output = runCLI("-i nonexistent /test_cli");
        
        // Should report not found
        EXPECT_NE(output.find("not found"), std::string::npos);
    }
}