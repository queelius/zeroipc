// Virtual Filesystem for ZeroIPC - Navigation and Inspection
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <dirent.h>
#include <sys/stat.h>

namespace zeroipc {
namespace vfs {

// Location type in the virtual filesystem
enum class LocationType {
    ROOT,       // / - list all shared memory segments
    SEGMENT,    // /segment_name - list structures
    STRUCTURE   // /segment_name/structure_name - show contents
};

// Represents a path in the virtual filesystem
class Path {
public:
    Path() : components_{} {}

    explicit Path(const std::string& path_str) {
        parse(path_str);
    }

    // Parse a path string into components
    void parse(const std::string& path_str) {
        components_.clear();

        if (path_str.empty() || path_str == "/") {
            return; // Root path
        }

        std::string path = path_str;
        // Remove leading slash
        if (path[0] == '/') {
            path = path.substr(1);
        }
        // Remove trailing slash
        if (!path.empty() && path.back() == '/') {
            path.pop_back();
        }

        // Split by '/'
        std::istringstream iss(path);
        std::string component;
        while (std::getline(iss, component, '/')) {
            if (!component.empty() && component != ".") {
                if (component == "..") {
                    if (!components_.empty()) {
                        components_.pop_back();
                    }
                } else {
                    components_.push_back(component);
                }
            }
        }
    }

    // Get the full path as string
    std::string toString() const {
        if (components_.empty()) {
            return "/";
        }
        std::string result = "/";
        for (size_t i = 0; i < components_.size(); ++i) {
            result += components_[i];
            if (i < components_.size() - 1) {
                result += "/";
            }
        }
        return result;
    }

    // Get number of components
    size_t depth() const { return components_.size(); }

    // Get component at index
    const std::string& operator[](size_t index) const {
        return components_[index];
    }

    // Get last component (name)
    std::string name() const {
        return components_.empty() ? "" : components_.back();
    }

    // Get parent path
    Path parent() const {
        Path p;
        if (!components_.empty()) {
            p.components_ = std::vector<std::string>(
                components_.begin(), components_.end() - 1);
        }
        return p;
    }

    // Check if this is root
    bool isRoot() const { return components_.empty(); }

    // Resolve a relative path from this path
    Path resolve(const std::string& relative) const {
        if (relative.empty()) {
            return *this;
        }

        // Absolute path
        if (relative[0] == '/') {
            return Path(relative);
        }

        // Relative path - append to current
        Path result;
        result.components_ = components_;

        Path rel_path(relative);
        for (const auto& comp : rel_path.components_) {
            if (comp == "..") {
                if (!result.components_.empty()) {
                    result.components_.pop_back();
                }
            } else {
                result.components_.push_back(comp);
            }
        }

        return result;
    }

    const std::vector<std::string>& components() const {
        return components_;
    }

private:
    std::vector<std::string> components_;
};

// Navigation context - tracks current location
struct NavigationContext {
    Path current_path;
    LocationType location_type = LocationType::ROOT;
    std::string segment_name;    // Current segment if in SEGMENT or STRUCTURE
    std::string structure_name;  // Current structure if in STRUCTURE

    NavigationContext() : current_path("/") {}

    // Update context based on path
    void update() {
        size_t depth = current_path.depth();

        if (depth == 0) {
            location_type = LocationType::ROOT;
            segment_name.clear();
            structure_name.clear();
        } else if (depth == 1) {
            location_type = LocationType::SEGMENT;
            segment_name = current_path[0];
            structure_name.clear();
        } else if (depth == 2) {
            location_type = LocationType::STRUCTURE;
            segment_name = current_path[0];
            structure_name = current_path[1];
        } else {
            // Too deep - invalid for our model
            // Could throw or truncate
        }
    }

    // Change to a new path
    bool cd(const std::string& target) {
        Path new_path = current_path.resolve(target);

        // Validate depth
        if (new_path.depth() > 2) {
            return false; // Too deep
        }

        current_path = new_path;
        update();
        return true;
    }

    // Get current path as string
    std::string pwd() const {
        return current_path.toString();
    }

    // Get prompt string
    std::string prompt() const {
        std::string path = pwd();
        if (path == "/") {
            return "zeroipc> ";
        }
        return path + "> ";
    }
};

// List shared memory segments in /dev/shm
inline std::vector<std::pair<std::string, size_t>> listSharedMemorySegments() {
    std::vector<std::pair<std::string, size_t>> segments;

    DIR* dir = opendir("/dev/shm");
    if (!dir) {
        return segments;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) {
            std::string name = entry->d_name;
            if (name != "." && name != "..") {
                // Get file size
                std::string path = std::string("/dev/shm/") + name;
                struct stat st;
                if (stat(path.c_str(), &st) == 0) {
                    // Add leading slash for shared memory name
                    segments.push_back({"/" + name, st.st_size});
                }
            }
        }
    }

    closedir(dir);
    return segments;
}

// Format size in human-readable form
inline std::string formatSize(size_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double size = bytes;

    while (size >= 1024.0 && unit_index < 3) {
        size /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << size << " " << units[unit_index];
    return oss.str();
}

} // namespace vfs
} // namespace zeroipc
