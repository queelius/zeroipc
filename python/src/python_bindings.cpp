/**
 * @file python_bindings.cpp
 * @brief Python bindings for posix_shm using pybind11
 * @author Alex Towell
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/numpy.h>

#include "../../include/posix_shm.h"
#include "../../include/shm_array.h"
#include "../../include/shm_queue.h"
#include "../../include/shm_stack.h"
#include "../../include/shm_hash_map.h"
#include "../../include/shm_set.h"
#include "../../include/shm_bitset.h"
#include "../../include/shm_ring_buffer.h"
#include "../../include/shm_object_pool.h"
#include "../../include/shm_atomic.h"

namespace py = pybind11;

// Macro to stringify version
#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

// Helper class to manage object lifetime in Python
template<typename T>
class PythonShmObject {
    posix_shm* shm_ptr;
    std::unique_ptr<T> obj;
public:
    PythonShmObject(posix_shm& shm, const std::string& name, size_t capacity = 0) 
        : shm_ptr(&shm), obj(std::make_unique<T>(shm, name, capacity)) {}
    T* operator->() { return obj.get(); }
    T& operator*() { return *obj; }
};


PYBIND11_MODULE(posix_shm_py, m) {
    m.doc() = "Python bindings for POSIX shared memory data structures";
    
    // Version
    #ifdef VERSION_INFO
        m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
    #else
        m.attr("__version__") = "dev";
    #endif

    // ========== Core Classes ==========
    
    // posix_shm
    py::class_<posix_shm>(m, "SharedMemory")
        .def(py::init<const std::string&, size_t>(),
             py::arg("name"), py::arg("size"),
             "Create or attach to shared memory segment")
        .def("unlink", &posix_shm::unlink,
             "Remove shared memory segment")
        .def("__repr__", [](const posix_shm& self) {
            return "<SharedMemory>";
        });

    // ========== Atomic Types ==========
    
    py::class_<shm_atomic<int>>(m, "AtomicInt")
        .def(py::init<posix_shm&, const std::string&, int>(),
             py::arg("shm"), py::arg("name"), py::arg("initial") = 0)
        .def("load", &shm_atomic<int>::load)
        .def("store", &shm_atomic<int>::store)
        .def("exchange", &shm_atomic<int>::exchange)
        .def("compare_exchange", [](shm_atomic<int>& self, int expected, int desired) {
            return self.compare_exchange_strong(expected, desired);
        })
        .def("fetch_add", &shm_atomic<int>::fetch_add)
        .def("fetch_sub", &shm_atomic<int>::fetch_sub)
        .def("__iadd__", [](shm_atomic<int>& self, int val) { 
            self.fetch_add(val); return self; 
        })
        .def("__isub__", [](shm_atomic<int>& self, int val) { 
            self.fetch_sub(val); return self; 
        })
        .def("__int__", &shm_atomic<int>::load)
        .def("__repr__", [](shm_atomic<int>& self) {
            return "<AtomicInt value=" + std::to_string(self.load()) + ">";
        });

    // ========== Queue ==========
    
    py::class_<shm_queue<int>>(m, "IntQueue")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("enqueue", &shm_queue<int>::enqueue, "Add element to queue")
        .def("dequeue", [](shm_queue<int>& self) -> py::object {
            auto val = self.dequeue();
            if (val) return py::cast(*val);
            return py::none();
        }, "Remove and return element from queue")
        .def("push", &shm_queue<int>::enqueue, "Alias for enqueue")
        .def("pop", [](shm_queue<int>& self) -> py::object {
            auto val = self.dequeue();
            if (val) return py::cast(*val);
            return py::none();
        }, "Alias for dequeue")
        .def("full", &shm_queue<int>::full)
        .def("capacity", &shm_queue<int>::capacity)
        .def("size", &shm_queue<int>::size)
        .def("empty", &shm_queue<int>::empty)
        .def("__len__", &shm_queue<int>::size)
        .def("__bool__", [](const shm_queue<int>& self) { return !self.empty(); });

    py::class_<shm_queue<double>>(m, "FloatQueue")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("enqueue", &shm_queue<double>::enqueue)
        .def("dequeue", [](shm_queue<double>& self) -> py::object {
            auto val = self.dequeue();
            if (val) return py::cast(*val);
            return py::none();
        })
        .def("size", &shm_queue<double>::size)
        .def("empty", &shm_queue<double>::empty)
        .def("__len__", &shm_queue<double>::size)
        .def("__bool__", [](const shm_queue<double>& self) { return !self.empty(); });

    // ========== Stack ==========
    
    py::class_<shm_stack<int>>(m, "IntStack")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("push", &shm_stack<int>::push, "Push element onto stack")
        .def("pop", [](shm_stack<int>& self) -> py::object {
            auto val = self.pop();
            if (val) return py::cast(*val);
            return py::none();
        }, "Pop element from stack")
        .def("top", [](shm_stack<int>& self) -> py::object {
            auto val = self.top();
            if (val) return py::cast(*val);
            return py::none();
        }, "Peek at top element")
        .def("clear", &shm_stack<int>::clear)
        .def("full", &shm_stack<int>::full)
        .def("capacity", &shm_stack<int>::capacity)
        .def("size", &shm_stack<int>::size)
        .def("empty", &shm_stack<int>::empty)
        .def("__len__", &shm_stack<int>::size)
        .def("__bool__", [](const shm_stack<int>& self) { return !self.empty(); });

    // ========== Hash Map ==========
    
    py::class_<shm_hash_map<int, double>>(m, "IntFloatMap")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("insert", &shm_hash_map<int, double>::insert,
             "Insert key-value pair")
        .def("find", [](shm_hash_map<int, double>& self, int key) -> py::object {
            auto* val = self.find(key);
            if (val) return py::cast(*val);
            return py::none();
        }, "Find value by key")
        .def("update", &shm_hash_map<int, double>::update,
             "Update existing key")
        .def("erase", &shm_hash_map<int, double>::erase,
             "Remove key-value pair")
        .def("contains", &shm_hash_map<int, double>::contains,
             "Check if key exists")
        .def("clear", &shm_hash_map<int, double>::clear)
        .def("__setitem__", [](shm_hash_map<int, double>& self, int key, double value) {
            self.insert_or_update(key, value);
        })
        .def("__getitem__", [](shm_hash_map<int, double>& self, int key) -> double {
            auto* val = self.find(key);
            if (!val) throw py::key_error("Key not found: " + std::to_string(key));
            return *val;
        })
        .def("__delitem__", [](shm_hash_map<int, double>& self, int key) {
            if (!self.erase(key)) {
                throw py::key_error("Key not found: " + std::to_string(key));
            }
        })
        .def("__contains__", &shm_hash_map<int, double>::contains)
        .def("get", [](shm_hash_map<int, double>& self, int key, py::object default_val) -> py::object {
            auto* val = self.find(key);
            if (val) return py::cast(*val);
            return default_val;
        }, py::arg("key"), py::arg("default") = py::none())
        .def("items", [](shm_hash_map<int, double>& self) {
            py::list items;
            self.for_each([&items](int key, double value) {
                items.append(py::make_tuple(key, value));
            });
            return items;
        })
        .def("keys", [](shm_hash_map<int, double>& self) {
            py::list keys;
            self.for_each([&keys](int key, double) {
                keys.append(key);
            });
            return keys;
        })
        .def("values", [](shm_hash_map<int, double>& self) {
            py::list values;
            self.for_each([&values](int, double value) {
                values.append(value);
            });
            return values;
        })
        .def("size", &shm_hash_map<int, double>::size)
        .def("empty", &shm_hash_map<int, double>::empty)
        .def("__len__", &shm_hash_map<int, double>::size)
        .def("__bool__", [](const shm_hash_map<int, double>& self) { return !self.empty(); });

    // String key variant
    py::class_<shm_hash_map<uint64_t, double>>(m, "StringFloatMap")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("__setitem__", [](shm_hash_map<uint64_t, double>& self, 
                               const std::string& key, double value) {
            // Simple string hash for demo
            std::hash<std::string> hasher;
            self.insert_or_update(hasher(key), value);
        })
        .def("__getitem__", [](shm_hash_map<uint64_t, double>& self, 
                               const std::string& key) -> double {
            std::hash<std::string> hasher;
            auto* val = self.find(hasher(key));
            if (!val) throw py::key_error("Key not found: " + key);
            return *val;
        });

    // ========== Set ==========
    
    py::class_<shm_set<int>>(m, "IntSet")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("capacity") = 0)
        .def("insert", &shm_set<int>::insert, "Add element to set")
        .def("add", &shm_set<int>::insert, "Alias for insert")
        .def("erase", &shm_set<int>::erase, "Remove element from set")
        .def("remove", &shm_set<int>::erase, "Alias for erase")
        .def("contains", &shm_set<int>::contains, "Check if element exists")
        .def("clear", &shm_set<int>::clear)
        .def("__contains__", &shm_set<int>::contains)
        .def("union", [](shm_set<int>& self, posix_shm& shm, 
                        const std::string& name, shm_set<int>& other) {
            return self.set_union(shm, name, other);
        })
        .def("intersection", [](shm_set<int>& self, posix_shm& shm,
                               const std::string& name, shm_set<int>& other) {
            return self.set_intersection(shm, name, other);
        })
        .def("difference", [](shm_set<int>& self, posix_shm& shm,
                            const std::string& name, shm_set<int>& other) {
            return self.set_difference(shm, name, other);
        })
        .def("is_subset", &shm_set<int>::is_subset_of)
        .def("is_superset", &shm_set<int>::is_superset_of)
        .def("is_disjoint", &shm_set<int>::is_disjoint)
        .def("size", &shm_set<int>::size)
        .def("empty", &shm_set<int>::empty)
        .def("__len__", &shm_set<int>::size)
        .def("__bool__", [](const shm_set<int>& self) { return !self.empty(); });

    // ========== Bitset ==========
    
    py::class_<shm_bitset<1024>>(m, "Bitset1024")
        .def(py::init<posix_shm&, const std::string&>(),
             py::arg("shm"), py::arg("name"))
        .def("set", py::overload_cast<size_t>(&shm_bitset<1024>::set),
             "Set bit to true")
        .def("set", py::overload_cast<size_t, bool>(&shm_bitset<1024>::set),
             "Set bit to value")
        .def("reset", py::overload_cast<size_t>(&shm_bitset<1024>::reset),
             "Clear bit")
        .def("flip", py::overload_cast<size_t>(&shm_bitset<1024>::flip),
             "Flip bit")
        .def("test", &shm_bitset<1024>::test, "Test if bit is set")
        .def("set_all", py::overload_cast<>(&shm_bitset<1024>::set),
             "Set all bits")
        .def("reset_all", py::overload_cast<>(&shm_bitset<1024>::reset),
             "Clear all bits")
        .def("flip_all", py::overload_cast<>(&shm_bitset<1024>::flip),
             "Flip all bits")
        .def("count", &shm_bitset<1024>::count, "Count set bits")
        .def("size", &shm_bitset<1024>::size, "Total number of bits")
        .def("all", &shm_bitset<1024>::all, "Check if all bits set")
        .def("any", &shm_bitset<1024>::any, "Check if any bit set")
        .def("none", &shm_bitset<1024>::none, "Check if no bits set")
        .def("find_first", &shm_bitset<1024>::find_first,
             "Find first set bit")
        .def("find_next", &shm_bitset<1024>::find_next,
             "Find next set bit after position")
        .def("__getitem__", &shm_bitset<1024>::test)
        .def("__setitem__", [](shm_bitset<1024>& self, size_t pos, bool val) {
            self.set(pos, val);
        })
        .def("__len__", &shm_bitset<1024>::size)
        .def("__repr__", [](shm_bitset<1024>& self) {
            return "<Bitset1024 set=" + std::to_string(self.count()) + 
                   "/" + std::to_string(self.size()) + ">";
        });

    // ========== Array ==========
    
    py::class_<shm_array<double>>(m, "FloatArray")
        .def(py::init<posix_shm&, const std::string&, size_t>(),
             py::arg("shm"), py::arg("name"), py::arg("size"))
        .def("__getitem__", [](shm_array<double>& self, size_t i) {
            if (i >= self.size()) throw py::index_error("Index out of range");
            return self[i];
        })
        .def("__setitem__", [](shm_array<double>& self, size_t i, double val) {
            if (i >= self.size()) throw py::index_error("Index out of range");
            self[i] = val;
        })
        .def("__len__", &shm_array<double>::size)
        .def("to_numpy", [](shm_array<double>& self) {
            // Zero-copy numpy array view
            return py::array_t<double>(
                {self.size()},  // shape
                {sizeof(double)},  // strides
                self.data(),  // data pointer
                py::cast(self)  // parent object to keep alive
            );
        }, "Get numpy array view (zero-copy)")
        .def("from_numpy", [](shm_array<double>& self, py::array_t<double> arr) {
            if (arr.size() != self.size()) {
                throw std::runtime_error("Array size mismatch");
            }
            std::memcpy(self.data(), arr.data(), self.size() * sizeof(double));
        }, "Copy from numpy array");

    // ========== Utility Functions ==========
    
    m.def("exists", [](posix_shm& shm, const std::string& type, const std::string& name) {
        if (type == "queue_int") return shm_queue<int>::exists(shm, name);
        if (type == "stack_int") return shm_stack<int>::exists(shm, name);
        if (type == "map_int_float") return shm_hash_map<int, double>::exists(shm, name);
        if (type == "set_int") return shm_set<int>::exists(shm, name);
        if (type == "bitset_1024") return shm_bitset<1024>::exists(shm, name);
        return false;
    }, "Check if a data structure exists in shared memory");

    // ========== Examples in docstring ==========
    
    m.attr("__doc__") = R"pbdoc(
        POSIX Shared Memory Data Structures for Python
        
        Example:
        --------
        >>> import posix_shm_py as shm
        >>> 
        >>> # Create shared memory
        >>> mem = shm.SharedMemory("/my_memory", 10_000_000)
        >>> 
        >>> # Use a queue
        >>> queue = shm.IntQueue(mem, "task_queue", capacity=1000)
        >>> queue.enqueue(42)
        >>> result = queue.dequeue()  # Returns 42
        >>> 
        >>> # Use a hash map with dict-like interface
        >>> map = shm.IntFloatMap(mem, "params", capacity=100)
        >>> map[1] = 3.14
        >>> print(map[1])  # 3.14
        >>> 
        >>> # Use a bitset
        >>> bits = shm.Bitset1024(mem, "flags")
        >>> bits[10] = True
        >>> if bits[10]:
        >>>     print("Bit 10 is set")
        >>> 
        >>> # Clean up
        >>> mem.unlink()
    )pbdoc";
}