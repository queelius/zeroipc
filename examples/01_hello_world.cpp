/**
 * Example: Hello World
 * 
 * Description: Demonstrates basic shared memory array creation and access
 * between two processes.
 * 
 * Usage:
 *   Terminal 1: ./01_hello_world writer
 *   Terminal 2: ./01_hello_world reader
 * 
 * Expected Output:
 *   Writer: Creates array and writes "Hello, World!" as ASCII values
 *   Reader: Reads array and prints the message
 */

#include <zeroipc/memory.h>
#include <zeroipc/array.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

using namespace zeroipc;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [writer|reader]" << std::endl;
        return 1;
    }
    
    std::string role = argv[1];
    
    try {
        if (role == "writer") {
            // Create shared memory segment
            std::cout << "Writer: Creating shared memory /hello_world" << std::endl;
            memory mem("/hello_world", 1024 * 1024);  // 1MB
            
            // Create array for message
            const char message[] = "Hello, World!";
            size_t msg_len = sizeof(message);
            
            array<char> msg_array(mem, "message", msg_len);
            
            // Write message to array
            for (size_t i = 0; i < msg_len; i++) {
                msg_array[i] = message[i];
            }
            
            std::cout << "Writer: Message written: " << message << std::endl;
            std::cout << "Writer: Array size: " << msg_array.size() << " bytes" << std::endl;
            
            // Keep alive for reader
            std::cout << "Writer: Press Enter to exit..." << std::endl;
            std::cin.get();
            
        } else if (role == "reader") {
            // Open existing shared memory
            std::cout << "Reader: Opening shared memory /hello_world" << std::endl;
            memory mem("/hello_world");
            
            // Open existing array
            array<char> msg_array(mem, "message");
            
            // Read and print message
            std::cout << "Reader: Message found with " << msg_array.size() << " bytes" << std::endl;
            std::cout << "Reader: Message content: ";
            
            for (size_t i = 0; i < msg_array.size(); i++) {
                std::cout << msg_array[i];
            }
            std::cout << std::endl;
            
            // Show individual bytes
            std::cout << "Reader: ASCII values: ";
            for (size_t i = 0; i < msg_array.size() && msg_array[i] != '\0'; i++) {
                std::cout << static_cast<int>(msg_array[i]) << " ";
            }
            std::cout << std::endl;
            
        } else {
            std::cerr << "Unknown role: " << role << std::endl;
            std::cerr << "Use 'writer' or 'reader'" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}