#include "shm_posix.h"
#include <iostream>

int main() {
    try {
        shm_posix shm("example_shm");

        std::cout << "Opened shared memory segment: example_shm" << std::endl;
        std::cout << "Current reference count: " << shm.get_ref_count() << std::endl;

        auto entry = shm.find("test_entry");
        if (entry) {
            std::cout << "Found entry: " << entry->name.data() << std::endl;
            std::cout << "Offset: " << entry->offset << ", Size: " << entry->size << std::endl;
        } else {
            std::cerr << "Entry not found" << std::endl;
        }

        std::cout << "Number of entries: " << shm.size() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }

    return 0;
}
