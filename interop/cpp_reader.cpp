#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

struct Coordinate {
    float lat, lon;
};

int main() {
    std::cout << "=== C++ Reader ===" << std::endl;

    try {
        // Open existing shared memory written by Python
        zeroipc::Memory mem("/interop_test_py");

        std::cout << "Opened shared memory: " << mem.table()->entry_count() << " structures" << std::endl;

        // Read Fibonacci sequence (matches python_writer.py)
        zeroipc::Array<int32_t> fibonacci(mem, "fibonacci");
        std::cout << "\nfibonacci: ";
        int expected_fib[] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34};
        bool fib_ok = true;
        for (size_t i = 0; i < fibonacci.capacity(); i++) {
            std::cout << fibonacci[i] << " ";
            if (fibonacci[i] != expected_fib[i]) {
                std::cerr << "\nERROR: fibonacci[" << i << "] = " << fibonacci[i]
                          << ", expected " << expected_fib[i] << std::endl;
                fib_ok = false;
            }
        }
        std::cout << (fib_ok ? " [OK]" : " [FAIL]") << std::endl;

        // Read reciprocals
        zeroipc::Array<float> reciprocals(mem, "reciprocals");
        std::cout << "reciprocals: ";
        bool recip_ok = true;
        for (size_t i = 0; i < reciprocals.capacity(); i++) {
            std::cout << reciprocals[i] << " ";
            float expected = 1.0f / (i + 1);
            if (std::fabs(reciprocals[i] - expected) > 1e-6f) {
                std::cerr << "\nERROR: reciprocals[" << i << "] = " << reciprocals[i]
                          << ", expected " << expected << std::endl;
                recip_ok = false;
            }
        }
        std::cout << (recip_ok ? " [OK]" : " [FAIL]") << std::endl;

        // Read coordinates as structured array
        zeroipc::Array<Coordinate> coords(mem, "coordinates");
        std::cout << "coordinates:" << std::endl;
        const char* cities[] = {"San Francisco", "New York", "London"};
        for (size_t i = 0; i < coords.capacity(); i++) {
            std::cout << "  " << cities[i] << ": lat=" << coords[i].lat
                      << ", lon=" << coords[i].lon << std::endl;
        }

        if (fib_ok && recip_ok) {
            std::cout << "\nCross-language test PASSED!" << std::endl;
        } else {
            std::cout << "\nCross-language test FAILED!" << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "C++ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
