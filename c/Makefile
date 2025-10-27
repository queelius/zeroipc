CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Iinclude
LDFLAGS = -lrt -lpthread -lm

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests
EXAMPLE_DIR = examples

# Core sources (new elegant API)
CORE_SOURCES = $(SRC_DIR)/core.c

# Legacy sources (existing API)
LEGACY_SOURCES = $(SRC_DIR)/memory.c $(SRC_DIR)/table.c $(SRC_DIR)/array.c \
                 $(SRC_DIR)/queue.c $(SRC_DIR)/stack.c $(SRC_DIR)/error.c \
                 $(SRC_DIR)/barrier.c $(SRC_DIR)/latch.c

# All sources
SOURCES = $(CORE_SOURCES) $(LEGACY_SOURCES)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# Libraries
LIB = libzeroipc.a
LIB_CORE = libzeroipc_core.a

# Test binaries
TEST_BIN = $(BIN_DIR)/test_zeroipc
TEST_CORE_BIN = $(BIN_DIR)/test_core

# Example binaries
EXAMPLE_BIN = $(BIN_DIR)/example
INTEROP_BIN = $(BIN_DIR)/interop

.PHONY: all clean test test-core examples interop

all: dirs $(LIB) $(LIB_CORE)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Build both libraries
$(LIB): $(OBJECTS)
	ar rcs $@ $^

$(LIB_CORE): $(OBJ_DIR)/core.o
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test targets
test: dirs $(LIB)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_basic.c $(LIB) $(LDFLAGS) -o $(TEST_BIN)
	./$(TEST_BIN)

test-core: dirs $(LIB_CORE)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_core.c $(LIB_CORE) $(LDFLAGS) -o $(TEST_CORE_BIN)
	./$(TEST_CORE_BIN)

# Example targets
examples: dirs $(LIB) $(LIB_CORE)
	$(CC) $(CFLAGS) $(EXAMPLE_DIR)/basic.c $(LIB) $(LDFLAGS) -o $(EXAMPLE_BIN)
	$(CC) $(CFLAGS) $(EXAMPLE_DIR)/interop.c $(LIB_CORE) $(LDFLAGS) -o $(INTEROP_BIN)

interop: dirs $(LIB_CORE)
	$(CC) $(CFLAGS) $(EXAMPLE_DIR)/interop.c $(LIB_CORE) $(LDFLAGS) -o $(INTEROP_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB) $(LIB_CORE)
	rm -f /dev/shm/test_* /dev/shm/example_* /dev/shm/zeroipc_*