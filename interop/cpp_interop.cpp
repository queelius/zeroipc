#include <zeroipc/memory.h>
#include <zeroipc/queue.h>
#include <zeroipc/stack.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

struct Event {
    uint32_t event_id;
    uint32_t source_pid;
    uint64_t timestamp;
    char message[48];
};

void read_and_modify() {
    std::cout << "\n=== C++ Consumer: Processing shared structures ===" << std::endl;

    try {
        // Open existing memory
        zeroipc::Memory mem("/zeroipc_interop");
        std::cout << "Opened shared memory with " << mem.count() << " structures\n" << std::endl;

        // Access sensor array
        zeroipc::Array<SensorData> sensors(mem, "sensor_array");
        std::cout << "Sensor Array:" << std::endl;
        std::cout << "  Capacity: " << sensors.capacity() << std::endl;

        // Modify some sensors
        for (size_t i = 0; i < 5; i++) {
            SensorData& data = sensors[i];
            data.temperature += 0.5f;  // Add 0.5°C from C++
            std::cout << "  Modified sensor[" << i << "]: temp="
                      << data.temperature << "°C" << std::endl;
        }

        // Access event queue
        zeroipc::Queue<Event> events(mem, "event_queue");
        std::cout << "\nEvent Queue:" << std::endl;
        std::cout << "  Size: " << events.size() << std::endl;

        // Pop and display some events
        Event evt;
        for (int i = 0; i < 2 && !events.empty(); i++) {
            auto opt_evt = events.pop();
            if (opt_evt) {
                evt = *opt_evt;
                std::cout << "  Popped: " << evt.message << std::endl;
            }
        }

        // Add C++ event
        Event cpp_evt;
        cpp_evt.event_id = 5000;
        cpp_evt.source_pid = getpid();
        cpp_evt.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::strcpy(cpp_evt.message, "C++ Event from interop test");

        if (events.push(cpp_evt)) {
            std::cout << "  Pushed new C++ event" << std::endl;
        }

        // Access task stack
        zeroipc::Stack<uint32_t> tasks(mem, "task_stack");
        std::cout << "\nTask Stack:" << std::endl;
        std::cout << "  Size: " << tasks.size() << std::endl;

        auto task = tasks.pop();
        if (task) {
            std::cout << "  Popped task ID: " << *task << std::endl;
        }

        // Push C++ task
        tasks.push(777);
        std::cout << "  Pushed C++ task ID: 777" << std::endl;

        // Access statistics
        zeroipc::Array<double> stats(mem, "statistics");
        std::cout << "\nStatistics Array:" << std::endl;
        double sum = 0.0;
        for (size_t i = 0; i < stats.capacity(); i++) {
            sum += stats[i];
        }
        std::cout << "  Average value: " << (sum / stats.capacity()) << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "C++ Error: " << e.what() << std::endl;
        exit(1);
    }

    std::cout << "\nC++ Consumer finished." << std::endl;
}

int main() {
    read_and_modify();
    return 0;
}
