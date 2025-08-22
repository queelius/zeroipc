#include <iostream>
#include "zeroipc.h"
#include "array.h"
#include "queue.h"

int main() {
    zeroipc::memory shm("/test_table_limit", 5 * 1024 * 1024);
    auto* table = shm.get_table();
    
    std::cout << "Table max entries: " << table->MAX_ENTRIES << std::endl;
    std::cout << "Initial entries: " << table->get_entry_count() << std::endl;
    
    int array_count = 0;
    for (int i = 0; i < 100; i++) {
        try {
            zeroipc::array<int> arr(shm, "arr_" + std::to_string(i), 100);
            array_count++;
        } catch (const std::exception& e) {
            std::cout << "Failed to create array " << i << ": " << e.what() << std::endl;
            break;
        }
    }
    
    std::cout << "Created " << array_count << " arrays" << std::endl;
    std::cout << "Table entries used: " << table->get_entry_count() << std::endl;
    std::cout << "Table entries available: " << table->get_available_entries() << std::endl;
    
    int queue_count = 0;
    for (int i = 0; i < 100; i++) {
        try {
            zeroipc::queue<int> q(shm, "que_" + std::to_string(i), 50);
            queue_count++;
        } catch (const std::exception& e) {
            std::cout << "Failed to create queue " << i << ": " << e.what() << std::endl;
            break;
        }
    }
    
    std::cout << "Created " << queue_count << " queues" << std::endl;
    std::cout << "Final table entries: " << table->get_entry_count() << std::endl;
    
    shm.unlink();
    return 0;
}