# Computational Data Structures: When Data Becomes Code

## Overview

Beyond traditional collections, we can create shared memory structures that represent computation itself. These blur the line between data and code, enabling distributed computation patterns, lazy evaluation, and functional programming paradigms in shared memory.

## Proposed Computational Structures

### 1. shm_future<T>: Asynchronous Computation Results

```cpp
template<typename T>
class shm_future {
    enum State { PENDING, COMPUTING, READY, ERROR };
    
    struct FutureData {
        std::atomic<State> state{PENDING};
        T value;
        char error_msg[256];
        std::atomic<uint32_t> waiters{0};
    };
    
    shm_span<FutureData> data;
    
public:
    // Producer sets the value
    void set_value(const T& val) {
        data->value = val;
        data->state.store(READY, std::memory_order_release);
        wake_waiters();
    }
    
    // Consumer waits for value
    T get() {
        while (data->state.load(std::memory_order_acquire) != READY) {
            wait();
        }
        return data->value;
    }
    
    // Continuation
    template<typename F>
    auto then(F&& func) -> shm_future<decltype(func(T{}))> {
        using U = decltype(func(T{}));
        auto next = shm_future<U>(shm, generate_name());
        
        // Store continuation
        on_ready([func, next]() {
            next.set_value(func(this->get()));
        });
        
        return next;
    }
};

// Usage in n-body simulation
shm_future<ForceField> force_calculation(shm, "forces");
shm_future<Positions> position_update = 
    force_calculation.then([](const ForceField& forces) {
        return integrate_positions(forces);
    });
```

### 2. shm_lazy<T>: Lazy Evaluation

```cpp
template<typename T>
class shm_lazy {
    mutable std::atomic<bool> computed{false};
    mutable T value;
    std::function<T()> computation;
    
public:
    shm_lazy(std::function<T()> comp) : computation(comp) {}
    
    const T& force() const {
        bool expected = false;
        if (computed.compare_exchange_strong(expected, true)) {
            value = computation();
        } else {
            // Wait for computation by another thread
            while (!computed.load()) {
                std::this_thread::yield();
            }
        }
        return value;
    }
    
    // Monadic operations
    template<typename F>
    auto map(F&& f) -> shm_lazy<decltype(f(T{}))> {
        return shm_lazy([=]() { return f(this->force()); });
    }
    
    template<typename F>
    auto flatMap(F&& f) -> decltype(f(T{})) {
        return f(this->force());
    }
};

// Lazy computation tree
shm_lazy<double> total_energy = shm_lazy([&]() {
    return compute_kinetic_energy() + compute_potential_energy();
});

// Only computed when needed
if (should_check_energy_conservation()) {
    double e = total_energy.force();
}
```

### 3. shm_closure: First-Class Functions in Shared Memory

```cpp
template<typename Sig>
class shm_closure;

template<typename R, typename... Args>
class shm_closure<R(Args...)> {
    enum OpCode {
        ADD, MUL, COMPOSE, PARTIAL, CUSTOM
    };
    
    struct ClosureData {
        OpCode op;
        union {
            struct { double a, b; } binary_op;
            struct { size_t f_idx, g_idx; } composition;
            struct { size_t func_idx; double bound_arg; } partial;
            struct { char code[256]; } custom;
        };
    };
    
public:
    R operator()(Args... args) {
        switch (data->op) {
            case ADD: return data->binary_op.a + args...;
            case MUL: return data->binary_op.a * args...;
            case COMPOSE: {
                auto f = shm_closure(shm, data->composition.f_idx);
                auto g = shm_closure(shm, data->composition.g_idx);
                return f(g(args...));
            }
            case PARTIAL: {
                auto f = shm_closure(shm, data->partial.func_idx);
                return f(data->partial.bound_arg, args...);
            }
            case CUSTOM:
                return interpret(data->custom.code, args...);
        }
    }
    
    // Function composition
    template<typename G>
    auto compose(const shm_closure<G>& g) {
        // Store composition in shared memory
        return shm_closure(COMPOSE, this->index, g.index);
    }
    
    // Partial application
    auto bind(double arg) {
        return shm_closure(PARTIAL, this->index, arg);
    }
};

// Usage
shm_closure<double(double)> gravity_force(shm, "gravity");
shm_closure<double(double)> damping(shm, "damping");

auto combined_force = gravity_force.compose(damping);
double f = combined_force(distance);
```

### 4. shm_stream<T>: Reactive Streams

```cpp
template<typename T>
class shm_stream {
    shm_ring_buffer<T> buffer;
    shm_atomic<uint64_t> sequence;
    std::vector<shm_future<void>> subscribers;
    
public:
    void emit(const T& value) {
        buffer.push(value);
        uint64_t seq = sequence.fetch_add(1);
        
        // Notify subscribers
        for (auto& sub : subscribers) {
            sub.notify(seq, value);
        }
    }
    
    // Functional transformations
    template<typename F>
    auto map(F&& f) -> shm_stream<decltype(f(T{}))> {
        using U = decltype(f(T{}));
        shm_stream<U> output(shm, generate_name());
        
        this->subscribe([f, output](const T& val) {
            output.emit(f(val));
        });
        
        return output;
    }
    
    template<typename F>
    auto filter(F&& predicate) -> shm_stream<T> {
        shm_stream<T> output(shm, generate_name());
        
        this->subscribe([predicate, output](const T& val) {
            if (predicate(val)) {
                output.emit(val);
            }
        });
        
        return output;
    }
    
    // Windowing
    auto window(size_t size) -> shm_stream<std::vector<T>> {
        shm_stream<std::vector<T>> output(shm, generate_name());
        std::vector<T> window_buffer;
        
        this->subscribe([output, window_buffer, size](const T& val) mutable {
            window_buffer.push_back(val);
            if (window_buffer.size() == size) {
                output.emit(window_buffer);
                window_buffer.clear();
            }
        });
        
        return output;
    }
};

// Reactive n-body simulation pipeline
shm_stream<Particle> particle_updates(shm, "updates");

auto collisions = particle_updates
    .window(2)  // Pairs
    .filter([](const auto& pair) { 
        return distance(pair[0], pair[1]) < threshold; 
    })
    .map([](const auto& pair) { 
        return compute_collision(pair[0], pair[1]); 
    });

collisions.subscribe([](const Collision& c) {
    render_collision_effect(c);
});
```

### 5. shm_fsm: Finite State Machines

```cpp
template<typename State, typename Event>
class shm_fsm {
    using Handler = std::function<State(State, Event)>;
    
    struct Transition {
        State from;
        Event trigger;
        State to;
        size_t action_idx;  // Index to action in shared memory
    };
    
    shm_array<Transition> transitions;
    shm_atomic<State> current_state;
    shm_queue<Event> event_queue;
    
public:
    void process_event(const Event& event) {
        State state = current_state.load();
        
        for (const auto& trans : transitions) {
            if (trans.from == state && trans.trigger == event) {
                // Execute action
                execute_action(trans.action_idx);
                
                // Transition
                current_state.store(trans.to);
                
                // Log transition
                log_transition(state, event, trans.to);
                break;
            }
        }
    }
    
    void run() {
        while (running) {
            if (auto event = event_queue.dequeue()) {
                process_event(*event);
            }
        }
    }
};

// Simulation state machine
enum SimState { INIT, RUNNING, PAUSED, STEPPING, STOPPED };
enum SimEvent { START, PAUSE, STEP, STOP, RESET };

shm_fsm<SimState, SimEvent> sim_fsm(shm, "sim_fsm");
sim_fsm.add_transition(INIT, START, RUNNING, []() { 
    initialize_particles(); 
});
sim_fsm.add_transition(RUNNING, PAUSE, PAUSED, []() { 
    save_checkpoint(); 
});
```

### 6. shm_dag: Computation DAG

```cpp
template<typename T>
class shm_dag {
    struct Node {
        std::string name;
        std::vector<size_t> dependencies;
        std::function<T()> computation;
        shm_future<T> result;
        std::atomic<bool> computed{false};
    };
    
    shm_array<Node> nodes;
    shm_queue<size_t> ready_queue;
    
public:
    size_t add_node(const std::string& name, 
                    std::function<T()> comp,
                    std::vector<std::string> deps = {}) {
        Node node{name, resolve_deps(deps), comp};
        return nodes.push_back(node);
    }
    
    void execute() {
        // Topological sort
        auto order = topological_sort();
        
        // Parallel execution respecting dependencies
        #pragma omp parallel
        {
            while (auto node_idx = ready_queue.dequeue()) {
                auto& node = nodes[*node_idx];
                
                // Wait for dependencies
                for (size_t dep : node.dependencies) {
                    nodes[dep].result.wait();
                }
                
                // Execute
                T result = node.computation();
                node.result.set_value(result);
                node.computed.store(true);
                
                // Notify dependents
                notify_dependents(*node_idx);
            }
        }
    }
};

// Build computation graph for n-body
shm_dag<void> simulation_dag(shm, "sim_dag");

auto forces = simulation_dag.add_node("forces", 
    []() { compute_all_forces(); });
    
auto positions = simulation_dag.add_node("positions",
    []() { integrate_positions(); }, {"forces"});
    
auto collisions = simulation_dag.add_node("collisions",
    []() { detect_collisions(); }, {"positions"});
    
auto energy = simulation_dag.add_node("energy",
    []() { compute_total_energy(); }, {"positions"});

simulation_dag.execute();
```

### 7. shm_memo: Memoization Table

```cpp
template<typename Key, typename Value>
class shm_memo {
    shm_hash_map<Key, Value> cache;
    shm_atomic<size_t> hits{0};
    shm_atomic<size_t> misses{0};
    
public:
    template<typename F>
    Value compute(const Key& key, F&& expensive_function) {
        // Check cache
        if (auto cached = cache.find(key)) {
            hits.fetch_add(1);
            return *cached;
        }
        
        // Compute and cache
        misses.fetch_add(1);
        Value result = expensive_function(key);
        cache.insert(key, result);
        return result;
    }
    
    double hit_rate() const {
        size_t h = hits.load();
        size_t m = misses.load();
        return h / double(h + m);
    }
};

// Memoize expensive distance calculations
shm_memo<std::pair<uint32_t, uint32_t>, float> distance_cache(shm, "distances");

float get_distance(uint32_t i, uint32_t j) {
    if (i > j) std::swap(i, j);  // Canonical order
    
    return distance_cache.compute({i, j}, [i, j]() {
        return compute_euclidean_distance(particles[i], particles[j]);
    });
}
```

## Implementation Considerations

### Memory Management
- Function pointers can't cross process boundaries
- Store computation as data (opcodes, AST, bytecode)
- Use indices into shared lookup tables

### Synchronization
- Atomic state transitions
- Lock-free algorithms where possible
- Event-driven architecture

### Performance
- Cache-align computational state
- Batch operations
- Use work-stealing for parallel execution

## Use Cases

1. **Lazy Physics** - Compute forces only when needed
2. **Reactive Visualization** - Stream processing for real-time updates
3. **Workflow Orchestration** - DAG-based simulation pipelines
4. **Checkpointing** - State machines for simulation control
5. **Caching** - Memoization of expensive computations

## Future Directions

- **shm_coroutine** - Suspendable computations
- **shm_transducer** - Composable algorithmic transformations
- **shm_genetic** - Genetic algorithms with shared population

These computational structures transform shared memory from passive data storage into active computation engines, enabling new patterns for distributed computing, functional programming, and reactive systems in high-performance simulations.