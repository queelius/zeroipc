#include "shm_list.hpp"
#include <iostream>

int main() {
    const size_t capacity = 100; // Example capacity
    ShmList<int> shmList(capacity);

    shmList.pop(1); // Remove the second element

    std::cout << "List after removing an element: ";
    shmList.traverse();

    return 0;
}
