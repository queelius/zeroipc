# POSIX Shared Memory Library Makefile
# Author: Alex Towell

.PHONY: all build test clean install docs package help

# Variables
BUILD_DIR ?= build
INSTALL_PREFIX ?= /usr/local
CMAKE_BUILD_TYPE ?= Release
CORES ?= $(shell nproc 2>/dev/null || echo 4)

# Colors for output
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[1;33m
NC := \033[0m # No Color

# Default target
all: build

# Help target
help:
	@echo "$(GREEN)POSIX Shared Memory Library - Build System$(NC)"
	@echo ""
	@echo "$(YELLOW)Available targets:$(NC)"
	@echo "  make build          - Build the library and tests"
	@echo "  make test           - Run all tests"
	@echo "  make install        - Install the library to system"
	@echo "  make docs           - Generate Doxygen documentation"
	@echo "  make clean          - Clean build artifacts"
	@echo "  make format         - Format code with clang-format"
	@echo "  make package        - Create distribution packages"
	@echo "  make coverage       - Run tests with coverage analysis"
	@echo "  make benchmark      - Run performance benchmarks"
	@echo "  make check          - Run static analysis"
	@echo ""
	@echo "$(YELLOW)Variables:$(NC)"
	@echo "  BUILD_DIR=build     - Build directory"
	@echo "  INSTALL_PREFIX=/usr/local - Installation prefix"
	@echo "  CMAKE_BUILD_TYPE=Release  - Build type (Debug/Release)"
	@echo ""
	@echo "$(YELLOW)Examples:$(NC)"
	@echo "  make BUILD_DIR=debug CMAKE_BUILD_TYPE=Debug build"
	@echo "  make INSTALL_PREFIX=~/.local install"

# Configure and build
build: $(BUILD_DIR)/Makefile
	@echo "$(GREEN)Building posix_shm...$(NC)"
	@cmake --build $(BUILD_DIR) -j$(CORES)
	@echo "$(GREEN)Build complete!$(NC)"

# Configure step
$(BUILD_DIR)/Makefile:
	@echo "$(GREEN)Configuring build...$(NC)"
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
		-DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX) \
		-DBUILD_TESTS=ON \
		-DBUILD_EXAMPLES=ON

# Run tests
test: build
	@echo "$(GREEN)Running tests...$(NC)"
	@cd $(BUILD_DIR) && ctest --output-on-failure -j$(CORES)
	@echo "$(GREEN)All tests passed!$(NC)"

# Run specific test
test-%: build
	@echo "$(GREEN)Running test: $*$(NC)"
	@cd $(BUILD_DIR) && ctest -R "$*" --output-on-failure

# Install library
install: build
	@echo "$(GREEN)Installing to $(INSTALL_PREFIX)...$(NC)"
	@cmake --install $(BUILD_DIR)
	@echo "$(GREEN)Installation complete!$(NC)"

# Generate documentation
docs:
	@echo "$(GREEN)Generating documentation...$(NC)"
	@doxygen Doxyfile
	@echo "$(GREEN)Documentation generated in docs/html/$(NC)"
	@echo "Open docs/html/index.html in your browser"

# Clean build artifacts
clean:
	@echo "$(YELLOW)Cleaning build artifacts...$(NC)"
	@rm -rf $(BUILD_DIR)
	@rm -rf docs/html
	@rm -rf /tmp/posix_shm_* /tmp/test_*
	@find . -name "*.o" -delete
	@find . -name "*.so" -delete
	@find . -name "*.a" -delete
	@echo "$(GREEN)Clean complete!$(NC)"

# Deep clean (includes generated files)
distclean: clean
	@echo "$(YELLOW)Deep cleaning...$(NC)"
	@rm -rf .cache
	@rm -rf compile_commands.json
	@rm -rf .clangd
	@rm -f debug_atomic debug_atomic_test.cpp
	@echo "$(GREEN)Deep clean complete!$(NC)"

# Format code
format:
	@echo "$(GREEN)Formatting code...$(NC)"
	@find include tests examples -name "*.h" -o -name "*.cpp" | xargs clang-format -i --style=file
	@echo "$(GREEN)Code formatted!$(NC)"

# Check formatting
check-format:
	@echo "$(GREEN)Checking code format...$(NC)"
	@find include tests examples -name "*.h" -o -name "*.cpp" | xargs clang-format --dry-run --Werror --style=file

# Static analysis
check: build
	@echo "$(GREEN)Running static analysis...$(NC)"
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@clang-tidy -p $(BUILD_DIR) include/*.h tests/*.cpp examples/*.cpp 2>/dev/null || true
	@echo "$(GREEN)Static analysis complete!$(NC)"

# Run benchmarks
benchmark: build
	@echo "$(GREEN)Running benchmarks...$(NC)"
	@$(BUILD_DIR)/examples/benchmark_reads
	@echo "$(GREEN)Benchmarks complete!$(NC)"

# Coverage analysis
coverage:
	@echo "$(GREEN)Building with coverage...$(NC)"
	@mkdir -p build-coverage
	@cd build-coverage && cmake .. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
		-DBUILD_TESTS=ON
	@cmake --build build-coverage -j$(CORES)
	@cd build-coverage && ctest
	@lcov --capture --directory build-coverage --output-file coverage.info
	@lcov --remove coverage.info '/usr/*' --output-file coverage.info
	@lcov --list coverage.info
	@echo "$(GREEN)Coverage report generated!$(NC)"

# Package creation
package: package-tar package-conan

# Create tar.gz archive
package-tar:
	@echo "$(GREEN)Creating tar.gz package...$(NC)"
	@git archive --format=tar.gz --prefix=posix_shm-1.0.0/ HEAD > posix_shm-1.0.0.tar.gz
	@echo "$(GREEN)Package created: posix_shm-1.0.0.tar.gz$(NC)"

# Create Conan package
package-conan:
	@echo "$(GREEN)Creating Conan package...$(NC)"
	@conan create . --build=missing -tf=None || echo "$(YELLOW)Conan not installed or package creation failed$(NC)"

# Create vcpkg package (local overlay)
package-vcpkg:
	@echo "$(GREEN)Creating vcpkg overlay...$(NC)"
	@mkdir -p vcpkg-overlay/posix-shm
	@cp vcpkg/* vcpkg-overlay/posix-shm/
	@echo "$(GREEN)vcpkg overlay created in vcpkg-overlay/$(NC)"

# Development build (with debug symbols and sanitizers)
dev: 
	@mkdir -p build-dev
	@cd build-dev && cmake .. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined -g" \
		-DBUILD_TESTS=ON \
		-DBUILD_EXAMPLES=ON
	@cmake --build build-dev -j$(CORES)
	@echo "$(GREEN)Development build complete (with sanitizers)!$(NC)"

# Run memory check
memcheck: build
	@echo "$(GREEN)Running memory check...$(NC)"
	@cd $(BUILD_DIR) && valgrind --leak-check=full --show-leak-kinds=all ./tests/posix_shm_tests

# Quick test - build and run a simple test
quick-test:
	@echo "$(GREEN)Running quick test...$(NC)"
	@echo '#include <posix_shm.h>' > /tmp/test_quick.cpp
	@echo '#include <shm_array.h>' >> /tmp/test_quick.cpp
	@echo '#include <iostream>' >> /tmp/test_quick.cpp
	@echo 'int main() {' >> /tmp/test_quick.cpp
	@echo '    posix_shm shm("/quick", 1024);' >> /tmp/test_quick.cpp
	@echo '    shm_array<int> arr(shm, "test", 10);' >> /tmp/test_quick.cpp
	@echo '    arr[0] = 42;' >> /tmp/test_quick.cpp
	@echo '    std::cout << "Quick test: " << arr[0] << std::endl;' >> /tmp/test_quick.cpp
	@echo '    shm.unlink();' >> /tmp/test_quick.cpp
	@echo '    return 0;' >> /tmp/test_quick.cpp
	@echo '}' >> /tmp/test_quick.cpp
	@g++ -std=c++23 -I./include /tmp/test_quick.cpp -o /tmp/test_quick -lrt
	@/tmp/test_quick
	@echo "$(GREEN)Quick test passed!$(NC)"

# Show shared memory segments
show-shm:
	@echo "$(YELLOW)Current shared memory segments:$(NC)"
	@ls -la /dev/shm/ | grep -v "^total" || echo "No segments found"

# Clean shared memory segments (be careful!)
clean-shm:
	@echo "$(YELLOW)Cleaning test shared memory segments...$(NC)"
	@rm -f /dev/shm/test_* /dev/shm/quick /dev/shm/debug_* 2>/dev/null || true
	@echo "$(GREEN)Shared memory cleaned!$(NC)"

# Full cleanup (build + shared memory)
clean-all: clean clean-shm
	@echo "$(GREEN)Complete cleanup done!$(NC)"

# CI/CD targets
ci: check-format build test docs
	@echo "$(GREEN)CI checks passed!$(NC)"

# Print configuration
info:
	@echo "$(GREEN)Build Configuration:$(NC)"
	@echo "  Build directory: $(BUILD_DIR)"
	@echo "  Install prefix: $(INSTALL_PREFIX)"
	@echo "  Build type: $(CMAKE_BUILD_TYPE)"
	@echo "  Parallel jobs: $(CORES)"
	@echo ""
	@echo "$(GREEN)System Information:$(NC)"
	@echo "  OS: $$(uname -s)"
	@echo "  Architecture: $$(uname -m)"
	@echo "  Compiler: $$(g++ --version | head -n1)"
	@echo "  CMake: $$(cmake --version | head -n1)"

.PHONY: dev memcheck quick-test show-shm clean-shm ci info package-tar package-conan package-vcpkg