# Cross-Process Communication Patterns with ZeroIPC

## Introduction

This document presents common patterns and architectural designs for building robust multi-process systems using ZeroIPC. Each pattern is illustrated with concrete examples and best practices.

## Table of Contents

1. [Basic Patterns](#basic-patterns)
2. [Async Patterns](#async-patterns)
3. [Streaming Patterns](#streaming-patterns)
4. [Concurrency Patterns](#concurrency-patterns)
5. [Architectural Patterns](#architectural-patterns)
6. [Reliability Patterns](#reliability-patterns)

---

## Basic Patterns

### Producer-Consumer

The fundamental pattern where one process produces data and another consumes it.

**Using Queue:**
```cpp
// Producer Process
memory mem("/app", 10*1024*1024);
queue<Task> task_queue(mem, "tasks", 1000);

while (running) {
    Task t = generate_task();
    if (!task_queue.push(t)) {
        // Handle backpressure
        std::this_thread::sleep_for(10ms);
    }
}

// Consumer Process
memory mem("/app");
queue<Task> task_queue(mem, "tasks");

while (running) {
    if (auto task = task_queue.pop()) {
        process_task(*task);
    } else {
        // Queue empty, wait
        std::this_thread::sleep_for(1ms);
    }
}
```

**Best Practices:**
- Handle backpressure gracefully
- Use exponential backoff for retries
- Monitor queue depth for capacity planning

### Request-Response

Synchronous communication pattern using Futures.

```cpp
// Service Process
memory mem("/service", 50*1024*1024);
queue<Request> requests(mem, "requests", 100);

while (running) {
    if (auto req = requests.pop()) {
        // Create future for response
        future<Response> response(mem, "response_" + std::to_string(req->id));
        
        // Process request
        Response res = handle_request(*req);
        response.set_value(res);
    }
}

// Client Process
memory mem("/service");
queue<Request> requests(mem, "requests");

Request req{.id = 42, .data = "query"};
requests.push(req);

// Wait for response
future<Response> response(mem, "response_42", true);
if (auto res = response.get_for(5s)) {
    process_response(*res);
} else {
    handle_timeout();
}
```

### Pub-Sub (Publish-Subscribe)

Multiple consumers receiving the same data stream.

```cpp
// Publisher
memory mem("/events", 20*1024*1024);
stream<Event> events(mem, "event_stream", 10000);

void publish_event(const Event& e) {
    events.emit(e);
}

// Subscriber A - Logging
memory mem("/events");
stream<Event> events(mem, "event_stream");

events.subscribe([](const Event& e) {
    log_to_file(e);
});

// Subscriber B - Analytics
events.subscribe([](const Event& e) {
    update_metrics(e);
});

// Subscriber C - Alerting
auto critical = events.filter(mem, "critical", 
    [](const Event& e) { return e.severity >= CRITICAL; });

critical.subscribe([](const Event& e) {
    send_alert(e);
});
```

---

## Async Patterns

### Future Chaining

Compose asynchronous operations without blocking.

```cpp
// Process A: Initiate computation chain
memory mem("/pipeline", 100*1024*1024);

future<RawData> raw_future(mem, "raw_data");
future<ProcessedData> processed_future(mem, "processed_data");
future<Result> final_future(mem, "final_result");

// Start pipeline
RawData raw = fetch_data();
raw_future.set_value(raw);

// Process B: First transformation
future<RawData> raw_future(mem, "raw_data", true);
future<ProcessedData> processed_future(mem, "processed_data");

RawData raw = raw_future.get();
ProcessedData processed = transform(raw);
processed_future.set_value(processed);

// Process C: Final computation
future<ProcessedData> processed_future(mem, "processed_data", true);
future<Result> final_future(mem, "final_result");

ProcessedData processed = processed_future.get();
Result result = compute_final(processed);
final_future.set_value(result);
```

### Fork-Join Parallelism

Split work across processes and join results.

```cpp
// Coordinator Process
memory mem("/parallel", 200*1024*1024);

// Create work items
const int N = 10;
for (int i = 0; i < N; i++) {
    array<double> input(mem, "input_" + std::to_string(i), 1000);
    fill_with_data(input);
    
    future<double> result(mem, "result_" + std::to_string(i));
}

// Wait for all results
std::vector<double> results;
for (int i = 0; i < N; i++) {
    future<double> result(mem, "result_" + std::to_string(i), true);
    results.push_back(result.get());
}

double final = aggregate(results);

// Worker Process (run N instances)
memory mem("/parallel");
int worker_id = get_worker_id();

array<double> input(mem, "input_" + std::to_string(worker_id));
future<double> result(mem, "result_" + std::to_string(worker_id), true);

double res = heavy_computation(input);
result.set_value(res);
```

### Lazy Initialization

Defer expensive initialization until needed.

```cpp
// Configuration Service
memory mem("/config", 10*1024*1024);
lazy<Config> config(mem, "app_config");

config.set_computation([]() {
    // Expensive: read files, parse, validate
    Config c;
    c.parse_from_file("/etc/app.conf");
    c.validate();
    c.resolve_dependencies();
    return c;
});

// Application Processes
memory mem("/config");
lazy<Config> config(mem, "app_config", true);

// First access triggers computation
Config c = config.get();  // Expensive first time

// Subsequent accesses are instant
Config c2 = config.get();  // Returns cached
```

---

## Streaming Patterns

### Pipeline Processing

Chain stream transformations for complex processing.

```cpp
// Data Processing Pipeline
memory mem("/pipeline", 50*1024*1024);

// Raw sensor data
stream<SensorReading> raw(mem, "raw_sensors", 10000);

// Stage 1: Validation and cleaning
auto validated = raw
    .filter(mem, "validated", [](const SensorReading& r) {
        return r.timestamp > 0 && r.value >= 0;
    })
    .map(mem, "cleaned", [](const SensorReading& r) {
        return SensorReading{
            .timestamp = r.timestamp,
            .value = apply_calibration(r.value)
        };
    });

// Stage 2: Windowing and aggregation
auto windows = validated
    .window(mem, "5sec_windows", 50)  // 50 readings per window
    .map(mem, "aggregated", [](const auto& window) {
        double sum = 0, min = DBL_MAX, max = DBL_MIN;
        for (const auto& r : window) {
            sum += r.value;
            min = std::min(min, r.value);
            max = std::max(max, r.value);
        }
        return Aggregate{
            .avg = sum / window.size(),
            .min = min,
            .max = max,
            .count = window.size()
        };
    });

// Stage 3: Alerting
windows.subscribe([](const Aggregate& agg) {
    if (agg.avg > THRESHOLD) {
        trigger_alert(agg);
    }
    store_metrics(agg);
});
```

### Merge and Join Streams

Combine multiple data streams.

```cpp
// Merge Pattern - Combine streams of same type
stream<Event> ui_events(mem, "ui_events", 1000);
stream<Event> system_events(mem, "system_events", 1000);
stream<Event> network_events(mem, "network_events", 1000);

auto all_events = ui_events
    .merge(mem, "merged_1", system_events)
    .merge(mem, "all_events", network_events);

// Zip Pattern - Combine streams element-wise
stream<Temperature> temps(mem, "temperatures", 1000);
stream<Pressure> pressures(mem, "pressures", 1000);

auto combined = temps.zip(mem, "temp_pressure", pressures);
combined.subscribe([](const auto& pair) {
    auto [temp, pressure] = pair;
    calculate_altitude(temp, pressure);
});
```

### Backpressure Management

Handle fast producers and slow consumers.

```cpp
class BackpressureStream {
    stream<Data> primary;
    queue<Data> overflow;
    std::atomic<bool> use_overflow{false};
    
public:
    void emit(const Data& d) {
        if (!primary.emit(d)) {
            // Primary stream full, use overflow queue
            if (!overflow.push(d)) {
                // Both full - apply backpressure strategy
                use_overflow.store(true);
                drop_oldest();  // or block, or sample
            }
        }
    }
    
    void process() {
        // Drain overflow when primary has space
        if (use_overflow.load() && primary.available_space() > 0) {
            while (auto d = overflow.pop()) {
                if (!primary.emit(*d)) break;
            }
            use_overflow.store(overflow.size() > 0);
        }
    }
};
```

---

## Concurrency Patterns

### Worker Pool

Distribute work across a pool of worker processes.

```cpp
// Manager Process
memory mem("/workers", 100*1024*1024);
channel<Task> task_channel(mem, "tasks", 1000);
channel<Result> result_channel(mem, "results", 1000);

// Submit tasks
for (const auto& task : tasks) {
    task_channel.send(task);
}
task_channel.close();

// Collect results
std::vector<Result> results;
while (auto result = result_channel.receive()) {
    results.push_back(*result);
}

// Worker Process Template
void worker_main(int worker_id) {
    memory mem("/workers");
    channel<Task> task_channel(mem, "tasks");
    channel<Result> result_channel(mem, "results");
    
    while (auto task = task_channel.receive()) {
        Result r = process(*task);
        r.worker_id = worker_id;
        result_channel.send(r);
    }
}

// Launch workers
for (int i = 0; i < NUM_WORKERS; i++) {
    std::thread(worker_main, i).detach();
}
```

### Actor Model

Implement actors with channels as mailboxes.

```cpp
class Actor {
    memory& mem;
    std::string name;
    channel<Message> mailbox;
    std::thread worker;
    std::atomic<bool> running{true};
    
public:
    Actor(memory& m, std::string_view n) 
        : mem(m), name(n), 
          mailbox(m, std::string(n) + "_mailbox", 100) {
        worker = std::thread([this]() { run(); });
    }
    
    void send(const Message& msg) {
        mailbox.send(msg);
    }
    
private:
    void run() {
        while (running.load()) {
            if (auto msg = mailbox.receive_for(100ms)) {
                handle_message(*msg);
            }
        }
    }
    
    void handle_message(const Message& msg) {
        switch (msg.type) {
            case COMPUTE:
                do_computation(msg.data);
                break;
            case FORWARD:
                forward_to_actor(msg.target, msg.data);
                break;
            case STOP:
                running.store(false);
                break;
        }
    }
};

// Usage
memory mem("/actors", 50*1024*1024);
Actor compute_actor(mem, "compute");
Actor storage_actor(mem, "storage");
Actor network_actor(mem, "network");

compute_actor.send({COMPUTE, data});
```

### CSP Select

Wait on multiple channels simultaneously.

```cpp
template<typename T1, typename T2>
void select_loop(channel<T1>& ch1, channel<T2>& ch2) {
    while (true) {
        // Try channels in order with timeout
        if (auto v1 = ch1.receive_for(0ms)) {
            handle_type1(*v1);
            continue;
        }
        
        if (auto v2 = ch2.receive_for(0ms)) {
            handle_type2(*v2);
            continue;
        }
        
        // No data available, wait a bit
        std::this_thread::sleep_for(1ms);
    }
}

// Alternative: Priority select
class PrioritySelect {
    struct ChannelPriority {
        int priority;
        std::function<bool()> try_receive;
    };
    
    std::vector<ChannelPriority> channels;
    
public:
    void add_channel(int priority, auto& channel, auto handler) {
        channels.push_back({priority, [&]() {
            if (auto v = channel.try_receive()) {
                handler(*v);
                return true;
            }
            return false;
        }});
        
        // Sort by priority
        std::sort(channels.begin(), channels.end(),
            [](const auto& a, const auto& b) { 
                return a.priority > b.priority; 
            });
    }
    
    void select() {
        for (auto& ch : channels) {
            if (ch.try_receive()) return;
        }
    }
};
```

---

## Architectural Patterns

### Microservices Communication

Build microservices that communicate through shared memory.

```cpp
// Service Registry
class ServiceRegistry {
    memory mem;
    map<uint32_t, ServiceInfo> services;
    
public:
    ServiceRegistry() : mem("/services", 10*1024*1024),
                       services(mem, "registry", 100) {}
    
    void register_service(uint32_t id, const ServiceInfo& info) {
        services.insert(id, info);
    }
    
    std::optional<ServiceInfo> lookup(uint32_t id) {
        return services.get(id);
    }
};

// Service Base Class
class Service {
protected:
    memory mem;
    queue<Request> requests;
    map<uint32_t, future<Response>*> responses;
    
public:
    Service(std::string_view name, size_t mem_size)
        : mem(std::string(name), mem_size),
          requests(mem, "requests", 100),
          responses(mem, "responses", 100) {}
    
    virtual Response handle_request(const Request& req) = 0;
    
    void run() {
        while (true) {
            if (auto req = requests.pop()) {
                Response res = handle_request(*req);
                
                future<Response> response(mem, 
                    "response_" + std::to_string(req->id));
                response.set_value(res);
            }
        }
    }
};

// Example Service
class DataService : public Service {
public:
    DataService() : Service("/data_service", 50*1024*1024) {}
    
    Response handle_request(const Request& req) override {
        switch (req.type) {
            case QUERY:
                return execute_query(req.data);
            case UPDATE:
                return update_data(req.data);
            default:
                return error_response("Unknown request type");
        }
    }
};
```

### Event Sourcing

Store events and rebuild state from event stream.

```cpp
class EventStore {
    memory mem;
    stream<Event> events;
    std::atomic<uint64_t> event_id{0};
    
public:
    EventStore() : mem("/events", 1000*1024*1024),  // 1GB
                   events(mem, "event_stream", 1000000) {}
    
    void append(Event e) {
        e.id = event_id.fetch_add(1);
        e.timestamp = std::chrono::system_clock::now();
        events.emit(e);
    }
    
    // Replay events to rebuild state
    template<typename State>
    State replay(State initial, 
                 std::function<State(State, Event)> reducer) {
        State state = initial;
        
        events.subscribe([&](const Event& e) {
            state = reducer(state, e);
        });
        
        return state;
    }
};

// Usage
EventStore store;

// Append events
store.append({CREATE_USER, user_data});
store.append({UPDATE_PROFILE, profile_data});
store.append({DELETE_USER, user_id});

// Rebuild current state
auto current_state = store.replay(InitialState{}, 
    [](auto state, const Event& e) {
        return apply_event(state, e);
    });
```

### CQRS (Command Query Responsibility Segregation)

Separate read and write models.

```cpp
class CQRSSystem {
    memory write_mem;
    memory read_mem;
    
    // Write side
    queue<Command> commands;
    stream<Event> events;
    
    // Read side
    array<QueryModel> read_models;
    lazy<Statistics> stats;
    
public:
    CQRSSystem() 
        : write_mem("/write", 100*1024*1024),
          read_mem("/read", 200*1024*1024),
          commands(write_mem, "commands", 1000),
          events(write_mem, "events", 10000),
          read_models(read_mem, "models", 10000),
          stats(read_mem, "stats") {}
    
    // Command handler (write side)
    void handle_commands() {
        while (auto cmd = commands.pop()) {
            Event e = process_command(*cmd);
            events.emit(e);
            
            // Update read models asynchronously
            update_read_model(e);
        }
    }
    
    // Query handler (read side)
    QueryResult query(const Query& q) {
        // Read from optimized read models
        switch (q.type) {
            case GET_BY_ID:
                return read_models[q.id];
            case GET_STATS:
                return stats.get();
            default:
                return {};
        }
    }
    
private:
    void update_read_model(const Event& e) {
        // Project event to read model
        read_models[e.entity_id] = project(e);
        
        // Invalidate cached statistics
        stats.invalidate();
    }
};
```

---

## Reliability Patterns

### Circuit Breaker

Prevent cascading failures in distributed systems.

```cpp
class CircuitBreaker {
    enum State { CLOSED, OPEN, HALF_OPEN };
    
    memory& mem;
    std::atomic<State> state{CLOSED};
    std::atomic<int> failure_count{0};
    std::atomic<uint64_t> last_failure_time{0};
    
    const int failure_threshold = 5;
    const int timeout_ms = 5000;
    const int half_open_success_threshold = 3;
    
public:
    CircuitBreaker(memory& m) : mem(m) {}
    
    template<typename Func>
    auto call(Func func) -> std::optional<decltype(func())> {
        if (state.load() == OPEN) {
            auto now = current_time_ms();
            if (now - last_failure_time.load() > timeout_ms) {
                state.store(HALF_OPEN);
            } else {
                return std::nullopt;  // Fast fail
            }
        }
        
        try {
            auto result = func();
            
            if (state.load() == HALF_OPEN) {
                // Success in half-open state, close circuit
                state.store(CLOSED);
                failure_count.store(0);
            }
            
            return result;
            
        } catch (...) {
            failure_count.fetch_add(1);
            last_failure_time.store(current_time_ms());
            
            if (failure_count.load() >= failure_threshold) {
                state.store(OPEN);
            }
            
            return std::nullopt;
        }
    }
};

// Usage
CircuitBreaker breaker(mem);

if (auto result = breaker.call([&]() { 
    return risky_operation(); 
})) {
    process(*result);
} else {
    use_fallback();
}
```

### Saga Pattern

Manage distributed transactions with compensations.

```cpp
class Saga {
    struct Step {
        std::function<bool()> action;
        std::function<void()> compensation;
        bool completed = false;
    };
    
    memory& mem;
    std::vector<Step> steps;
    stream<SagaEvent> events;
    
public:
    Saga(memory& m, std::string_view name) 
        : mem(m), events(m, std::string(name) + "_events", 1000) {}
    
    void add_step(auto action, auto compensation) {
        steps.push_back({action, compensation, false});
    }
    
    bool execute() {
        for (size_t i = 0; i < steps.size(); i++) {
            events.emit({STEP_STARTED, i});
            
            if (!steps[i].action()) {
                events.emit({STEP_FAILED, i});
                
                // Compensate completed steps in reverse
                for (int j = i - 1; j >= 0; j--) {
                    if (steps[j].completed) {
                        events.emit({COMPENSATION_STARTED, j});
                        steps[j].compensation();
                        events.emit({COMPENSATION_COMPLETED, j});
                    }
                }
                
                return false;
            }
            
            steps[i].completed = true;
            events.emit({STEP_COMPLETED, i});
        }
        
        events.emit({SAGA_COMPLETED, 0});
        return true;
    }
};

// Usage: Distributed order processing
Saga order_saga(mem, "order_123");

order_saga.add_step(
    [&]() { return reserve_inventory(order); },
    [&]() { release_inventory(order); }
);

order_saga.add_step(
    [&]() { return charge_payment(order); },
    [&]() { refund_payment(order); }
);

order_saga.add_step(
    [&]() { return schedule_shipping(order); },
    [&]() { cancel_shipping(order); }
);

if (!order_saga.execute()) {
    notify_order_failed();
}
```

### Bulkhead Isolation

Isolate resources to prevent total system failure.

```cpp
class Bulkhead {
    memory& mem;
    std::vector<pool<Resource>> pools;
    
public:
    Bulkhead(memory& m, size_t num_compartments, size_t resources_per)
        : mem(m) {
        
        for (size_t i = 0; i < num_compartments; i++) {
            pools.emplace_back(mem, 
                "pool_" + std::to_string(i), 
                resources_per);
        }
    }
    
    template<typename Func>
    auto execute_in_compartment(size_t compartment, Func func) {
        auto* resource = pools[compartment].allocate();
        if (!resource) {
            throw std::runtime_error("Compartment " + 
                std::to_string(compartment) + " exhausted");
        }
        
        // RAII wrapper for automatic return
        struct Guard {
            pool<Resource>& p;
            Resource* r;
            ~Guard() { p.deallocate(r); }
        } guard{pools[compartment], resource};
        
        return func(resource);
    }
};

// Usage: Isolate different request types
Bulkhead bulkhead(mem, 3, 10);  // 3 compartments, 10 resources each

// Critical requests use compartment 0
bulkhead.execute_in_compartment(0, [](auto* res) {
    handle_critical_request(res);
});

// Normal requests use compartment 1
bulkhead.execute_in_compartment(1, [](auto* res) {
    handle_normal_request(res);
});

// Background tasks use compartment 2
bulkhead.execute_in_compartment(2, [](auto* res) {
    handle_background_task(res);
});
```

---

## Best Practices Summary

### Design Principles

1. **Loose Coupling**: Use named structures for discovery rather than hard-coded offsets
2. **Backpressure Handling**: Always handle full queues/channels gracefully
3. **Timeout Everything**: Never block indefinitely, always use timeouts
4. **Error Propagation**: Use futures and channels to propagate errors
5. **Resource Cleanup**: Ensure proper cleanup in all code paths

### Performance Guidelines

1. **Batch Operations**: Process multiple items at once when possible
2. **Avoid Polling**: Use blocking operations with timeouts instead of busy-waiting
3. **Cache Locality**: Group related data in memory
4. **Lock-Free First**: Prefer lock-free structures for high concurrency
5. **Profile and Monitor**: Measure actual performance, don't guess

### Reliability Guidelines

1. **Graceful Degradation**: Design for partial failures
2. **Idempotency**: Make operations repeatable without side effects
3. **Compensations**: Plan rollback strategies for distributed operations
4. **Health Checks**: Implement health monitoring for all services
5. **Observability**: Log, trace, and monitor all critical paths

### Testing Strategies

1. **Chaos Testing**: Randomly kill processes to test recovery
2. **Load Testing**: Test with realistic data volumes and rates
3. **Race Condition Testing**: Use thread sanitizers and stress tests
4. **Integration Testing**: Test complete workflows across processes
5. **Performance Regression**: Track performance metrics over time

## Conclusion

These patterns demonstrate how ZeroIPC's combination of traditional data structures and codata primitives enable sophisticated multi-process architectures. The key is choosing the right pattern for your specific use case and combining them effectively to build robust, scalable systems.