// ZeroIPC Shared Memory Inspector CLI Tool
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sstream>

struct TableHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t reserved;      // Padding/reserved for future use
    uint64_t memory_size;   // Total size of the shared memory segment
    uint64_t next_offset;   // Next allocation offset
};

struct TableEntry {
    char name[32];
    uint64_t offset;
    uint64_t size;
};

class SharedMemoryInspector {
private:
    std::string shm_name_;
    void* base_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
    TableHeader* header_ = nullptr;
    TableEntry* entries_ = nullptr;

    static constexpr uint32_t MAGIC = 0x5A49504D; // 'ZIPM'

public:
    SharedMemoryInspector(const std::string& name) : shm_name_(name) {
        if (shm_name_[0] != '/') {
            shm_name_ = "/" + shm_name_;
        }
    }

    ~SharedMemoryInspector() {
        cleanup();
    }

    bool open() {
        // Open shared memory
        fd_ = shm_open(shm_name_.c_str(), O_RDONLY, 0);
        if (fd_ == -1) {
            std::cerr << "Error: Failed to open shared memory '" << shm_name_ 
                      << "': " << strerror(errno) << "\n";
            return false;
        }

        // Get size
        struct stat st;
        if (fstat(fd_, &st) == -1) {
            std::cerr << "Error: Failed to get size of shared memory: " 
                      << strerror(errno) << "\n";
            cleanup();
            return false;
        }
        size_ = st.st_size;

        // Map memory
        base_ = mmap(nullptr, size_, PROT_READ, MAP_SHARED, fd_, 0);
        if (base_ == MAP_FAILED) {
            std::cerr << "Error: Failed to map shared memory: " 
                      << strerror(errno) << "\n";
            cleanup();
            return false;
        }

        // Validate header
        header_ = static_cast<TableHeader*>(base_);
        if (header_->magic != MAGIC) {
            std::cerr << "Error: Invalid magic number. Expected 0x" 
                      << std::hex << MAGIC << ", got 0x" << header_->magic << "\n";
            cleanup();
            return false;
        }

        // Entries follow the header
        entries_ = reinterpret_cast<TableEntry*>(
            static_cast<char*>(base_) + sizeof(TableHeader));

        return true;
    }

    void printSummary() {
        std::cout << "\n=== Shared Memory Summary ===\n";
        std::cout << "Name: " << shm_name_ << "\n";
        std::cout << "Total Size: " << formatSize(size_) << " (" << size_ << " bytes)\n";
        std::cout << "Format Version: " << header_->version << "\n";
        std::cout << "Active Entries: " << header_->entry_count << "\n";
        std::cout << "Memory Size: " << formatSize(header_->memory_size) << "\n";
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

        // Print header
        std::cout << std::left << std::setw(4) << "#" 
                  << std::setw(32) << "Name"
                  << std::setw(12) << "Offset"
                  << std::setw(12) << "Size";
        
        if (verbose) {
            std::cout << std::setw(10) << "Type";
        }
        std::cout << "\n";
        
        std::cout << std::string(70, '-') << "\n";

        // Print entries
        for (uint32_t i = 0; i < header_->entry_count; i++) {
            std::cout << std::left << std::setw(4) << i
                      << std::setw(32) << entries_[i].name
                      << "0x" << std::hex << std::setw(10) << entries_[i].offset
                      << std::dec << std::setw(12) << formatSize(entries_[i].size);
            
            if (verbose) {
                std::cout << std::setw(10) << guessStructureType(entries_[i]);
            }
            std::cout << "\n";
        }
    }

    void printHexDump(const std::string& entry_name, size_t max_bytes = 256) {
        // Find entry
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
            // Print offset
            std::cout << std::hex << std::setfill('0') << std::setw(8) << i << "  ";

            // Print hex bytes
            for (size_t j = 0; j < 16; j++) {
                if (i + j < bytes_to_dump) {
                    std::cout << std::setw(2) << static_cast<int>(data[i + j]) << " ";
                } else {
                    std::cout << "   ";
                }
                if (j == 7) std::cout << " ";
            }

            std::cout << " |";

            // Print ASCII
            for (size_t j = 0; j < 16 && i + j < bytes_to_dump; j++) {
                char c = static_cast<char>(data[i + j]);
                std::cout << (isprint(c) ? c : '.');
            }

            std::cout << "|\n";
        }

        if (bytes_to_dump < entry->size) {
            std::cout << "... (" << (entry->size - bytes_to_dump) 
                      << " more bytes)\n";
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
        
        std::string type = guessStructureType(*entry);
        std::cout << "Probable Type: " << type << "\n";

        // Read header based on guessed type
        const char* data = static_cast<const char*>(base_) + entry->offset;
        
        if (type == "Array") {
            struct ArrayHeader {
                uint64_t capacity;
            };
            const ArrayHeader* hdr = reinterpret_cast<const ArrayHeader*>(data);
            std::cout << "Capacity: " << hdr->capacity << " elements\n";
            size_t elem_size = (entry->size - sizeof(ArrayHeader)) / hdr->capacity;
            std::cout << "Element Size: " << elem_size << " bytes\n";
        } 
        else if (type == "Queue") {
            struct QueueHeader {
                uint64_t head;
                uint64_t tail;
                uint64_t capacity;
            };
            const QueueHeader* hdr = reinterpret_cast<const QueueHeader*>(data);
            std::cout << "Head: " << hdr->head << "\n";
            std::cout << "Tail: " << hdr->tail << "\n";
            std::cout << "Capacity: " << hdr->capacity << " elements\n";
            
            uint64_t count = (hdr->tail >= hdr->head) 
                ? (hdr->tail - hdr->head)
                : (hdr->capacity - hdr->head + hdr->tail);
            std::cout << "Current Items: " << count << "\n";
        }
        else if (type == "Stack") {
            struct StackHeader {
                uint64_t top;
                uint64_t capacity;
            };
            const StackHeader* hdr = reinterpret_cast<const StackHeader*>(data);
            std::cout << "Top: " << hdr->top << "\n";
            std::cout << "Capacity: " << hdr->capacity << " elements\n";
            std::cout << "Current Items: " << hdr->top << "\n";
        }
    }

    void listSharedMemory() {
        std::cout << "\n=== Available Shared Memory Objects ===\n";
        system("ls -la /dev/shm/ | grep -v '^total' | grep -v '^d' | awk '{print $9, $5}'");
    }

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

    std::string guessStructureType(const TableEntry& entry) {
        // Simple heuristic based on size patterns
        const char* data = static_cast<const char*>(base_) + entry.offset;
        uint64_t first_value = *reinterpret_cast<const uint64_t*>(data);
        
        // Check for array (single capacity value)
        if (entry.size > sizeof(uint64_t)) {
            uint64_t potential_capacity = first_value;
            size_t expected_array_size = sizeof(uint64_t) + potential_capacity * 4; // assume 4-byte elements
            if (entry.size >= expected_array_size && entry.size <= expected_array_size * 16) {
                return "Array";
            }
        }
        
        // Check for queue (three uint64_t values)
        if (entry.size > 3 * sizeof(uint64_t)) {
            return "Queue";
        }
        
        // Check for stack (two uint64_t values)
        if (entry.size > 2 * sizeof(uint64_t)) {
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

void printUsage(const char* program) {
    std::cout << "ZeroIPC Shared Memory Inspector\n";
    std::cout << "\nUsage: " << program << " [OPTIONS] <shm_name>\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -s, --summary      Show summary information (default)\n";
    std::cout << "  -t, --table        Show table entries\n";
    std::cout << "  -v, --verbose      Verbose output (guess structure types)\n";
    std::cout << "  -d, --dump <name>  Hex dump of named entry\n";
    std::cout << "  -i, --info <name>  Show structure information for named entry\n";
    std::cout << "  -l, --list         List all shared memory objects\n";
    std::cout << "  -a, --all          Show all information\n";
    std::cout << "  -h, --help         Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program << " /my_shm                    # Show summary\n";
    std::cout << "  " << program << " -t /my_shm                 # Show table entries\n";
    std::cout << "  " << program << " -d sensor_data /my_shm     # Hex dump of 'sensor_data'\n";
    std::cout << "  " << program << " -i event_queue /my_shm     # Info about 'event_queue'\n";
    std::cout << "  " << program << " -l                         # List all shared memory\n";
}

int main(int argc, char* argv[]) {
    static struct option long_options[] = {
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

    bool show_summary = false;
    bool show_table = false;
    bool verbose = false;
    bool show_all = false;
    bool list_only = false;
    std::string dump_entry;
    std::string info_entry;

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "stvd:i:lah", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                show_summary = true;
                break;
            case 't':
                show_table = true;
                break;
            case 'v':
                verbose = true;
                break;
            case 'd':
                dump_entry = optarg;
                break;
            case 'i':
                info_entry = optarg;
                break;
            case 'l':
                list_only = true;
                break;
            case 'a':
                show_all = true;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }

    // Special case: list shared memory
    if (list_only) {
        SharedMemoryInspector inspector("");
        inspector.listSharedMemory();
        return 0;
    }

    // Need shared memory name
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

    // Default to summary if nothing specified
    if (!show_summary && !show_table && dump_entry.empty() && 
        info_entry.empty() && !show_all) {
        show_summary = true;
    }

    if (show_all) {
        show_summary = true;
        show_table = true;
        verbose = true;
    }

    if (show_summary) {
        inspector.printSummary();
    }

    if (show_table) {
        inspector.printTable(verbose);
    }

    if (!dump_entry.empty()) {
        inspector.printHexDump(dump_entry);
    }

    if (!info_entry.empty()) {
        inspector.printStructureInfo(info_entry);
    }

    return 0;
}