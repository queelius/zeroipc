#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>
#include <cmath>

struct Point3D {
    float x, y, z;
};

int main() {
    std::cout << "=== C++ Writer ===" << std::endl;

    // Clean up any leftover shared memory
    zeroipc::Memory::unlink("/interop_test");

    // Create shared memory (1MB)
    zeroipc::Memory mem("/interop_test", 1024 * 1024);

    // Create integer array with squares (matches python_reader.py expectations)
    zeroipc::Array<int32_t> int_array(mem, "int_array", 10);
    for (int i = 0; i < 10; i++) {
        int_array[i] = i * i;
    }
    std::cout << "  int_array (squares): ";
    for (int i = 0; i < 10; i++) {
        std::cout << int_array[i] << " ";
    }
    std::cout << std::endl;

    // Create float array
    zeroipc::Array<float> float_array(mem, "float_array", 5);
    float_array[0] = 1.0f;
    float_array[1] = 2.5f;
    float_array[2] = 3.14f;
    float_array[3] = -1.0f;
    float_array[4] = 0.0f;
    std::cout << "  float_array: ";
    for (int i = 0; i < 5; i++) {
        std::cout << float_array[i] << " ";
    }
    std::cout << std::endl;

    // Create points as structured array (3 floats per point)
    zeroipc::Array<Point3D> points(mem, "points", 3);
    points[0] = {1.0f, 2.0f, 3.0f};
    points[1] = {4.0f, 5.0f, 6.0f};
    points[2] = {7.0f, 8.0f, 9.0f};
    std::cout << "  points: ";
    for (int i = 0; i < 3; i++) {
        std::cout << "(" << points[i].x << ", " << points[i].y << ", " << points[i].z << ") ";
    }
    std::cout << std::endl;

    std::cout << "\nC++ writer created 3 structures in /interop_test" << std::endl;

    return 0;
}
