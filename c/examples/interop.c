/**
 * Cross-Language Interoperability Example
 *
 * Demonstrates C, C++, and Python all working with the same shared memory
 * structures simultaneously.
 */

#include "zeroipc_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

/* Sensor data structure - shared between all languages */
typedef struct {
    float temperature;
    float humidity;
    uint32_t timestamp;
} sensor_data_t;

/* Event structure for the event queue */
typedef struct {
    uint32_t event_id;
    uint32_t source_pid;
    uint64_t timestamp;
    char message[48];
} event_t;

/* Create and populate test structures */
void create_structures(void) {
    printf("=== C Producer: Creating shared structures ===\n\n");

    /* Open shared memory */
    zipc_shm_t shm = zipc_open("/zeroipc_interop", 10 * 1024 * 1024, 256);
    if (!shm) {
        fprintf(stderr, "Failed to create shared memory\n");
        exit(1);
    }

    printf("Created shared memory: /zeroipc_interop (10MB, 256 entries)\n");

    /* Create sensor data array */
    zipc_view_t sensors = zipc_create(shm, "sensor_array", ZIPC_TYPE_ARRAY,
                                      sizeof(sensor_data_t), 100);
    if (!sensors) {
        fprintf(stderr, "Failed to create sensor array\n");
        exit(1);
    }
    printf("Created array: 'sensor_array' (100 sensors)\n");

    /* Populate sensor data */
    printf("Populating sensor data...\n");
    for (size_t i = 0; i < 100; i++) {
        sensor_data_t data = {
            .temperature = 20.0f + (float)(i % 10),
            .humidity = 40.0f + (float)(i % 20),
            .timestamp = (uint32_t)time(NULL) + i
        };
        zipc_array_set(sensors, i, &data);
    }

    /* Create event queue */
    zipc_view_t events = zipc_create(shm, "event_queue", ZIPC_TYPE_QUEUE,
                                     sizeof(event_t), 50);
    if (!events) {
        fprintf(stderr, "Failed to create event queue\n");
        exit(1);
    }
    printf("Created queue: 'event_queue' (capacity 50)\n");

    /* Push some events */
    printf("Adding initial events...\n");
    for (int i = 0; i < 5; i++) {
        event_t evt = {
            .event_id = 1000 + i,
            .source_pid = getpid(),
            .timestamp = (uint64_t)time(NULL) * 1000 + i
        };
        snprintf(evt.message, sizeof(evt.message),
                "C Event %d from PID %d", i, getpid());

        if (zipc_queue_push(events, &evt) != ZIPC_OK) {
            fprintf(stderr, "Failed to push event\n");
        }
    }

    /* Create processing stack */
    zipc_view_t stack = zipc_create(shm, "task_stack", ZIPC_TYPE_STACK,
                                    sizeof(uint32_t), 20);
    if (!stack) {
        fprintf(stderr, "Failed to create task stack\n");
        exit(1);
    }
    printf("Created stack: 'task_stack' (capacity 20)\n");

    /* Push some task IDs */
    printf("Adding task IDs...\n");
    for (uint32_t i = 100; i < 105; i++) {
        zipc_stack_push(stack, &i);
    }

    /* Create statistics array */
    zipc_view_t stats = zipc_create(shm, "statistics", ZIPC_TYPE_ARRAY,
                                    sizeof(double), 10);
    if (!stats) {
        fprintf(stderr, "Failed to create statistics array\n");
        exit(1);
    }
    printf("Created array: 'statistics' (10 doubles)\n");

    /* Initialize statistics */
    for (size_t i = 0; i < 10; i++) {
        double value = sqrt((double)(i + 1)) * 3.14159;
        zipc_array_set(stats, i, &value);
    }

    printf("\n");
    printf("Summary of created structures:\n");
    printf("  Total structures: %zu\n", zipc_count(shm));
    printf("  Memory used: ~%zu KB\n",
           (100 * sizeof(sensor_data_t) +
            50 * sizeof(event_t) +
            20 * sizeof(uint32_t) +
            10 * sizeof(double)) / 1024);

    /* Clean up */
    zipc_view_close(sensors);
    zipc_view_close(events);
    zipc_view_close(stack);
    zipc_view_close(stats);
    zipc_close(shm);

    printf("\nC Producer finished. Structures ready for other languages.\n");
}

/* Read and verify structures created by other languages */
void read_structures(void) {
    printf("=== C Consumer: Reading shared structures ===\n\n");

    /* Open existing shared memory */
    zipc_shm_t shm = zipc_open("/zeroipc_interop", 0, 0);
    if (!shm) {
        fprintf(stderr, "Failed to open shared memory\n");
        exit(1);
    }

    printf("Opened shared memory: /zeroipc_interop\n");
    printf("Found %zu structures\n\n", zipc_count(shm));

    /* Read sensor array */
    zipc_view_t sensors = zipc_get(shm, "sensor_array", ZIPC_TYPE_ARRAY,
                                   sizeof(sensor_data_t));
    if (sensors) {
        printf("Reading sensor array:\n");
        sensor_data_t data;

        /* Read first 5 sensors */
        for (size_t i = 0; i < 5; i++) {
            if (zipc_array_get(sensors, i, &data) == ZIPC_OK) {
                printf("  Sensor[%zu]: temp=%.1f°C, humidity=%.1f%%, time=%u\n",
                       i, data.temperature, data.humidity, data.timestamp);
            }
        }
        printf("  ... (95 more sensors)\n");
        zipc_view_close(sensors);
    }

    /* Read event queue */
    zipc_view_t events = zipc_get(shm, "event_queue", ZIPC_TYPE_QUEUE,
                                  sizeof(event_t));
    if (events) {
        printf("\nReading event queue:\n");
        printf("  Queue size: %zu\n", zipc_queue_size(events));

        event_t evt;
        int count = 0;
        while (zipc_queue_pop(events, &evt) == ZIPC_OK && count < 3) {
            printf("  Event: id=%u, pid=%u, msg='%s'\n",
                   evt.event_id, evt.source_pid, evt.message);
            count++;
        }

        if (zipc_queue_size(events) > 0) {
            printf("  ... (%zu more events)\n", zipc_queue_size(events));
        }

        /* Add a new event from C */
        event_t new_evt = {
            .event_id = 9999,
            .source_pid = getpid(),
            .timestamp = (uint64_t)time(NULL) * 1000
        };
        snprintf(new_evt.message, sizeof(new_evt.message),
                "C Consumer Event PID %d", getpid());
        zipc_queue_push(events, &new_evt);
        printf("  Added new event from C consumer\n");

        zipc_view_close(events);
    }

    /* Read task stack */
    zipc_view_t stack = zipc_get(shm, "task_stack", ZIPC_TYPE_STACK,
                                 sizeof(uint32_t));
    if (stack) {
        printf("\nReading task stack:\n");
        printf("  Stack size: %zu\n", zipc_stack_size(stack));

        uint32_t task_id;
        if (zipc_stack_pop(stack, &task_id) == ZIPC_OK) {
            printf("  Popped task ID: %u\n", task_id);
        }

        /* Push a new task */
        uint32_t new_task = 999;
        zipc_stack_push(stack, &new_task);
        printf("  Pushed new task ID: %u\n", new_task);

        zipc_view_close(stack);
    }

    /* Read statistics */
    zipc_view_t stats = zipc_get(shm, "statistics", ZIPC_TYPE_ARRAY,
                                 sizeof(double));
    if (stats) {
        printf("\nReading statistics array:\n");
        double sum = 0.0;
        for (size_t i = 0; i < 10; i++) {
            double value;
            if (zipc_array_get(stats, i, &value) == ZIPC_OK) {
                sum += value;
                if (i < 3) {
                    printf("  stats[%zu] = %.4f\n", i, value);
                }
            }
        }
        printf("  ... (7 more values)\n");
        printf("  Sum of all values: %.4f\n", sum);

        zipc_view_close(stats);
    }

    /* List all structures using iterator */
    printf("\n");
    printf("All structures in shared memory:\n");

    struct iter_context {
        int count;
    } ctx = {0};

    /* Iterator callback function */
    bool iter_callback(const char* name, size_t offset, size_t size, void* context) {
        struct iter_context* c = (struct iter_context*)context;
        printf("  %2d. %-20s offset=0x%08zx size=%zu bytes\n",
               ++c->count, name, offset, size);
        return true;
    }

    zipc_iterate(shm, iter_callback, &ctx);

    /* Clean up */
    zipc_close(shm);

    printf("\nC Consumer finished.\n");
}

/* Concurrent stress test - multiple processes */
void stress_test(void) {
    printf("=== C Stress Test: Concurrent operations ===\n\n");

    zipc_shm_t shm = zipc_open("/zeroipc_interop", 0, 0);
    if (!shm) {
        fprintf(stderr, "Failed to open shared memory\n");
        exit(1);
    }

    zipc_view_t queue = zipc_get(shm, "event_queue", ZIPC_TYPE_QUEUE,
                                 sizeof(event_t));
    if (!queue) {
        fprintf(stderr, "Failed to open event queue\n");
        exit(1);
    }

    printf("Process %d performing 1000 queue operations...\n", getpid());

    int pushes = 0, pops = 0;
    for (int i = 0; i < 1000; i++) {
        if (rand() % 2 == 0) {
            /* Try to push */
            event_t evt = {
                .event_id = (uint32_t)(10000 + i),
                .source_pid = getpid(),
                .timestamp = (uint64_t)time(NULL) * 1000 + i
            };
            snprintf(evt.message, sizeof(evt.message),
                    "Stress test %d from PID %d", i, getpid());

            if (zipc_queue_push(queue, &evt) == ZIPC_OK) {
                pushes++;
            }
        } else {
            /* Try to pop */
            event_t evt;
            if (zipc_queue_pop(queue, &evt) == ZIPC_OK) {
                pops++;
            }
        }
    }

    printf("Completed: %d pushes, %d pops\n", pushes, pops);
    printf("Final queue size: %zu\n", zipc_queue_size(queue));

    zipc_view_close(queue);
    zipc_close(shm);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <command>\n", argv[0]);
        printf("Commands:\n");
        printf("  create  - Create shared structures\n");
        printf("  read    - Read existing structures\n");
        printf("  stress  - Concurrent stress test\n");
        printf("  clean   - Remove shared memory\n");
        return 1;
    }

    if (strcmp(argv[1], "create") == 0) {
        create_structures();
    } else if (strcmp(argv[1], "read") == 0) {
        read_structures();
    } else if (strcmp(argv[1], "stress") == 0) {
        stress_test();
    } else if (strcmp(argv[1], "clean") == 0) {
        zipc_destroy("/zeroipc_interop");
        printf("Cleaned up shared memory\n");
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }

    return 0;
}