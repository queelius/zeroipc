#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include "zeroipc.h"
#include "array.h"
#include "queue.h"

int main() {
    try {
        // Create or open shared memory segment
        constexpr size_t shm_size = 1024 * 1024; // 1MB
        zeroipc::memory shm("my_simulation_shm", shm_size);
        
        std::cout << "Shared memory created/opened successfully\n";
        
        // Example 1: Using zeroipc::array for fixed-size data exchange
        {
            constexpr size_t array_size = 100;
            zeroipc::array<double> sensor_data(shm, "sensor_readings", array_size);
            
            // Simulate writing sensor data
            for (size_t i = 0; i < 10; ++i) {
                sensor_data[i] = 3.14 * i;
            }
            
            std::cout << "Written sensor data: ";
            for (size_t i = 0; i < 10; ++i) {
                std::cout << sensor_data[i] << " ";
            }
            std::cout << "\n";
        }
        
        // Example 2: Discovery of existing array
        {
            // Another process could discover this array by name
            zeroipc::array<double> discovered_array(shm, "sensor_readings");
            std::cout << "Discovered array size: " << discovered_array.size() << "\n";
            std::cout << "First value: " << discovered_array[0] << "\n";
        }
        
        // Example 3: Using zeroipc::queue for message passing
        {
            struct SimulationMessage {
                uint64_t timestamp;
                double value;
                uint32_t sensor_id;
            };
            
            constexpr size_t queue_capacity = 50;
            zeroipc::queue<SimulationMessage> msg_queue(shm, "sim_messages", queue_capacity);
            
            // Enqueue some messages
            SimulationMessage msg1{1000, 42.5, 1};
            SimulationMessage msg2{2000, 37.2, 2};
            
            if (msg_queue.enqueue(msg1)) {
                std::cout << "Message 1 enqueued successfully\n";
            }
            if (msg_queue.enqueue(msg2)) {
                std::cout << "Message 2 enqueued successfully\n";
            }
            
            // Dequeue and display
            SimulationMessage received;
            if (msg_queue.dequeue(received)) {
                std::cout << "Dequeued message - Timestamp: " << received.timestamp 
                          << ", Value: " << received.value 
                          << ", Sensor: " << received.sensor_id << "\n";
            }
            
            std::cout << "Queue size: " << msg_queue.size() << "\n";
        }
        
        // The shared memory will be automatically cleaned up when the last reference is released
        std::cout << "\nShared memory operations completed successfully!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}