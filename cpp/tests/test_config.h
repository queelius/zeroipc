#pragma once

#include <chrono>
#include <cstdlib>

namespace zeroipc::test {

// Test timing configuration
// Can be overridden via environment variables for different test scenarios
struct TestTiming {
    // Thread synchronization delays
    static constexpr auto THREAD_START_DELAY = std::chrono::milliseconds(1);
    static constexpr auto THREAD_SYNC_DELAY = std::chrono::milliseconds(2);
    static constexpr auto CRITICAL_SECTION_DELAY = std::chrono::microseconds(10);

    // Timeout values
    static constexpr auto SHORT_TIMEOUT = std::chrono::milliseconds(50);
    static constexpr auto MEDIUM_TIMEOUT = std::chrono::milliseconds(100);
    static constexpr auto LONG_TIMEOUT = std::chrono::milliseconds(500);

    // Stress test parameters
    static constexpr int FAST_ITERATIONS = 100;
    static constexpr int MEDIUM_ITERATIONS = 1000;
    static constexpr int STRESS_ITERATIONS = 10000;

    static constexpr int FAST_THREADS = 4;
    static constexpr int MEDIUM_THREADS = 8;
    static constexpr int STRESS_THREADS = 32;

    // Get test mode from environment (FAST, MEDIUM, STRESS)
    static const char* test_mode() {
        const char* mode = std::getenv("ZEROIPC_TEST_MODE");
        return mode ? mode : "FAST";
    }

    // Check if running in CI environment
    static bool is_ci() {
        return std::getenv("CI") != nullptr ||
               std::getenv("CONTINUOUS_INTEGRATION") != nullptr;
    }

    // Multiplier for delays in CI (reduce flakiness by allowing more time)
    static int ci_multiplier() {
        return is_ci() ? 3 : 1;
    }
};

// Test categories for selective test execution
enum class TestCategory {
    FAST,      // <100ms - Core functionality, no delays
    MEDIUM,    // <5s - Multi-threaded, minimal delays
    SLOW,      // >5s - Full synchronization tests
    STRESS,    // >30s - Exhaustive stress testing
    INTEROP    // Cross-language integration
};

} // namespace zeroipc::test
