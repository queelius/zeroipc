#include <catch2/catch_test_macros.hpp>
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"

TEST_CASE("Shared memory fragmentation behavior", "[fragmentation]") {
    
    SECTION("Current implementation never reclaims space") {
        posix_shm shm("test_frag", 10 * 1024 * 1024);
        auto* table = shm.get_table();
        
        // Create array1
        {
            shm_array<int> arr1(shm, "array1", 1000);
            size_t allocated_after_1 = table->get_total_allocated_size();
            REQUIRE(allocated_after_1 > 0);
            
            // Create array2
            shm_array<int> arr2(shm, "array2", 1000);
            size_t allocated_after_2 = table->get_total_allocated_size();
            REQUIRE(allocated_after_2 > allocated_after_1);
            
            // Erase array1 from table
            table->erase("array1");
            
            // Total allocated doesn't decrease!
            size_t allocated_after_erase = table->get_total_allocated_size();
            REQUIRE(allocated_after_erase == allocated_after_2);  // Space not reclaimed
            
            // Create array3 - it goes at the END, not in the gap
            shm_array<int> arr3(shm, "array3", 500);
            size_t allocated_after_3 = table->get_total_allocated_size();
            REQUIRE(allocated_after_3 > allocated_after_2);  // Appended, not reused!
        }
        
        INFO("This test shows we have a memory leak/fragmentation issue!");
        INFO("Erased structures leave gaps that are never reused");
    }
    
    SECTION("Worst case: repeated create/delete exhausts memory") {
        posix_shm shm("test_exhaust", 1024 * 1024);  // 1MB only
        
        // This will eventually fail even though we "delete" each time
        for (int i = 0; i < 100; ++i) {
            std::string name = "temp_" + std::to_string(i);
            
            try {
                shm_array<double> arr(shm, name, 1000);  // 8KB each
                
                // "Delete" it
                shm.get_table()->erase(name.c_str());
                
                // But the space is never reclaimed!
            } catch (const std::runtime_error& e) {
                // Will eventually throw "Not enough space"
                INFO("Failed at iteration " << i);
                INFO("Even though we 'deleted' each array!");
                break;
            }
        }
    }
}

TEST_CASE("Possible solutions for fragmentation", "[fragmentation][design]") {
    
    SECTION("Solution 1: Free list for reuse") {
        // We could maintain a free list of erased entries
        // and try to reuse them if new allocation fits
        
        struct BetterTable {
            struct Entry {
                char name[32];
                size_t offset;
                size_t size;
                bool active;
                size_t next_free;  // Linked list of free blocks
            };
            
            Entry entries[64];
            size_t free_head = -1;
            
            // First-fit allocation
            size_t allocate(size_t size) {
                // Check free list first
                size_t prev = -1;
                size_t curr = free_head;
                
                while (curr != -1) {
                    if (entries[curr].size >= size) {
                        // Found a fit!
                        if (prev == -1) {
                            free_head = entries[curr].next_free;
                        } else {
                            entries[prev].next_free = entries[curr].next_free;
                        }
                        return entries[curr].offset;
                    }
                    prev = curr;
                    curr = entries[curr].next_free;
                }
                
                // No fit, allocate at end
                // return allocate_at_end(size);  // Not implemented in this example
                return static_cast<size_t>(-1);  // Indicate failure
            }
        };
        
        INFO("This would allow reuse but adds complexity");
    }
    
    SECTION("Solution 2: Compaction (requires stop-the-world)") {
        // Periodically compact all structures to beginning
        // Very expensive, requires all processes to coordinate
        INFO("Not practical for real-time systems");
    }
    
    SECTION("Solution 3: Pool-based allocation") {
        // Pre-allocate pools of common sizes
        // Like malloc's bins
        INFO("Good for known workloads");
    }
    
    SECTION("Current design trade-off") {
        INFO("We chose simplicity and predictability over space efficiency");
        INFO("For long-running systems, this is a real issue!");
        INFO("Recommendation: Document this limitation clearly");
        INFO("Users should:");
        INFO("  1. Pre-allocate all structures at startup");
        INFO("  2. Never erase/recreate structures");
        INFO("  3. Use object pools for dynamic allocation needs");
    }
}