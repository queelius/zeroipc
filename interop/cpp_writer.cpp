#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>
#include <cstring>

int main() {
    try {
        // Create shared memory with some test data
        zeroipc::Memory mem("/interop_test", 1024 * 1024);  // 1MB
        
        // Create an integer array
        zeroipc::Array<int> int_array(mem, "int_array", 10);
        for (int i = 0; i < 10; ++i) {
            int_array[i] = i * i;  // Store squares
        }
        
        // Create a float array
        zeroipc::Array<float> float_array(mem, "float_array", 5);
        float_array[0] = 3.14159f;
        float_array[1] = 2.71828f;
        float_array[2] = 1.41421f;
        float_array[3] = 1.61803f;
        float_array[4] = 2.23606f;
        
        // Create a struct array
        struct Point {
            float x, y, z;
        };
        
        zeroipc::Array<Point> points(mem, "points", 3);
        points[0] = {1.0f, 2.0f, 3.0f};
        points[1] = {4.0f, 5.0f, 6.0f};
        points[2] = {7.0f, 8.0f, 9.0f};
        
        std::cout << "C++ writer created:" << std::endl;
        std::cout << "  - int_array: 10 integers (squares)" << std::endl;
        std::cout << "  - float_array: 5 floats (math constants)" << std::endl;
        std::cout << "  - points: 3 3D points" << std::endl;
        std::cout << "Shared memory name: /interop_test" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}