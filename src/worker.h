#ifndef WORKER_H
#define WORKER_H

#include "config.h"

typedef struct {
    int fd;
    Config cfg;
} ConnData;

void* handle_connection(void* arg);

#endif