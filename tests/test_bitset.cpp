#include <catch2/catch_test_macros.hpp>
#include "zeroipc.h"
#include "bitset.h"
#include <thread>
#include <vector>
#include <bitset>

TEST_CASE("zeroipc::bitset basic operations", "[zeroipc::bitset]") {
    const std::string shm_name = "/test_bitset_basic";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Create and use bitset") {
        zeroipc::bitset<100> bits(shm, "test_bits");
        
        REQUIRE(bits.size() == 100);
        REQUIRE(bits.count() == 0);
        REQUIRE(bits.none());
        REQUIRE(!bits.any());
        REQUIRE(!bits.all());
    }

    SECTION("Set and test individual bits") {
        zeroipc::bitset<64> bits(shm, "individual_bits");
        
        bits.set(0);
        bits.set(10);
        bits.set(63);
        
        REQUIRE(bits.test(0));
        REQUIRE(bits.test(10));
        REQUIRE(bits.test(63));
        REQUIRE(!bits.test(1));
        REQUIRE(!bits.test(32));
        
        REQUIRE(bits.count() == 3);
        REQUIRE(bits.any());
        REQUIRE(!bits.none());
        REQUIRE(!bits.all());
    }

    SECTION("Reset bits") {
        zeroipc::bitset<32> bits(shm, "reset_bits");
        
        bits.set(5);
        bits.set(10);
        REQUIRE(bits.count() == 2);
        
        bits.reset(5);
        REQUIRE(!bits.test(5));
        REQUIRE(bits.test(10));
        REQUIRE(bits.count() == 1);
        
        bits.reset(10);
        REQUIRE(bits.none());
    }

    SECTION("Flip bits") {
        zeroipc::bitset<16> bits(shm, "flip_bits");
        
        bits.flip(3);
        REQUIRE(bits.test(3));
        REQUIRE(bits.count() == 1);
        
        bits.flip(3);
        REQUIRE(!bits.test(3));
        REQUIRE(bits.count() == 0);
    }

    SECTION("Set with value") {
        zeroipc::bitset<8> bits(shm, "set_value");
        
        bits.set(2, true);
        REQUIRE(bits.test(2));
        
        bits.set(2, false);
        REQUIRE(!bits.test(2));
    }

    SECTION("Set all bits") {
        zeroipc::bitset<100> bits(shm, "set_all");
        
        bits.set();
        REQUIRE(bits.count() == 100);
        REQUIRE(bits.all());
        REQUIRE(!bits.none());
        
        // Individual bits should all be set
        for (size_t i = 0; i < 100; ++i) {
            REQUIRE(bits.test(i));
        }
    }

    SECTION("Reset all bits") {
        zeroipc::bitset<50> bits(shm, "reset_all");
        
        // Set some bits first
        for (size_t i = 0; i < 50; i += 3) {
            bits.set(i);
        }
        REQUIRE(bits.any());
        
        bits.reset();
        REQUIRE(bits.count() == 0);
        REQUIRE(bits.none());
        REQUIRE(!bits.any());
    }

    SECTION("Flip all bits") {
        zeroipc::bitset<10> bits(shm, "flip_all");
        
        bits.set(0);
        bits.set(2);
        bits.set(4);
        REQUIRE(bits.count() == 3);
        
        bits.flip();
        REQUIRE(bits.count() == 7);
        REQUIRE(!bits.test(0));
        REQUIRE(bits.test(1));
        REQUIRE(!bits.test(2));
        REQUIRE(bits.test(3));
    }

    SECTION("Find first set bit") {
        zeroipc::bitset<128> bits(shm, "find_first");
        
        REQUIRE(bits.find_first() == 128);  // No bits set
        
        bits.set(50);
        bits.set(30);
        bits.set(70);
        
        REQUIRE(bits.find_first() == 30);
    }

    SECTION("Find next set bit") {
        zeroipc::bitset<100> bits(shm, "find_next");
        
        bits.set(10);
        bits.set(20);
        bits.set(30);
        bits.set(40);
        
        size_t pos = bits.find_first();
        REQUIRE(pos == 10);
        
        pos = bits.find_next(pos);
        REQUIRE(pos == 20);
        
        pos = bits.find_next(pos);
        REQUIRE(pos == 30);
        
        pos = bits.find_next(pos);
        REQUIRE(pos == 40);
        
        pos = bits.find_next(pos);
        REQUIRE(pos == 100);  // No more bits
    }

    shm.unlink();
}

TEST_CASE("zeroipc::bitset bitwise operations", "[zeroipc::bitset]") {
    const std::string shm_name = "/test_bitset_bitwise";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    zeroipc::bitset<64> bits1(shm, "bits1");
    zeroipc::bitset<64> bits2(shm, "bits2");
    
    // Setup bit patterns
    for (size_t i = 0; i < 64; i += 2) {
        bits1.set(i);  // Even bits
    }
    for (size_t i = 0; i < 64; i += 3) {
        bits2.set(i);  // Every third bit
    }

    SECTION("Bitwise AND") {
        zeroipc::bitset<64> result(shm, "and_result");
        result.set();  // Start with all 1s
        
        result &= bits1;
        result &= bits2;
        
        // Should only have bits set where both bits1 and bits2 are set
        for (size_t i = 0; i < 64; ++i) {
            bool expected = (i % 2 == 0) && (i % 3 == 0);
            REQUIRE(result.test(i) == expected);
        }
    }

    SECTION("Bitwise OR") {
        zeroipc::bitset<64> result(shm, "or_result");
        
        result |= bits1;
        result |= bits2;
        
        // Should have bits set where either bits1 or bits2 are set
        for (size_t i = 0; i < 64; ++i) {
            bool expected = (i % 2 == 0) || (i % 3 == 0);
            REQUIRE(result.test(i) == expected);
        }
    }

    SECTION("Bitwise XOR") {
        zeroipc::bitset<64> result(shm, "xor_result");
        result |= bits1;  // Copy bits1
        
        result ^= bits2;
        
        // Should have bits set where bits1 XOR bits2
        for (size_t i = 0; i < 64; ++i) {
            bool expected = (i % 2 == 0) != (i % 3 == 0);
            REQUIRE(result.test(i) == expected);
        }
    }

    shm.unlink();
}

TEST_CASE("zeroipc::bitset concurrent operations", "[zeroipc::bitset][concurrent]") {
    const std::string shm_name = "/test_bitset_concurrent";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Concurrent bit setting") {
        zeroipc::bitset<10000> bits(shm, "concurrent_set");
        const int num_threads = 4;
        const int bits_per_thread = 2500;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&bits, t, bits_per_thread]() {
                for (int i = 0; i < bits_per_thread; ++i) {
                    bits.set(t * bits_per_thread + i);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        REQUIRE(bits.count() == 10000);
        REQUIRE(bits.all());
    }

    SECTION("Concurrent flipping") {
        zeroipc::bitset<1000> bits(shm, "concurrent_flip");
        const int num_threads = 4;
        const int flips_per_thread = 1000;
        
        std::vector<std::thread> threads;
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&bits, t, flips_per_thread]() {
                for (int i = 0; i < flips_per_thread; ++i) {
                    // Each thread flips its own range
                    size_t bit = (t * 250) + (i % 250);
                    bits.flip(bit);
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Each bit flipped 4 times (1000/250), so should be back to 0
        REQUIRE(bits.none());
    }

    SECTION("Concurrent read/write") {
        zeroipc::bitset<10000> bits(shm, "concurrent_rw");
        
        // Pre-set some bits
        for (size_t i = 0; i < 10000; i += 2) {
            bits.set(i);
        }
        
        std::atomic<int> read_count{0};
        std::vector<std::thread> threads;
        
        // Writers
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&bits, t]() {
                for (int i = 0; i < 5000; ++i) {
                    bits.flip(t * 5000 + i);
                }
            });
        }
        
        // Readers
        for (int t = 0; t < 2; ++t) {
            threads.emplace_back([&bits, &read_count]() {
                for (int i = 0; i < 10000; ++i) {
                    for (size_t j = 0; j < 10000; ++j) {
                        if (bits.test(j)) {
                            read_count++;
                        }
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Readers should have seen some bits set
        REQUIRE(read_count > 0);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::bitset edge cases", "[zeroipc::bitset]") {
    const std::string shm_name = "/test_bitset_edge";
    shm_unlink(shm_name.c_str());
    zeroipc::memory shm(shm_name, 10 * 1024 * 1024);

    SECTION("Non-64-bit aligned size") {
        zeroipc::bitset<73> bits(shm, "odd_size");
        
        // Should handle bits near boundary correctly
        bits.set(63);
        bits.set(64);
        bits.set(72);
        
        REQUIRE(bits.test(63));
        REQUIRE(bits.test(64));
        REQUIRE(bits.test(72));
        
        // Out of bounds should be safe
        bits.set(100);  // Should do nothing
        REQUIRE(!bits.test(73));
        
        // Set all should respect size
        bits.set();
        REQUIRE(bits.count() == 73);
    }

    SECTION("Single bit") {
        zeroipc::bitset<1> single(shm, "single_bit");
        
        REQUIRE(!single.test(0));
        single.set(0);
        REQUIRE(single.test(0));
        REQUIRE(single.all());
        REQUIRE(single.any());
        REQUIRE(!single.none());
        
        single.flip();
        REQUIRE(single.none());
    }

    SECTION("Large bitset") {
        zeroipc::bitset<100000> large(shm, "large_bits");
        
        // Set pattern
        for (size_t i = 0; i < 100000; i += 1000) {
            large.set(i);
        }
        
        REQUIRE(large.count() == 100);
        
        // Find operations should work
        REQUIRE(large.find_first() == 0);
        REQUIRE(large.find_next(0) == 1000);
    }

    shm.unlink();
}

TEST_CASE("zeroipc::bitset cross-process", "[zeroipc::bitset][process]") {
    const std::string shm_name = "/test_bitset_process";
    shm_unlink(shm_name.c_str());
    
    SECTION("Bitset persistence across processes") {
        // Process 1: Create and set bits
        {
            zeroipc::memory shm1(shm_name, 1024 * 1024);
            zeroipc::bitset<256> bits(shm1, "persistent_bits");
            
            for (size_t i = 0; i < 256; i += 4) {
                bits.set(i);
            }
            
            REQUIRE(bits.count() == 64);
        }
        
        // Process 2: Open and verify
        {
            zeroipc::memory shm2(shm_name, 0);  // Attach only
            zeroipc::bitset<256> bits(shm2, "persistent_bits");
            
            REQUIRE(bits.count() == 64);
            
            for (size_t i = 0; i < 256; ++i) {
                if (i % 4 == 0) {
                    REQUIRE(bits.test(i));
                } else {
                    REQUIRE(!bits.test(i));
                }
            }
            
            // Modify
            bits.flip();
        }
        
        // Process 3: Verify modifications
        {
            zeroipc::memory shm3(shm_name, 0);
            zeroipc::bitset<256> bits(shm3, "persistent_bits");
            
            REQUIRE(bits.count() == 192);  // 256 - 64
            
            for (size_t i = 0; i < 256; ++i) {
                if (i % 4 == 0) {
                    REQUIRE(!bits.test(i));  // These were flipped
                } else {
                    REQUIRE(bits.test(i));   // These are now set
                }
            }
        }
    }
    
    shm_unlink(shm_name.c_str());
}