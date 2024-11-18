#include "shm_posix.h"
#include <iostream>

int main() {
    try {
        shm_posix shm("example_shm", 1024);

        std::cout << "Created shared memory segment: example_shm" << std::endl;
        std::cout << "Initial reference count: " << shm.get_ref_count() << std::endl;

        if (shm.push_back("test_entry", 0, 256, sizeof(int), 64)) {
            std::cout << "Added entry to shared memory" << std::endl;
        } else {
            std::cerr << "Failed to add entry" << std::endl;
        }

        std::cout << "Number of entries: " << shm.size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
