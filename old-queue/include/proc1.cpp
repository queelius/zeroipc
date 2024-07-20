#include "shm_list.hpp"

int main() {
    MemoryPool pool(POOL_SIZE);
    ShmList<int> shmList(pool);

    shmList.push_back(1);
    shmList.push_back(2);
    shmList.push_back(3);

    std::cout << "List after adding elements: ";
    shmList.traverse();

    return 0;
}
