// ZeroIPC Shared Memory Inspector CLI Tool v3
// Enhanced with REPL mode and creation/manipulation support
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <sstream>
#include <memory>
#include <map>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// Include ZeroIPC headers for creation and manipulation
#include "zeroipc/memory.h"
#include "zeroipc/array.h"
#include "zeroipc/queue.h"
#include "zeroipc/stack.h"
#include "zeroipc/semaphore.h"
#include "zeroipc/barrier.h"
#include "zeroipc/latch.h"
#include "zeroipc/ring.h"
#include "zeroipc/map.h"
#include "zeroipc/set.h"
#include "zeroipc/pool.h"
#include "zeroipc/channel.h"
#include "zeroipc/stream.h"
#include "vfs.h"

// Matches SPECIFICATION.md exactly
struct TableHeader {
    uint32_t magic;         // 0x5A49504D ('ZIPM')
    uint32_t version;       // Format version (currently 1)
    uint32_t entry_count;   // Number of active entries
    uint32_t next_offset;   // Next allocation offset
};

struct TableEntry {
    char name[32];
    uint32_t offset;
    uint32_t size;
};

// Structure headers for detection
struct ArrayHeader {
    uint64_t capacity;
};

struct QueueHeader {
    uint64_t head;
    uint64_t tail;
    uint64_t capacity;
};

struct StackHeader {
    int32_t top;
    uint32_t capacity;
    uint32_t elem_size;
};

struct SemaphoreHeader {
    std::atomic<int32_t> count;
    std::atomic<int32_t> waiting;
    int32_t max_count;
    int32_t _padding;
};

struct BarrierHeader {
    std::atomic<int32_t> arrived;
    std::atomic<int32_t> generation;
    int32_t num_participants;
    int32_t _padding;
};

struct LatchHeader {
    std::atomic<int32_t> count;
    int32_t initial_count;
    int32_t _padding[2];
};

class SharedMemoryInspector {
private:
    std::string shm_name_;
    void* base_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
    TableHeader* header_ = nullptr;
    TableEntry* entries_ = nullptr;
    bool read_write_ = false;

    static constexpr uint32_t MAGIC = 0x5A49504D; // 'ZIPM'

public:
    SharedMemoryInspector(const std::string& name, bool read_write = false)
        : shm_name_(name), read_write_(read_write) {
        if (!shm_name_.empty() && shm_name_[0] != '/') {
            shm_name_ = "/" + shm_name_;
        }
    }

    ~SharedMemoryInspector() {
        cleanup();
    }

    bool open() {
        int flags = read_write_ ? O_RDWR : O_RDONLY;
        fd_ = shm_open(shm_name_.c_str(), flags, 0);
        if (fd_ == -1) {
            std::cerr << "Error: Failed to open shared memory '" << shm_name_
                      << "': " << strerror(errno) << "\n";
            return false;
        }

        struct stat st;
        if (fstat(fd_, &st) == -1) {
            std::cerr << "Error: Failed to get size of shared memory: "
                      << strerror(errno) << "\n";
            cleanup();
            return false;
        }
        size_ = st.st_size;

        int prot = read_write_ ? (PROT_READ | PROT_WRITE) : PROT_READ;
        base_ = mmap(nullptr, size_, prot, MAP_SHARED, fd_, 0);
        if (base_ == MAP_FAILED) {
            std::cerr << "Error: Failed to map shared memory: "
                      << strerror(errno) << "\n";
            cleanup();
            return false;
        }

        header_ = static_cast<TableHeader*>(base_);
        if (header_->magic != MAGIC) {
            std::cerr << "Error: Invalid magic number. Expected 0x"
                      << std::hex << MAGIC << ", got 0x" << header_->magic << std::dec << "\n";
            cleanup();
            return false;
        }

        entries_ = reinterpret_cast<TableEntry*>(
            static_cast<char*>(base_) + sizeof(TableHeader));

        return true;
    }

    void printSummary() {
        std::cout << "\n=== Shared Memory Summary ===\n";
        std::cout << "Name: " << shm_name_ << "\n";
        std::cout << "Mode: " << (read_write_ ? "Read/Write" : "Read-Only") << "\n";
        std::cout << "Total Size: " << formatSize(size_) << " (" << size_ << " bytes)\n";
        std::cout << "Format Version: " << header_->version << "\n";
        std::cout << "Active Entries: " << header_->entry_count << "\n";
        std::cout << "Next Allocation Offset: 0x" << std::hex << header_->next_offset
                  << " (" << std::dec << header_->next_offset << " bytes)\n";

        size_t used = header_->next_offset;
        size_t free = (used < size_) ? size_ - used : 0;
        double usage = (size_ > 0) ? (100.0 * used / size_) : 0;

        std::cout << "Memory Used: " << formatSize(used) << " ("
                  << std::fixed << std::setprecision(1) << usage << "%)\n";
        std::cout << "Memory Free: " << formatSize(free) << "\n";
    }

    void printTable(bool verbose = false) {
        std::cout << "\n=== Table Entries ===\n";

        if (header_->entry_count == 0) {
            std::cout << "No entries in table\n";
            return;
        }

        std::cout << std::left << std::setw(4) << "#"
                  << std::setw(32) << "Name"
                  << std::setw(12) << "Offset"
                  << std::setw(12) << "Size";

        if (verbose) {
            std::cout << std::setw(15) << "Type";
        }
        std::cout << "\n";

        std::cout << std::string(verbose ? 75 : 60, '-') << "\n";

        for (uint32_t i = 0; i < header_->entry_count; i++) {
            std::cout << std::left << std::setw(4) << i
                      << std::setw(32) << entries_[i].name
                      << "0x" << std::hex << std::setw(10) << entries_[i].offset << std::dec
                      << std::setw(12) << formatSize(entries_[i].size);

            if (verbose) {
                std::cout << std::setw(15) << detectStructureType(entries_[i]);
            }
            std::cout << "\n";
        }
    }

    void printStructureInfo(const std::string& entry_name) {
        TableEntry* entry = findEntry(entry_name);
        if (!entry) {
            std::cerr << "Error: Entry '" << entry_name << "' not found\n";
            return;
        }

        std::cout << "\n=== Structure: " << entry_name << " ===\n";
        std::cout << "Offset: 0x" << std::hex << entry->offset << std::dec
                  << " (" << entry->offset << " bytes)\n";
        std::cout << "Size: " << formatSize(entry->size)
                  << " (" << entry->size << " bytes)\n";

        std::string type = detectStructureType(*entry);
        std::cout << "Type: " << type << "\n\n";

        const char* data = static_cast<const char*>(base_) + entry->offset;

        if (type == "Semaphore") {
            const SemaphoreHeader* hdr = reinterpret_cast<const SemaphoreHeader*>(data);
            std::cout << "Count: " << hdr->count.load() << "\n";
            std::cout << "Waiting: " << hdr->waiting.load() << "\n";
            std::cout << "Max Count: " << (hdr->max_count == 0 ? "unbounded" : std::to_string(hdr->max_count)) << "\n";
        }
        else if (type == "Barrier") {
            const BarrierHeader* hdr = reinterpret_cast<const BarrierHeader*>(data);
            std::cout << "Arrived: " << hdr->arrived.load() << " / " << hdr->num_participants << "\n";
            std::cout << "Generation: " << hdr->generation.load() << "\n";
            std::cout << "Num Participants: " << hdr->num_participants << "\n";
        }
        else if (type == "Latch") {
            const LatchHeader* hdr = reinterpret_cast<const LatchHeader*>(data);
            std::cout << "Count: " << hdr->count.load() << " / " << hdr->initial_count << "\n";
            std::cout << "Initial Count: " << hdr->initial_count << "\n";
            std::cout << "Status: " << (hdr->count.load() == 0 ? "Released" : "Counting down") << "\n";
        }
        else if (type == "Array") {
            const ArrayHeader* hdr = reinterpret_cast<const ArrayHeader*>(data);
            std::cout << "Capacity: " << hdr->capacity << " elements\n";
            size_t elem_size = (entry->size - sizeof(ArrayHeader)) / hdr->capacity;
            std::cout << "Element Size: " << elem_size << " bytes\n";
            std::cout << "Total Data: " << formatSize(hdr->capacity * elem_size) << "\n";
        }
        else if (type == "Queue") {
            const QueueHeader* hdr = reinterpret_cast<const QueueHeader*>(data);
            std::cout << "Head: " << hdr->head << "\n";
            std::cout << "Tail: " << hdr->tail << "\n";
            std::cout << "Capacity: " << hdr->capacity << " elements\n";

            uint64_t count = (hdr->tail >= hdr->head)
                ? (hdr->tail - hdr->head)
                : (hdr->capacity - hdr->head + hdr->tail);
            std::cout << "Current Items: " << count << "\n";
            std::cout << "Fill: " << std::fixed << std::setprecision(1)
                      << (100.0 * count / hdr->capacity) << "%\n";
        }
        else if (type == "Stack") {
            const StackHeader* hdr = reinterpret_cast<const StackHeader*>(data);
            std::cout << "Top: " << hdr->top << "\n";
            std::cout << "Capacity: " << hdr->capacity << " elements\n";
            std::cout << "Current Items: " << (hdr->top + 1) << "\n";
            std::cout << "Element Size: " << hdr->elem_size << " bytes\n";
        }
    }

    void printHexDump(const std::string& entry_name, size_t max_bytes = 256) {
        TableEntry* entry = findEntry(entry_name);
        if (!entry) {
            std::cerr << "Error: Entry '" << entry_name << "' not found\n";
            return;
        }

        std::cout << "\n=== Hex Dump: " << entry_name << " ===\n";
        std::cout << "Offset: 0x" << std::hex << entry->offset << std::dec << "\n";
        std::cout << "Size: " << formatSize(entry->size) << "\n\n";

        size_t bytes_to_dump = std::min(max_bytes, static_cast<size_t>(entry->size));
        const uint8_t* data = static_cast<const uint8_t*>(base_) + entry->offset;

        for (size_t i = 0; i < bytes_to_dump; i += 16) {
            std::cout << std::hex << std::setfill('0') << std::setw(8) << i << "  ";

            for (size_t j = 0; j < 16; j++) {
                if (i + j < bytes_to_dump) {
                    std::cout << std::setw(2) << static_cast<int>(data[i + j]) << " ";
                } else {
                    std::cout << "   ";
                }
                if (j == 7) std::cout << " ";
            }

            std::cout << " |";

            for (size_t j = 0; j < 16 && i + j < bytes_to_dump; j++) {
                char c = static_cast<char>(data[i + j]);
                std::cout << (isprint(c) ? c : '.');
            }

            std::cout << "|\n";
        }

        if (bytes_to_dump < entry->size) {
            std::cout << std::dec << "... (" << (entry->size - bytes_to_dump)
                      << " more bytes)\n";
        }
    }

    void listSharedMemory() {
        std::cout << "\n=== Available Shared Memory Objects ===\n";
        std::cout << std::left << std::setw(40) << "Name" << std::setw(12) << "Size" << "\n";
        std::cout << std::string(52, '-') << "\n";
        system("ls -l /dev/shm/ 2>/dev/null | grep -v '^total' | grep -v '^d' | awk '{printf \"%-40s %s\\n\", $9, $5}'");
    }

    const std::string& getName() const { return shm_name_; }
    bool isOpen() const { return base_ != nullptr; }
    bool isReadWrite() const { return read_write_; }

private:
    void cleanup() {
        if (base_ != nullptr && base_ != MAP_FAILED) {
            munmap(base_, size_);
            base_ = nullptr;
        }
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
    }

    TableEntry* findEntry(const std::string& name) {
        for (uint32_t i = 0; i < header_->entry_count; i++) {
            if (strcmp(entries_[i].name, name.c_str()) == 0) {
                return &entries_[i];
            }
        }
        return nullptr;
    }

    std::string detectStructureType(const TableEntry& entry) {
        if (entry.size == 16) {
            const char* data = static_cast<const char*>(base_) + entry.offset;
            const int32_t* values = reinterpret_cast<const int32_t*>(data);

            if (values[2] >= 0) {
                if (values[0] >= 0 && values[0] <= values[2]) {
                    return "Barrier";
                }
                return "Semaphore";
            }
            return "Latch";
        }

        if (entry.size > sizeof(ArrayHeader)) {
            const char* data = static_cast<const char*>(base_) + entry.offset;
            const ArrayHeader* hdr = reinterpret_cast<const ArrayHeader*>(data);

            if (hdr->capacity > 0 && hdr->capacity < 1000000000) {
                size_t expected_size = sizeof(ArrayHeader) + hdr->capacity * 1;
                if (entry.size >= expected_size) {
                    return "Array";
                }
            }
        }

        if (entry.size > sizeof(QueueHeader)) {
            const char* data = static_cast<const char*>(base_) + entry.offset;
            const QueueHeader* hdr = reinterpret_cast<const QueueHeader*>(data);

            if (hdr->capacity > 0 && hdr->capacity < 1000000000 &&
                hdr->head < hdr->capacity && hdr->tail < hdr->capacity) {
                return "Queue";
            }
        }

        if (entry.size > sizeof(StackHeader)) {
            return "Stack";
        }

        return "Unknown";
    }

    std::string formatSize(size_t bytes) {
        std::stringstream ss;
        if (bytes < 1024) {
            ss << bytes << " B";
        } else if (bytes < 1024 * 1024) {
            ss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
        } else if (bytes < 1024 * 1024 * 1024) {
            ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024)) << " MB";
        } else {
            ss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024 * 1024)) << " GB";
        }
        return ss.str();
    }
};

// REPL class for interactive mode
class ZeroIPCRepl {
private:
    std::unique_ptr<zeroipc::Memory> memory_;
    std::string current_shm_;
    bool running_ = true;
    zeroipc::vfs::NavigationContext nav_context_;

    // Storage for created structures (keep them alive)
    std::map<std::string, std::shared_ptr<void>> structures_;

public:
    void run() {
        std::cout << "ZeroIPC Interactive Shell v3.0 - Virtual Filesystem Interface\n";
        std::cout << "Type 'help' for available commands, 'quit' to exit\n\n";

        while (running_) {
            std::cout << nav_context_.prompt();

            std::string line;
            if (!std::getline(std::cin, line)) {
                break;  // EOF
            }

            if (line.empty()) continue;

            std::vector<std::string> tokens = tokenize(line);
            if (tokens.empty()) continue;

            processCommand(tokens);
        }
    }

private:
    std::vector<std::string> tokenize(const std::string& line) {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    void processCommand(const std::vector<std::string>& tokens) {
        const std::string& cmd = tokens[0];

        try {
            if (cmd == "help" || cmd == "?") {
                printHelp();
            }
            else if (cmd == "quit" || cmd == "exit") {
                running_ = false;
            }
            else if (cmd == "create") {
                cmdCreate(tokens);
            }
            else if (cmd == "open") {
                cmdOpen(tokens);
            }
            else if (cmd == "close") {
                cmdClose();
            }
            else if (cmd == "summary") {
                cmdSummary();
            }
            else if (cmd == "table") {
                cmdTable(tokens);
            }
            else if (cmd == "info") {
                cmdInfo(tokens);
            }
            else if (cmd == "dump") {
                cmdDump(tokens);
            }
            else if (cmd == "list") {
                cmdList();
            }
            else if (cmd == "create-array") {
                cmdCreateArray(tokens);
            }
            else if (cmd == "create-queue") {
                cmdCreateQueue(tokens);
            }
            else if (cmd == "create-stack") {
                cmdCreateStack(tokens);
            }
            else if (cmd == "create-semaphore") {
                cmdCreateSemaphore(tokens);
            }
            else if (cmd == "create-barrier") {
                cmdCreateBarrier(tokens);
            }
            else if (cmd == "create-latch") {
                cmdCreateLatch(tokens);
            }
            else if (cmd == "push") {
                cmdPush(tokens);
            }
            else if (cmd == "pop") {
                cmdPop(tokens);
            }
            else if (cmd == "enqueue") {
                cmdEnqueue(tokens);
            }
            else if (cmd == "dequeue") {
                cmdDequeue(tokens);
            }
            else if (cmd == "acquire") {
                cmdAcquire(tokens);
            }
            else if (cmd == "release") {
                cmdRelease(tokens);
            }
            else if (cmd == "wait") {
                cmdWait(tokens);
            }
            else if (cmd == "count-down") {
                cmdCountDown(tokens);
            }
            else if (cmd == "create-ring") {
                cmdCreateRing(tokens);
            }
            else if (cmd == "ring-write") {
                cmdRingWrite(tokens);
            }
            else if (cmd == "ring-read") {
                cmdRingRead(tokens);
            }
            else if (cmd == "create-map") {
                cmdCreateMap(tokens);
            }
            else if (cmd == "map-insert") {
                cmdMapInsert(tokens);
            }
            else if (cmd == "map-find") {
                cmdMapFind(tokens);
            }
            else if (cmd == "map-erase") {
                cmdMapErase(tokens);
            }
            else if (cmd == "create-set") {
                cmdCreateSet(tokens);
            }
            else if (cmd == "set-insert") {
                cmdSetInsert(tokens);
            }
            else if (cmd == "set-contains") {
                cmdSetContains(tokens);
            }
            else if (cmd == "set-erase") {
                cmdSetErase(tokens);
            }
            else if (cmd == "create-pool") {
                cmdCreatePool(tokens);
            }
            else if (cmd == "create-channel") {
                cmdCreateChannel(tokens);
            }
            else if (cmd == "channel-send") {
                cmdChannelSend(tokens);
            }
            else if (cmd == "channel-recv") {
                cmdChannelRecv(tokens);
            }
            else if (cmd == "channel-close") {
                cmdChannelClose(tokens);
            }
            else if (cmd == "ls") {
                cmdLs(tokens);
            }
            else if (cmd == "cd") {
                cmdCd(tokens);
            }
            else if (cmd == "pwd") {
                cmdPwd(tokens);
            }
            else {
                std::cerr << "Unknown command: " << cmd << "\n";
                std::cerr << "Type 'help' for available commands\n";
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

    void printHelp() {
        std::cout << "\n=== ZeroIPC Commands ===\n\n";

        std::cout << "Navigation (Virtual Filesystem):\n";
        std::cout << "  ls [path]                            List contents at current location or path\n";
        std::cout << "  cd <path>                            Change directory\n";
        std::cout << "  pwd                                  Print working directory\n\n";

        std::cout << "Memory Management:\n";
        std::cout << "  create <name> <size_mb>              Create new shared memory\n";
        std::cout << "  open <name>                          Open existing shared memory\n";
        std::cout << "  close                                Close current shared memory\n";
        std::cout << "  list                                 List all shared memory objects\n\n";

        std::cout << "Inspection:\n";
        std::cout << "  summary                              Show memory summary\n";
        std::cout << "  table [verbose]                      Show table entries\n";
        std::cout << "  info <name>                          Show structure info\n";
        std::cout << "  dump <name> [bytes]                  Hex dump of structure\n\n";

        std::cout << "Structure Creation:\n";
        std::cout << "  create-array <name> <capacity> <elem_size>      Create array\n";
        std::cout << "  create-queue <name> <capacity> <elem_size>      Create queue\n";
        std::cout << "  create-stack <name> <capacity> <elem_size>      Create stack\n";
        std::cout << "  create-ring <name> <capacity> <elem_size>       Create ring buffer\n";
        std::cout << "  create-map <name> <capacity> <k_sz> <v_sz>      Create map\n";
        std::cout << "  create-set <name> <capacity> <elem_size>        Create set\n";
        std::cout << "  create-pool <name> <capacity> <elem_size>       Create pool\n";
        std::cout << "  create-channel <name> <capacity> <elem_size>    Create channel\n";
        std::cout << "  create-semaphore <name> <count> [max]           Create semaphore\n";
        std::cout << "  create-barrier <name> <participants>            Create barrier\n";
        std::cout << "  create-latch <name> <count>                     Create latch\n\n";

        std::cout << "Structure Manipulation (int32 only):\n";
        std::cout << "  push <stack_name> <value>            Push to stack\n";
        std::cout << "  pop <stack_name>                     Pop from stack\n";
        std::cout << "  enqueue <queue_name> <value>         Enqueue to queue\n";
        std::cout << "  dequeue <queue_name>                 Dequeue from queue\n";
        std::cout << "  ring-write <ring_name> <value>       Write to ring buffer\n";
        std::cout << "  ring-read <ring_name>                Read from ring buffer\n";
        std::cout << "  map-insert <map_name> <key> <value>  Insert into map\n";
        std::cout << "  map-find <map_name> <key>            Find in map\n";
        std::cout << "  map-erase <map_name> <key>           Erase from map\n";
        std::cout << "  set-insert <set_name> <value>        Insert into set\n";
        std::cout << "  set-contains <set_name> <value>      Check if set contains\n";
        std::cout << "  set-erase <set_name> <value>         Erase from set\n";
        std::cout << "  channel-send <ch_name> <value>       Send to channel\n";
        std::cout << "  channel-recv <ch_name>               Receive from channel\n";
        std::cout << "  channel-close <ch_name>              Close channel\n";
        std::cout << "  acquire <semaphore_name>             Acquire semaphore\n";
        std::cout << "  release <semaphore_name>             Release semaphore\n";
        std::cout << "  wait <barrier/latch_name>            Wait at barrier/latch\n";
        std::cout << "  count-down <latch_name> [n]          Count down latch\n\n";

        std::cout << "General:\n";
        std::cout << "  help, ?                              Show this help\n";
        std::cout << "  quit, exit                           Exit REPL\n\n";
    }

    void cmdCreate(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) {
            std::cerr << "Usage: create <name> <size_mb>\n";
            return;
        }

        std::string name = tokens[1];
        if (name[0] != '/') name = "/" + name;

        size_t size_mb = std::stoull(tokens[2]);
        size_t size = size_mb * 1024 * 1024;

        memory_ = std::make_unique<zeroipc::Memory>(name, size, 256);  // 256 table entries
        current_shm_ = name;

        std::cout << "Created shared memory '" << name << "' (" << size_mb << " MB)\n";
    }

    void cmdOpen(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            std::cerr << "Usage: open <name>\n";
            return;
        }

        std::string name = tokens[1];
        if (name[0] != '/') name = "/" + name;

        memory_ = std::make_unique<zeroipc::Memory>(name);
        current_shm_ = name;

        std::cout << "Opened shared memory '" << name << "'\n";
    }

    void cmdClose() {
        if (!memory_) {
            std::cerr << "No shared memory currently open\n";
            return;
        }

        structures_.clear();
        memory_.reset();
        current_shm_.clear();

        std::cout << "Closed shared memory\n";
    }

    void cmdSummary() {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'open <name>' first.\n";
            return;
        }

        SharedMemoryInspector inspector(current_shm_);
        if (inspector.open()) {
            inspector.printSummary();
        }
    }

    void cmdTable(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'open <name>' first.\n";
            return;
        }

        bool verbose = (tokens.size() > 1 && tokens[1] == "verbose");

        SharedMemoryInspector inspector(current_shm_);
        if (inspector.open()) {
            inspector.printTable(verbose);
        }
    }

    void cmdInfo(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'open <name>' first.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: info <name>\n";
            return;
        }

        SharedMemoryInspector inspector(current_shm_);
        if (inspector.open()) {
            inspector.printStructureInfo(tokens[1]);
        }
    }

    void cmdDump(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'open <name>' first.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: dump <name> [bytes]\n";
            return;
        }

        size_t bytes = 256;
        if (tokens.size() > 2) {
            bytes = std::stoull(tokens[2]);
        }

        SharedMemoryInspector inspector(current_shm_);
        if (inspector.open()) {
            inspector.printHexDump(tokens[1], bytes);
        }
    }

    void cmdList() {
        SharedMemoryInspector inspector("");
        inspector.listSharedMemory();
    }

    void cmdCreateArray(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-array <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);
        size_t elem_size = std::stoull(tokens[3]);

        if (elem_size == 4) {
            auto arr = std::make_shared<zeroipc::Array<int32_t>>(*memory_, name, capacity);
            structures_[name] = arr;
            std::cout << "Created array<int32> '" << name << "' with " << capacity << " elements\n";
        }
        else if (elem_size == 8) {
            auto arr = std::make_shared<zeroipc::Array<int64_t>>(*memory_, name, capacity);
            structures_[name] = arr;
            std::cout << "Created array<int64> '" << name << "' with " << capacity << " elements\n";
        }
        else {
            std::cerr << "Unsupported element size. Use 4 or 8 bytes.\n";
        }
    }

    void cmdCreateQueue(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-queue <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);
        size_t elem_size = std::stoull(tokens[3]);

        if (elem_size == 4) {
            auto q = std::make_shared<zeroipc::Queue<int32_t>>(*memory_, name, capacity);
            structures_[name] = q;
            std::cout << "Created queue<int32> '" << name << "' with capacity " << capacity << "\n";
        }
        else if (elem_size == 8) {
            auto q = std::make_shared<zeroipc::Queue<int64_t>>(*memory_, name, capacity);
            structures_[name] = q;
            std::cout << "Created queue<int64> '" << name << "' with capacity " << capacity << "\n";
        }
        else {
            std::cerr << "Unsupported element size. Use 4 or 8 bytes.\n";
        }
    }

    void cmdCreateStack(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-stack <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);
        size_t elem_size = std::stoull(tokens[3]);

        if (elem_size == 4) {
            auto s = std::make_shared<zeroipc::Stack<int32_t>>(*memory_, name, capacity);
            structures_[name] = s;
            std::cout << "Created stack<int32> '" << name << "' with capacity " << capacity << "\n";
        }
        else if (elem_size == 8) {
            auto s = std::make_shared<zeroipc::Stack<int64_t>>(*memory_, name, capacity);
            structures_[name] = s;
            std::cout << "Created stack<int64> '" << name << "' with capacity " << capacity << "\n";
        }
        else {
            std::cerr << "Unsupported element size. Use 4 or 8 bytes.\n";
        }
    }

    void cmdCreateSemaphore(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: create-semaphore <name> <count> [max_count]\n";
            return;
        }

        std::string name = tokens[1];
        int32_t count = std::stoi(tokens[2]);
        int32_t max_count = 0;

        if (tokens.size() > 3) {
            max_count = std::stoi(tokens[3]);
        }

        auto sem = std::make_shared<zeroipc::Semaphore>(*memory_, name, count, max_count);
        structures_[name] = sem;

        std::cout << "Created semaphore '" << name << "' with count " << count;
        if (max_count > 0) {
            std::cout << " (max: " << max_count << ")";
        }
        std::cout << "\n";
    }

    void cmdCreateBarrier(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: create-barrier <name> <participants>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t participants = std::stoi(tokens[2]);

        auto bar = std::make_shared<zeroipc::Barrier>(*memory_, name, participants);
        structures_[name] = bar;

        std::cout << "Created barrier '" << name << "' with " << participants << " participants\n";
    }

    void cmdCreateLatch(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: create-latch <name> <count>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t count = std::stoi(tokens[2]);

        auto lat = std::make_shared<zeroipc::Latch>(*memory_, name, count);
        structures_[name] = lat;

        std::cout << "Created latch '" << name << "' with count " << count << "\n";
    }

    void cmdPush(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: push <stack_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        // Try to get existing or create new
        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Stack<int32_t>> stack;

        if (it != structures_.end()) {
            stack = std::static_pointer_cast<zeroipc::Stack<int32_t>>(it->second);
        } else {
            stack = std::make_shared<zeroipc::Stack<int32_t>>(*memory_, name);
            structures_[name] = stack;
        }

        stack->push(value);
        std::cout << "Pushed " << value << " to stack '" << name << "'\n";
    }

    void cmdPop(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: pop <stack_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Stack<int32_t>> stack;

        if (it != structures_.end()) {
            stack = std::static_pointer_cast<zeroipc::Stack<int32_t>>(it->second);
        } else {
            stack = std::make_shared<zeroipc::Stack<int32_t>>(*memory_, name);
            structures_[name] = stack;
        }

        auto val = stack->pop();
        if (val) {
            std::cout << "Popped: " << *val << "\n";
        } else {
            std::cout << "Stack '" << name << "' is empty\n";
        }
    }

    void cmdEnqueue(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: enqueue <queue_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Queue<int32_t>> queue;

        if (it != structures_.end()) {
            queue = std::static_pointer_cast<zeroipc::Queue<int32_t>>(it->second);
        } else {
            queue = std::make_shared<zeroipc::Queue<int32_t>>(*memory_, name);
            structures_[name] = queue;
        }

        if (queue->push(value)) {
            std::cout << "Enqueued " << value << " to queue '" << name << "'\n";
        } else {
            std::cout << "Queue '" << name << "' is full\n";
        }
    }

    void cmdDequeue(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: dequeue <queue_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Queue<int32_t>> queue;

        if (it != structures_.end()) {
            queue = std::static_pointer_cast<zeroipc::Queue<int32_t>>(it->second);
        } else {
            queue = std::make_shared<zeroipc::Queue<int32_t>>(*memory_, name);
            structures_[name] = queue;
        }

        auto val = queue->pop();
        if (val) {
            std::cout << "Dequeued: " << *val << "\n";
        } else {
            std::cout << "Queue '" << name << "' is empty\n";
        }
    }

    void cmdAcquire(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: acquire <semaphore_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Semaphore> sem;

        if (it != structures_.end()) {
            sem = std::static_pointer_cast<zeroipc::Semaphore>(it->second);
        } else {
            sem = std::make_shared<zeroipc::Semaphore>(*memory_, name);
            structures_[name] = sem;
        }

        sem->acquire();
        std::cout << "Acquired semaphore '" << name << "'\n";
    }

    void cmdRelease(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: release <semaphore_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Semaphore> sem;

        if (it != structures_.end()) {
            sem = std::static_pointer_cast<zeroipc::Semaphore>(it->second);
        } else {
            sem = std::make_shared<zeroipc::Semaphore>(*memory_, name);
            structures_[name] = sem;
        }

        sem->release();
        std::cout << "Released semaphore '" << name << "'\n";
    }

    void cmdWait(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: wait <barrier/latch_name>\n";
            return;
        }

        std::string name = tokens[1];

        // Try barrier first
        try {
            auto it = structures_.find(name);
            std::shared_ptr<zeroipc::Barrier> bar;

            if (it != structures_.end()) {
                bar = std::static_pointer_cast<zeroipc::Barrier>(it->second);
            } else {
                bar = std::make_shared<zeroipc::Barrier>(*memory_, name);
                structures_[name] = bar;
            }

            bar->wait();
            std::cout << "Passed barrier '" << name << "'\n";
            return;
        } catch (...) {
            // Not a barrier, try latch
        }

        try {
            auto it = structures_.find(name);
            std::shared_ptr<zeroipc::Latch> lat;

            if (it != structures_.end()) {
                lat = std::static_pointer_cast<zeroipc::Latch>(it->second);
            } else {
                lat = std::make_shared<zeroipc::Latch>(*memory_, name);
                structures_[name] = lat;
            }

            lat->wait();
            std::cout << "Latch '" << name << "' released\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
    }

    void cmdCountDown(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: count-down <latch_name> [n]\n";
            return;
        }

        std::string name = tokens[1];
        int32_t n = 1;

        if (tokens.size() > 2) {
            n = std::stoi(tokens[2]);
        }

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Latch> lat;

        if (it != structures_.end()) {
            lat = std::static_pointer_cast<zeroipc::Latch>(it->second);
        } else {
            lat = std::make_shared<zeroipc::Latch>(*memory_, name);
            structures_[name] = lat;
        }

        lat->count_down(n);
        std::cout << "Counted down latch '" << name << "' by " << n << "\n";
    }

    // Ring buffer commands
    void cmdCreateRing(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-ring <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);
        size_t elem_size = std::stoull(tokens[3]);

        if (elem_size == 4) {
            auto ring = std::make_shared<zeroipc::Ring<int32_t>>(*memory_, name, capacity);
            structures_[name] = ring;
            std::cout << "Created ring<int32> '" << name << "' with capacity " << capacity << " bytes\n";
        }
        else {
            std::cerr << "Unsupported element size. Use 4 bytes.\n";
        }
    }

    void cmdRingWrite(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: ring-write <ring_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Ring<int32_t>> ring;

        if (it != structures_.end()) {
            ring = std::static_pointer_cast<zeroipc::Ring<int32_t>>(it->second);
        } else {
            ring = std::make_shared<zeroipc::Ring<int32_t>>(*memory_, name);
            structures_[name] = ring;
        }

        if (ring->write(value)) {
            std::cout << "Wrote " << value << " to ring '" << name << "'\n";
        } else {
            std::cout << "Ring '" << name << "' is full\n";
        }
    }

    void cmdRingRead(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: ring-read <ring_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Ring<int32_t>> ring;

        if (it != structures_.end()) {
            ring = std::static_pointer_cast<zeroipc::Ring<int32_t>>(it->second);
        } else {
            ring = std::make_shared<zeroipc::Ring<int32_t>>(*memory_, name);
            structures_[name] = ring;
        }

        auto val = ring->read();
        if (val) {
            std::cout << "Read: " << *val << "\n";
        } else {
            std::cout << "Ring '" << name << "' is empty\n";
        }
    }

    // Map commands
    void cmdCreateMap(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 5) {
            std::cerr << "Usage: create-map <name> <capacity> <key_size> <value_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);

        auto map = std::make_shared<zeroipc::Map<int32_t, int32_t>>(*memory_, name, capacity);
        structures_[name] = map;
        std::cout << "Created map<int32,int32> '" << name << "' with capacity " << capacity << "\n";
    }

    void cmdMapInsert(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: map-insert <map_name> <key> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t key = std::stoi(tokens[2]);
        int32_t value = std::stoi(tokens[3]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Map<int32_t, int32_t>> map;

        if (it != structures_.end()) {
            map = std::static_pointer_cast<zeroipc::Map<int32_t, int32_t>>(it->second);
        } else {
            map = std::make_shared<zeroipc::Map<int32_t, int32_t>>(*memory_, name);
            structures_[name] = map;
        }

        if (map->insert(key, value)) {
            std::cout << "Inserted [" << key << " => " << value << "] into map '" << name << "'\n";
        } else {
            std::cout << "Map '" << name << "' is full\n";
        }
    }

    void cmdMapFind(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: map-find <map_name> <key>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t key = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Map<int32_t, int32_t>> map;

        if (it != structures_.end()) {
            map = std::static_pointer_cast<zeroipc::Map<int32_t, int32_t>>(it->second);
        } else {
            map = std::make_shared<zeroipc::Map<int32_t, int32_t>>(*memory_, name);
            structures_[name] = map;
        }

        auto val = map->find(key);
        if (val) {
            std::cout << "Found: [" << key << " => " << *val << "]\n";
        } else {
            std::cout << "Key " << key << " not found in map '" << name << "'\n";
        }
    }

    void cmdMapErase(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: map-erase <map_name> <key>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t key = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Map<int32_t, int32_t>> map;

        if (it != structures_.end()) {
            map = std::static_pointer_cast<zeroipc::Map<int32_t, int32_t>>(it->second);
        } else {
            map = std::make_shared<zeroipc::Map<int32_t, int32_t>>(*memory_, name);
            structures_[name] = map;
        }

        if (map->erase(key)) {
            std::cout << "Erased key " << key << " from map '" << name << "'\n";
        } else {
            std::cout << "Key " << key << " not found in map '" << name << "'\n";
        }
    }

    // Set commands
    void cmdCreateSet(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-set <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);

        auto set = std::make_shared<zeroipc::Set<int32_t>>(*memory_, name, capacity);
        structures_[name] = set;
        std::cout << "Created set<int32> '" << name << "' with capacity " << capacity << "\n";
    }

    void cmdSetInsert(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: set-insert <set_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Set<int32_t>> set;

        if (it != structures_.end()) {
            set = std::static_pointer_cast<zeroipc::Set<int32_t>>(it->second);
        } else {
            set = std::make_shared<zeroipc::Set<int32_t>>(*memory_, name);
            structures_[name] = set;
        }

        if (set->insert(value)) {
            std::cout << "Inserted " << value << " into set '" << name << "'\n";
        } else {
            std::cout << "Value " << value << " already exists or set is full\n";
        }
    }

    void cmdSetContains(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: set-contains <set_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Set<int32_t>> set;

        if (it != structures_.end()) {
            set = std::static_pointer_cast<zeroipc::Set<int32_t>>(it->second);
        } else {
            set = std::make_shared<zeroipc::Set<int32_t>>(*memory_, name);
            structures_[name] = set;
        }

        if (set->contains(value)) {
            std::cout << "Set '" << name << "' contains " << value << "\n";
        } else {
            std::cout << "Set '" << name << "' does not contain " << value << "\n";
        }
    }

    void cmdSetErase(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: set-erase <set_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Set<int32_t>> set;

        if (it != structures_.end()) {
            set = std::static_pointer_cast<zeroipc::Set<int32_t>>(it->second);
        } else {
            set = std::make_shared<zeroipc::Set<int32_t>>(*memory_, name);
            structures_[name] = set;
        }

        if (set->erase(value)) {
            std::cout << "Erased " << value << " from set '" << name << "'\n";
        } else {
            std::cout << "Value " << value << " not found in set '" << name << "'\n";
        }
    }

    // Pool commands
    void cmdCreatePool(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-pool <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);

        auto pool = std::make_shared<zeroipc::Pool<int32_t>>(*memory_, name, capacity);
        structures_[name] = pool;
        std::cout << "Created pool<int32> '" << name << "' with capacity " << capacity << "\n";
    }

    // Channel commands
    void cmdCreateChannel(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open. Use 'create' or 'open' first.\n";
            return;
        }

        if (tokens.size() < 4) {
            std::cerr << "Usage: create-channel <name> <capacity> <elem_size>\n";
            return;
        }

        std::string name = tokens[1];
        size_t capacity = std::stoull(tokens[2]);

        auto ch = std::make_shared<zeroipc::Channel<int32_t>>(*memory_, name, capacity);
        structures_[name] = ch;
        std::cout << "Created channel<int32> '" << name << "' with capacity " << capacity << "\n";
    }

    void cmdChannelSend(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 3) {
            std::cerr << "Usage: channel-send <channel_name> <value>\n";
            return;
        }

        std::string name = tokens[1];
        int32_t value = std::stoi(tokens[2]);

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Channel<int32_t>> ch;

        if (it != structures_.end()) {
            ch = std::static_pointer_cast<zeroipc::Channel<int32_t>>(it->second);
        } else {
            ch = std::make_shared<zeroipc::Channel<int32_t>>(*memory_, name);
            structures_[name] = ch;
        }

        if (ch->send(value)) {
            std::cout << "Sent " << value << " to channel '" << name << "'\n";
        } else {
            std::cout << "Channel '" << name << "' is full or closed\n";
        }
    }

    void cmdChannelRecv(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: channel-recv <channel_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Channel<int32_t>> ch;

        if (it != structures_.end()) {
            ch = std::static_pointer_cast<zeroipc::Channel<int32_t>>(it->second);
        } else {
            ch = std::make_shared<zeroipc::Channel<int32_t>>(*memory_, name);
            structures_[name] = ch;
        }

        auto val = ch->recv();
        if (val) {
            std::cout << "Received: " << *val << "\n";
        } else {
            std::cout << "Channel '" << name << "' is empty or closed\n";
        }
    }

    void cmdChannelClose(const std::vector<std::string>& tokens) {
        if (!memory_) {
            std::cerr << "No shared memory currently open.\n";
            return;
        }

        if (tokens.size() < 2) {
            std::cerr << "Usage: channel-close <channel_name>\n";
            return;
        }

        std::string name = tokens[1];

        auto it = structures_.find(name);
        std::shared_ptr<zeroipc::Channel<int32_t>> ch;

        if (it != structures_.end()) {
            ch = std::static_pointer_cast<zeroipc::Channel<int32_t>>(it->second);
        } else {
            ch = std::make_shared<zeroipc::Channel<int32_t>>(*memory_, name);
            structures_[name] = ch;
        }

        ch->close();
        std::cout << "Closed channel '" << name << "'\n";
    }

    // Virtual Filesystem Navigation Commands

    void cmdLs(const std::vector<std::string>& tokens) {
        using namespace zeroipc::vfs;

        // Parse optional path argument
        std::string target = tokens.size() > 1 ? tokens[1] : "";
        Path path = target.empty() ? nav_context_.current_path :
                                     nav_context_.current_path.resolve(target);

        if (path.isRoot()) {
            // List shared memory segments
            auto segments = listSharedMemorySegments();
            std::cout << "\n=== Shared Memory Segments ===\n";
            std::cout << std::left << std::setw(30) << "Name"
                      << std::setw(15) << "Size" << "\n";
            std::cout << std::string(50, '-') << "\n";

            for (const auto& [name, size] : segments) {
                std::cout << std::setw(30) << name
                          << std::setw(15) << formatSize(size) << "\n";
            }
        } else if (path.depth() == 1) {
            // List structures in segment
            std::string segment_name = "/" + path[0];

            // Always use SharedMemoryInspector in read-only mode
            SharedMemoryInspector inspector(segment_name);
            if (inspector.open()) {
                inspector.printTable(true);  // verbose to show types
            }
        } else if (path.depth() == 2) {
            // Show structure contents
            std::string segment_name = "/" + path[0];
            std::string structure_name = path[1];

            SharedMemoryInspector inspector(segment_name);
            if (inspector.open()) {
                inspector.printStructureInfo(structure_name);
            }
        } else {
            std::cerr << "Invalid path depth\n";
        }
    }

    void cmdCd(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            std::cerr << "Usage: cd <path>\n";
            return;
        }

        std::string target = tokens[1];

        // Save old state in case we need to revert
        auto old_path = nav_context_.current_path;
        auto old_location = nav_context_.location_type;

        // Try to change directory
        if (!nav_context_.cd(target)) {
            std::cerr << "cd: invalid path (max depth is 2)\n";
            return;
        }

        // If we changed segments, update the open memory
        if (nav_context_.location_type == zeroipc::vfs::LocationType::SEGMENT ||
            nav_context_.location_type == zeroipc::vfs::LocationType::STRUCTURE) {

            std::string segment = "/" + nav_context_.segment_name;

            // If segment changed, open it
            if (current_shm_ != segment) {
                try {
                    memory_ = std::make_unique<zeroipc::Memory>(segment);
                    current_shm_ = segment;
                    structures_.clear();  // Clear old structures
                } catch (const std::exception& e) {
                    std::cerr << "Error opening segment '" << segment << "': " << e.what() << "\n";
                    // Revert navigation
                    nav_context_.current_path = old_path;
                    nav_context_.location_type = old_location;
                    return;
                }
            }
        } else if (nav_context_.location_type == zeroipc::vfs::LocationType::ROOT) {
            // Going back to root, close current segment
            memory_.reset();
            current_shm_.clear();
            structures_.clear();
        }
    }

    void cmdPwd(const std::vector<std::string>& tokens) {
        std::cout << nav_context_.pwd() << "\n";
    }
};

void printUsage(const char* program) {
    std::cout << "ZeroIPC Shared Memory Inspector v3.0\n";
    std::cout << "Enhanced with REPL mode and creation/manipulation support\n";
    std::cout << "\nUsage: " << program << " [OPTIONS] [<shm_name>]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -r, --repl         Start interactive REPL mode\n";
    std::cout << "  -s, --summary      Show summary information (default)\n";
    std::cout << "  -t, --table        Show table entries\n";
    std::cout << "  -v, --verbose      Verbose output (show structure types)\n";
    std::cout << "  -d, --dump <name>  Hex dump of named entry\n";
    std::cout << "  -i, --info <name>  Show structure information for named entry\n";
    std::cout << "  -l, --list         List all shared memory objects\n";
    std::cout << "  -a, --all          Show all information\n";
    std::cout << "  -h, --help         Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program << " -r                         # Start REPL\n";
    std::cout << "  " << program << " /my_shm                    # Show summary\n";
    std::cout << "  " << program << " -tv /my_shm                # Show table with types\n";
    std::cout << "  " << program << " -i my_semaphore /my_shm    # Info about semaphore\n";
    std::cout << "  " << program << " -a /my_shm                 # Show everything\n";
    std::cout << "  " << program << " -l                         # List all shared memory\n";
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
        {"repl", no_argument, 0, 'r'},
        {"summary", no_argument, 0, 's'},
        {"table", no_argument, 0, 't'},
        {"verbose", no_argument, 0, 'v'},
        {"dump", required_argument, 0, 'd'},
        {"info", required_argument, 0, 'i'},
        {"list", no_argument, 0, 'l'},
        {"all", no_argument, 0, 'a'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    bool repl_mode = false;
    bool show_summary = false;
    bool show_table = false;
    bool verbose = false;
    bool show_all = false;
    bool list_only = false;
    std::string dump_entry;
    std::string info_entry;

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "rstvd:i:lah", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'r': repl_mode = true; break;
            case 's': show_summary = true; break;
            case 't': show_table = true; break;
            case 'v': verbose = true; break;
            case 'd': dump_entry = optarg; break;
            case 'i': info_entry = optarg; break;
            case 'l': list_only = true; break;
            case 'a': show_all = true; break;
            case 'h': printUsage(argv[0]); return 0;
            default: printUsage(argv[0]); return 1;
        }
    }

    // REPL mode
    if (repl_mode) {
        ZeroIPCRepl repl;
        repl.run();
        return 0;
    }

    // List mode
    if (list_only) {
        SharedMemoryInspector inspector("");
        inspector.listSharedMemory();
        return 0;
    }

    // Inspection mode
    if (optind >= argc) {
        std::cerr << "Error: Missing shared memory name\n";
        printUsage(argv[0]);
        return 1;
    }

    std::string shm_name = argv[optind];
    SharedMemoryInspector inspector(shm_name);

    if (!inspector.open()) {
        return 1;
    }

    if (!show_summary && !show_table && dump_entry.empty() &&
        info_entry.empty() && !show_all) {
        show_summary = true;
    }

    if (show_all) {
        show_summary = true;
        show_table = true;
        verbose = true;
    }

    if (show_summary) inspector.printSummary();
    if (show_table) inspector.printTable(verbose);
    if (!dump_entry.empty()) inspector.printHexDump(dump_entry);
    if (!info_entry.empty()) inspector.printStructureInfo(info_entry);

    return 0;
}
