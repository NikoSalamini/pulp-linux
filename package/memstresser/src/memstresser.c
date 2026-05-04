#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

typedef enum {
    MODE_READ,
    MODE_WRITE,
    MODE_READWRITE
} access_mode_t;

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <size_in_bytes> <mode: r|w|rw>\n", argv[0]);
        return 1;
    }

    size_t size = strtoull(argv[1], NULL, 0);

    access_mode_t mode;
    if (strcmp(argv[2], "r") == 0) {
        mode = MODE_READ;
    } else if (strcmp(argv[2], "w") == 0) {
        mode = MODE_WRITE;
    } else if (strcmp(argv[2], "rw") == 0) {
        mode = MODE_READWRITE;
    } else {
        printf("Invalid mode. Use r, w, or rw\n");
        return 1;
    }

    // Allocate memory
    volatile uint8_t *buffer = malloc(size);
    if (!buffer) {
        perror("malloc failed");
        return 1;
    }

    // Initialize buffer (important for read-only mode)
    for (size_t i = 0; i < size; i++) {
        buffer[i] = (uint8_t)i;
    }

    struct timespec start_time, end_time;

    if (clock_gettime(CLOCK_MONOTONIC, &start_time) != 0) {
        perror("clock_gettime failed");
        free((void *)buffer);
        return 1;
    }

    volatile uint8_t sink = 0;

    for (size_t i = 0; i < size; i++) {
        if (mode == MODE_WRITE) {
            buffer[i] = (uint8_t)i;
        } else if (mode == MODE_READ) {
            sink += buffer[i];
        } else { // MODE_READWRITE
            buffer[i] = (uint8_t)i;
            sink += buffer[i];
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &end_time) != 0) {
        perror("clock_gettime failed");
        free((void *)buffer);
        return 1;
    }

    double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                        (end_time.tv_nsec - start_time.tv_nsec) / 1e6;

    printf("%.3f\n", elapsed_ms);

    free((void *)buffer);
    return 0;
}