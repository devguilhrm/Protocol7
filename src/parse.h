#ifndef PARSE_H
#define PARSE_H

#include "config.h"

#define REQ_BUF_SIZE 8192

typedef struct {
    char method[16];
    char uri[MAX_PATH_LEN];
    char host[MAX_PATH_LEN];
} HttpRequest;

int parse_request(const char* buf, size_t len, HttpRequest* req);

#endif