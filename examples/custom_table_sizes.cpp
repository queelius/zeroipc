#include <iostream>
#include "posix_shm.h"
#include "shm_array.h"
#include "shm_queue.h"

// Example showing how to use custom table sizes for different use cases

void example_minimal_overhead() {
    std::cout << "=== Minimal Overhead Configuration ===\n";
    
    // Use small table: 16-char names, 16 max entries
    // Good for embedded systems or when you know you'll have few structures
    posix_shm_small shm("minimal_shm", 64 * 1024); // 64KB
    
    std::cout << "Table overhead: " << sizeof(shm_table_small) << " bytes\n";
    std::cout << "Max name length: " << shm_table_small::MAX_NAME_SIZE << "\n";
    std::cout << "Max entries: " << shm_table_small::MAX_ENTRIES << "\n\n";
    
    // Arrays and queues automatically use the same table type
    shm_array<int, shm_table_small> small_array(shm, "data", 100);
    small_array[0] = 42;
    
    std::cout << "Created array '" << small_array.name() << "' with " 
              << small_array.size() << " elements\n\n";
}

void example_large_simulation() {
    std::cout << "=== Large Simulation Configuration ===\n";
    
    // Use large table: 64-char names, 256 max entries
    // Good for complex simulations with many data structures
    posix_shm_large shm("simulation_shm", 100 * 1024 * 1024); // 100MB
    
    std::cout << "Table overhead: " << sizeof(shm_table_large) << " bytes\n";
    std::cout << "Max name length: " << shm_table_large::MAX_NAME_SIZE << "\n";
    std::cout << "Max entries: " << shm_table_large::MAX_ENTRIES << "\n\n";
    
    // Can have descriptive names and many structures
    shm_array<double, shm_table_large> sensor_data(
        shm, "sensor_data_from_camera_01_preprocessed", 1000);
    
    shm_queue<float, shm_table_large> event_queue(
        shm, "high_priority_event_queue_for_controller", 500);
    
    std::cout << "Created structures with long descriptive names:\n";
    std::cout << "  - " << sensor_data.name() << "\n";
    std::cout << "  - " << event_queue.name() << "\n\n";
}

void example_custom_config() {
    std::cout << "=== Custom Configuration ===\n";
    
    // Define your own configuration
    using my_custom_table = shm_table_impl<48, 128>;
    using my_custom_shm = posix_shm_impl<my_custom_table>;
    
    my_custom_shm shm("custom_shm", 10 * 1024 * 1024); // 10MB
    
    std::cout << "Custom table overhead: " << sizeof(my_custom_table) << " bytes\n";
    std::cout << "Max name length: " << my_custom_table::MAX_NAME_SIZE << "\n";
    std::cout << "Max entries: " << my_custom_table::MAX_ENTRIES << "\n\n";
    
    shm_array<uint64_t, my_custom_table> timestamps(shm, "timestamps", 1000);
    timestamps[0] = 123456789;
    
    std::cout << "Custom configuration working with " << timestamps.size() 
              << " timestamps\n\n";
}

int main() {
    try {
        example_minimal_overhead();
        example_large_simulation();
        example_custom_config();
        
        std::cout << "All custom table configurations work successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}