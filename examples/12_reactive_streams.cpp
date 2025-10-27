/**
 * Example: Reactive Streams
 * 
 * Description: Demonstrates functional reactive programming with streams.
 * Shows map, filter, window, and fold operations on a temperature sensor stream.
 * 
 * Usage:
 *   Terminal 1: ./12_reactive_streams sensor
 *   Terminal 2: ./12_reactive_streams analytics
 *   Terminal 3: ./12_reactive_streams monitor
 * 
 * Expected Output:
 *   Sensor: Emits random temperature readings
 *   Analytics: Processes stream with FRP operators
 *   Monitor: Displays alerts when thresholds exceeded
 */

#include <zeroipc/memory.h>
#include <zeroipc/stream.h>
#include <zeroipc/future.h>
#include <iostream>
#include <random>
#include <thread>
#include <chrono>
#include <numeric>
#include <iomanip>

using namespace zeroipc;
using namespace std::chrono_literals;

struct SensorReading {
    double temperature;  // Celsius
    double humidity;     // Percentage
    uint64_t timestamp;  // Unix timestamp
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [sensor|analytics|monitor]" << std::endl;
        return 1;
    }
    
    std::string role = argv[1];
    
    try {
        if (role == "sensor") {
            // Sensor process: Emit temperature readings
            std::cout << "Sensor: Starting temperature sensor simulation" << std::endl;
            
            memory mem("/reactive_demo", 50 * 1024 * 1024);  // 50MB
            stream<SensorReading> raw_stream(mem, "raw_sensors", 10000);
            
            // Random number generation
            std::random_device rd;
            std::mt19937 gen(rd());
            std::normal_distribution<> temp_dist(22.0, 3.0);  // Mean 22°C, StdDev 3°C
            std::normal_distribution<> humidity_dist(60.0, 10.0);  // Mean 60%, StdDev 10%
            
            std::cout << "Sensor: Emitting readings every 100ms..." << std::endl;
            std::cout << "Sensor: Press Ctrl+C to stop" << std::endl;
            
            size_t count = 0;
            while (true) {
                SensorReading reading{
                    .temperature = temp_dist(gen),
                    .humidity = std::clamp(humidity_dist(gen), 0.0, 100.0),
                    .timestamp = static_cast<uint64_t>(
                        std::chrono::system_clock::now().time_since_epoch().count())
                };
                
                if (raw_stream.emit(reading)) {
                    count++;
                    if (count % 10 == 0) {
                        std::cout << "Sensor: Emitted " << count << " readings. "
                                 << "Latest: " << std::fixed << std::setprecision(2) 
                                 << reading.temperature << "°C, " 
                                 << reading.humidity << "% humidity" << std::endl;
                    }
                }
                
                std::this_thread::sleep_for(100ms);
            }
            
        } else if (role == "analytics") {
            // Analytics process: Apply FRP transformations
            std::cout << "Analytics: Starting stream processing pipeline" << std::endl;
            
            memory mem("/reactive_demo");
            stream<SensorReading> raw_stream(mem, "raw_sensors");
            
            // Stage 1: Convert to Fahrenheit
            auto fahrenheit_stream = raw_stream.map(mem, "fahrenheit", 
                [](const SensorReading& r) {
                    SensorReading f = r;
                    f.temperature = r.temperature * 9.0/5.0 + 32.0;
                    return f;
                });
            
            // Stage 2: Filter for high temperatures (>80°F)
            auto high_temp_stream = fahrenheit_stream.filter(mem, "high_temps",
                [](const SensorReading& r) {
                    return r.temperature > 80.0;
                });
            
            // Stage 3: Window every 10 readings for averaging
            auto windowed_stream = high_temp_stream.window(mem, "temp_windows", 10);
            
            // Stage 4: Calculate statistics per window
            struct TempStats {
                double avg_temp;
                double max_temp;
                double min_temp;
                double avg_humidity;
                size_t count;
            };
            
            auto stats_stream = windowed_stream.map(mem, "temp_stats",
                [](const std::vector<SensorReading>& window) {
                    if (window.empty()) {
                        return TempStats{0, 0, 0, 0, 0};
                    }
                    
                    double sum_temp = 0, sum_humidity = 0;
                    double max_temp = -1000, min_temp = 1000;
                    
                    for (const auto& r : window) {
                        sum_temp += r.temperature;
                        sum_humidity += r.humidity;
                        max_temp = std::max(max_temp, r.temperature);
                        min_temp = std::min(min_temp, r.temperature);
                    }
                    
                    return TempStats{
                        .avg_temp = sum_temp / window.size(),
                        .max_temp = max_temp,
                        .min_temp = min_temp,
                        .avg_humidity = sum_humidity / window.size(),
                        .count = window.size()
                    };
                });
            
            // Stage 5: Create alerts for critical temperatures
            auto alert_stream = stats_stream.filter(mem, "alerts",
                [](const TempStats& stats) {
                    return stats.avg_temp > 85.0;  // Critical: avg > 85°F
                });
            
            // Subscribe to process statistics
            std::cout << "Analytics: Pipeline created, processing..." << std::endl;
            
            auto stats_sub = stats_stream.subscribe([](const TempStats& stats) {
                std::cout << "Analytics: Window Stats - "
                         << "Avg: " << std::fixed << std::setprecision(2) << stats.avg_temp << "°F, "
                         << "Max: " << stats.max_temp << "°F, "
                         << "Min: " << stats.min_temp << "°F, "
                         << "Humidity: " << stats.avg_humidity << "%, "
                         << "Samples: " << stats.count << std::endl;
            });
            
            auto alert_sub = alert_stream.subscribe([](const TempStats& stats) {
                std::cout << "⚠️  ALERT: Critical temperature! "
                         << "Average: " << stats.avg_temp << "°F" << std::endl;
            });
            
            // Stage 6: Calculate running average using fold
            auto running_avg = fahrenheit_stream.fold(mem, "running_avg",
                std::pair<double, size_t>{0.0, 0},
                [](std::pair<double, size_t> acc, const SensorReading& r) {
                    double new_sum = acc.first + r.temperature;
                    size_t new_count = acc.second + 1;
                    return std::make_pair(new_sum, new_count);
                });
            
            // Keep running
            std::cout << "Analytics: Press Ctrl+C to stop" << std::endl;
            while (true) {
                std::this_thread::sleep_for(1s);
                
                // Periodically check the running average
                if (running_avg.is_ready()) {
                    auto [sum, count] = running_avg.get();
                    if (count > 0) {
                        std::cout << "Analytics: Overall average temperature: " 
                                 << (sum / count) << "°F "
                                 << "(" << count << " samples)" << std::endl;
                    }
                }
            }
            
        } else if (role == "monitor") {
            // Monitor process: Display real-time dashboard
            std::cout << "Monitor: Real-time monitoring dashboard" << std::endl;
            
            memory mem("/reactive_demo");
            
            // Subscribe to multiple streams
            stream<SensorReading> raw_stream(mem, "raw_sensors");
            stream<SensorReading> high_temp_stream(mem, "high_temps");
            
            // Count events
            std::atomic<size_t> total_readings{0};
            std::atomic<size_t> high_temp_readings{0};
            
            auto raw_sub = raw_stream.subscribe([&](const SensorReading& r) {
                total_readings++;
            });
            
            auto high_sub = high_temp_stream.subscribe([&](const SensorReading& r) {
                high_temp_readings++;
                std::cout << "Monitor: High temp detected: " 
                         << std::fixed << std::setprecision(2)
                         << (r.temperature) << "°F at "
                         << "humidity " << r.humidity << "%" << std::endl;
            });
            
            // Display dashboard
            while (true) {
                std::this_thread::sleep_for(5s);
                
                std::cout << "\n=== Dashboard Update ===" << std::endl;
                std::cout << "Total Readings: " << total_readings.load() << std::endl;
                std::cout << "High Temp Events: " << high_temp_readings.load() << std::endl;
                
                double high_temp_ratio = 0.0;
                if (total_readings.load() > 0) {
                    high_temp_ratio = static_cast<double>(high_temp_readings.load()) / 
                                     total_readings.load() * 100.0;
                }
                
                std::cout << "High Temp Ratio: " << std::fixed << std::setprecision(2) 
                         << high_temp_ratio << "%" << std::endl;
                std::cout << "=====================\n" << std::endl;
            }
            
        } else {
            std::cerr << "Unknown role: " << role << std::endl;
            std::cerr << "Use 'sensor', 'analytics', or 'monitor'" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}