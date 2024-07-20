#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <optional>
#include <string>
#include <iostream> // For debugging

/**
 * @brief CommandQueue is a shared memory structure that other processes can access.
 * @tparam Command The type of the command stored in the queue.
 */
template <typename Command>
class CommandQueue {
public:
    /**
     * @brief Construct a new Command Queue object.
     * 
     * @param capacity The maximum number of commands that can be stored in the queue.
     * @param resource_name The name of the shared memory resource.
     */
    CommandQueue(size_t capacity, const std::string& resource_name);

    /**
     * @brief Destroy the Command Queue object and release resources.
     */
    ~CommandQueue();

    /**
     * @brief Check if the queue is empty.
     * 
     * @return true if the queue is empty, false otherwise.
     */
    bool isEmpty() const;

    /**
     * @brief Check if the queue is full.
     * 
     * @return true if the queue is full, false otherwise.
     */
    bool isFull() const;

    /**
     * @brief Push a command onto the queue.
     * 
     * @param command The command to push.
     * @return true if the command was pushed, false if the queue is full.
     */
    bool push(const Command& command);

    /**
     * @brief Pop a single command from the queue.
     * 
     * @return An optional containing the command if the queue is not empty, std::nullopt otherwise.
     */
    std::optional<Command> pop();

    /**
     * @brief Clear the queue.
     */
    void clear();

    /**
     * @brief Get the current size of the queue.
     * 
     * @return The number of commands in the queue.
     */
    size_t getSize() const;

    // Disable copy constructor and assignment operator
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

private:
    Command* cmds;             ///< The command buffer.
    std::string resource_name; ///< The name of the shared memory resource.
    size_t capacity;           ///< The maximum capacity of the queue.
    size_t size;               ///< The current size of the queue.
    size_t front;              ///< The index of the front of the queue.
    size_t rear;               ///< The index of the rear of the queue.
    int fd;                    ///< The file descriptor for the shared memory object.
};

template <typename Command>
CommandQueue<Command>::CommandQueue(size_t capacity, const std::string& resource_name)
    : cmds(nullptr), resource_name(resource_name), capacity(capacity), size(0), front(0), rear(0), fd(-1)
{
    std::cout << "Creating CommandQueue with capacity: " << capacity << ", resource name: " << resource_name << std::endl;

    // Unlink any existing shared memory object with the same name
    shm_unlink(resource_name.c_str());

    fd = shm_open(resource_name.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        throw std::runtime_error("shm_open failed");
    }
    if (ftruncate(fd, capacity * sizeof(Command)) == -1) {
        close(fd);
        throw std::runtime_error("ftruncate failed");
    }

    cmds = static_cast<Command*>(mmap(nullptr, capacity * sizeof(Command), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (cmds == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("mmap failed");
    }

    std::memset(cmds, 0, capacity * sizeof(Command));
}

template <typename Command>
CommandQueue<Command>::~CommandQueue() {
    std::cout << "Destroying CommandQueue" << std::endl;

    if (cmds && cmds != MAP_FAILED) {
        munmap(cmds, capacity * sizeof(Command));
    }
    if (fd != -1) {
        close(fd);
    }
    shm_unlink(resource_name.c_str());
}

template <typename Command>
bool CommandQueue<Command>::isEmpty() const {
    return size == 0;
}

template <typename Command>
bool CommandQueue<Command>::isFull() const {
    return size == capacity;
}

template <typename Command>
bool CommandQueue<Command>::push(const Command& command) {
    if (isFull()) {
        return false;
    }
    cmds[rear] = command;
    rear = (rear + 1) % capacity;
    ++size;
    return true;
}

template <typename Command>
std::optional<Command> CommandQueue<Command>::pop() {
    if (isEmpty()) {
        return std::nullopt;
    }

    Command command = cmds[front];
    front = (front + 1) % capacity;
    --size;
    return command;
}

template <typename Command>
void CommandQueue<Command>::clear() {
    size = 0;
    front = 0;
    rear = 0;
    std::memset(cmds, 0, capacity * sizeof(Command));
}

template <typename Command>
size_t CommandQueue<Command>::getSize() const {
    return size;
}
