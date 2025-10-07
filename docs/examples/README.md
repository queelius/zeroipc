# ZeroIPC Examples

This directory contains complete, working examples demonstrating various ZeroIPC features and patterns.

## Example Categories

### Basic Examples
- `01_hello_world.cpp` - Simple array sharing between processes
- `02_producer_consumer.cpp` - Basic queue-based communication
- `03_pub_sub.cpp` - Stream-based publish-subscribe

### Codata Examples
- `10_futures.cpp` - Asynchronous computation with futures
- `11_lazy_config.cpp` - Lazy evaluation for configuration
- `12_reactive_streams.cpp` - FRP with stream operators
- `13_channels.cpp` - CSP-style communication

### Advanced Patterns
- `20_worker_pool.cpp` - Distributed work processing
- `21_event_sourcing.cpp` - Event store and replay
- `22_saga_pattern.cpp` - Distributed transactions
- `23_circuit_breaker.cpp` - Fault tolerance

### Real-World Applications
- `30_sensor_pipeline.cpp` - IoT sensor data processing
- `31_trading_system.cpp` - High-frequency trading example
- `32_ml_pipeline.cpp` - Machine learning data pipeline
- `33_game_server.cpp` - Multiplayer game state sharing

## Building Examples

```bash
cd /home/spinoza/github/beta/zeroipc/docs/examples
mkdir build && cd build
cmake ..
make

# Run individual examples
./01_hello_world
```

## Running Multi-Process Examples

Many examples require multiple processes. Launch them in separate terminals:

```bash
# Terminal 1 - Producer
./02_producer_consumer producer

# Terminal 2 - Consumer
./02_producer_consumer consumer
```

## Example Template

Each example follows this structure:

```cpp
/**
 * Example: [Name]
 * 
 * Description: What this example demonstrates
 * 
 * Usage:
 *   Process A: ./example producer
 *   Process B: ./example consumer
 * 
 * Expected Output:
 *   [What you should see]
 */

#include <zeroipc/memory.h>
// ... other includes

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [producer|consumer]" << std::endl;
        return 1;
    }
    
    std::string role = argv[1];
    
    if (role == "producer") {
        // Producer code
    } else if (role == "consumer") {
        // Consumer code
    } else {
        std::cerr << "Unknown role: " << role << std::endl;
        return 1;
    }
    
    return 0;
}
```

## Learning Path

Recommended order for learning ZeroIPC:

1. Start with `01_hello_world.cpp` - Basic concepts
2. Move to `02_producer_consumer.cpp` - Queues
3. Try `10_futures.cpp` - Async patterns
4. Explore `12_reactive_streams.cpp` - FRP
5. Study `20_worker_pool.cpp` - Scaling
6. Examine real-world examples - Complete systems

## Debugging Tips

1. Use `zeroipc-inspect` to monitor shared memory:
   ```bash
   zeroipc-inspect monitor /example_segment structure_name
   ```

2. Enable debug output:
   ```bash
   ZEROIPC_DEBUG=1 ./example
   ```

3. Check for orphaned segments:
   ```bash
   ls /dev/shm/
   zeroipc-inspect clean --dry-run
   ```

## Contributing Examples

To add a new example:

1. Follow the naming convention: `NN_description.cpp`
2. Include comprehensive comments
3. Add error handling
4. Update this README
5. Test with multiple processes

## License

All examples are MIT licensed and free to use in your projects.