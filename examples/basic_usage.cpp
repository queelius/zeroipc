#include <iostream>
#include <vector>
#include "SharedMemoryManager.h"
#include "SharedMemoryAllocator.h"

int main() {
    try {
        SharedMemoryManager smm("/my_shared_memory", 1024 * 1024);  // 1MB
        SharedMemoryAllocator<int> allocator(smm);

        std::vector<int, SharedMemoryAllocator<int>> shared_vector(allocator);

        shared_vector.push_back(42);
        shared_vector.push_back(100);

        std::cout << "Vector size: " << shared_vector.size() << std::endl;
        std::cout << "Vector contents: ";
        for (int i : shared_vector) {
            std::cout << i << " ";
        }
        std::cout << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}