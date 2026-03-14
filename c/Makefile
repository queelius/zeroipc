CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -Iinclude
LDFLAGS = -lrt -lpthread -lm

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests
EXAMPLE_DIR = examples

SOURCES = $(SRC_DIR)/memory.c $(SRC_DIR)/table.c $(SRC_DIR)/array.c \
          $(SRC_DIR)/queue.c $(SRC_DIR)/stack.c $(SRC_DIR)/error.c \
          $(SRC_DIR)/barrier.c $(SRC_DIR)/latch.c

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

LIB = libzeroipc.a
FFI_SO = libzeroipc_ffi.so

TEST_BIN = $(BIN_DIR)/test_zeroipc
TEST_QUEUE_BIN = $(BIN_DIR)/test_queue_stack

.PHONY: all clean test examples shared

all: dirs $(LIB) $(FFI_SO)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

$(LIB): $(OBJECTS)
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(FFI_SO): $(SRC_DIR)/ffi.c
	$(CC) $(CFLAGS) -fPIC -shared $< -o $@

shared: dirs $(FFI_SO)

test: dirs $(LIB)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_basic.c $(LIB) $(LDFLAGS) -o $(TEST_BIN)
	./$(TEST_BIN)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_queue_stack.c $(LIB) $(LDFLAGS) -o $(TEST_QUEUE_BIN)
	./$(TEST_QUEUE_BIN)

examples: dirs $(LIB)
	$(CC) $(CFLAGS) $(EXAMPLE_DIR)/basic.c $(LIB) $(LDFLAGS) -o $(BIN_DIR)/example

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB) $(FFI_SO)
	rm -f /dev/shm/test_* /dev/shm/example_* /dev/shm/zeroipc_*
