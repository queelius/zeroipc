#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <zeroipc/queue.h>
#include <iostream>
#include <cstring>

// Simple structure compatible with Python
struct DataPoint {
    float value;
    uint32_t id;
};

int main() {
    std::cout << "=== Simple C++ Interop Test ===" << std::endl;

    // Open existing memory created by Python
    zeroipc::Memory mem("/simple_interop");

    // Access array created by Python
    zeroipc::Array<float> arr(mem, "float_array");
    std::cout << "Found float array with capacity: " << arr.capacity() << std::endl;

    // Read values
    for (size_t i = 0; i < 5 && i < arr.capacity(); i++) {
        std::cout << "  [" << i << "] = " << arr[i] << std::endl;
    }

    // Modify a value
    arr[0] = 99.99f;
    std::cout << "Modified arr[0] to 99.99" << std::endl;

    // Access queue
    zeroipc::Queue<int32_t> queue(mem, "int_queue");
    std::cout << "Found queue with size: " << queue.size() << std::endl;

    // Add some values
    queue.push(100);
    queue.push(200);
    std::cout << "Added values 100, 200 to queue" << std::endl;

    std::cout << "C++ test complete" << std::endl;
    return 0;
}