CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Iinclude
LDFLAGS = -lrt -lpthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TEST_DIR = tests
EXAMPLE_DIR = examples

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

LIB = libzeroipc.a
TEST_BIN = $(BIN_DIR)/test_zeroipc
EXAMPLE_BIN = $(BIN_DIR)/example

.PHONY: all clean test examples

all: dirs $(LIB)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

$(LIB): $(OBJECTS)
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

test: dirs $(LIB)
	$(CC) $(CFLAGS) $(TEST_DIR)/test_basic.c $(LIB) $(LDFLAGS) -o $(TEST_BIN)
	./$(TEST_BIN)

examples: dirs $(LIB)
	$(CC) $(CFLAGS) $(EXAMPLE_DIR)/basic.c $(LIB) $(LDFLAGS) -o $(EXAMPLE_BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(LIB)
	rm -f /dev/shm/test_* /dev/shm/example_*