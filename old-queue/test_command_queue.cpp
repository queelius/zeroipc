#include "command_queue.hpp"
#include <iostream>
#include <cassert>
#include <string>

struct Command {
    int id;
    std::string name;

    bool operator==(const Command& other) const {
        return id == other.id && name == other.name;
    }
};

void test_push_and_pop() {
    CommandQueue<Command> queue(3, "test_queue");

    assert(queue.isEmpty());
    assert(!queue.isFull());

    Command cmd1 = {1, "Command1"};
    Command cmd2 = {2, "Command2"};
    Command cmd3 = {3, "Command3"};

    assert(queue.push(cmd1));
    assert(queue.push(cmd2));
    assert(queue.push(cmd3));

    assert(!queue.isEmpty());
    assert(queue.isFull());

    auto popped_cmd1 = queue.pop();
    assert(popped_cmd1.has_value() && popped_cmd1.value() == cmd1);

    auto popped_cmd2 = queue.pop();
    assert(popped_cmd2.has_value() && popped_cmd2.value() == cmd2);

    auto popped_cmd3 = queue.pop();
    assert(popped_cmd3.has_value() && popped_cmd3.value() == cmd3);

    assert(queue.isEmpty());
    assert(!queue.isFull());
}

void test_overflow() {
    CommandQueue<Command> queue(2, "test_queue");

    Command cmd1 = {1, "Command1"};
    Command cmd2 = {2, "Command2"};
    Command cmd3 = {3, "Command3"};

    assert(queue.push(cmd1));
    assert(queue.push(cmd2));
    assert(!queue.push(cmd3)); // Should fail since the queue is full

    assert(queue.isFull());

    auto popped_cmd1 = queue.pop();
    assert(popped_cmd1.has_value() && popped_cmd1.value() == cmd1);

    assert(queue.push(cmd3)); // Should succeed now

    auto popped_cmd2 = queue.pop();
    assert(popped_cmd2.has_value() && popped_cmd2.value() == cmd2);

    auto popped_cmd3 = queue.pop();
    assert(popped_cmd3.has_value() && popped_cmd3.value() == cmd3);

    assert(queue.isEmpty());
}

void test_clear() {
    CommandQueue<Command> queue(2, "test_queue");

    Command cmd1 = {1, "Command1"};
    Command cmd2 = {2, "Command2"};

    assert(queue.push(cmd1));
    assert(queue.push(cmd2));

    queue.clear();

    assert(queue.isEmpty());
    assert(!queue.pop().has_value());
    assert(queue.push(cmd1));
}

void test_circular_behavior() {
    CommandQueue<Command> queue(3, "test_queue");

    Command cmd1 = {1, "Command1"};
    Command cmd2 = {2, "Command2"};
    Command cmd3 = {3, "Command3"};
    Command cmd4 = {4, "Command4"};

    assert(queue.push(cmd1));
    assert(queue.push(cmd2));
    assert(queue.push(cmd3));

    assert(queue.pop().value() == cmd1);
    assert(queue.pop().value() == cmd2);

    assert(queue.push(cmd4));

    assert(queue.pop().value() == cmd3);
    assert(queue.pop().value() == cmd4);
}

int main() {
    test_push_and_pop();
    test_overflow();
    test_clear();
    test_circular_behavior();

    std::cout << "All tests passed!" << std::endl;
    return 0;
}
