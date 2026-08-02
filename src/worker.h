#ifndef WORKER_H
#define WORKER_H

#include "config.h"

typedef struct {
    int fd;
    Config cfg;
} ConnData;

// Thread entry point: handles a single client connection lifecycle.
void* handle_connection(void* arg);

#endif