#ifndef REQUEST_H
#define REQUEST_H

#include "config.h"
#include <sys/types.h>

#define REQ_BUF_SIZE 8192

typedef struct {
    char method[16];
    char uri[MAX_PATH_LEN];
    char host[MAX_PATH_LEN];
} HttpRequest;

// Parse raw socket buffer. Returns 200 on success, or HTTP error code (400, 431).
int parse_http_request(const char* buf, size_t len, HttpRequest* req);

// Validates security, resolves vhost and absolute path. Returns 200 or error code (403, 404, 500).
int resolve_secure_path(const HttpRequest* req, const Config* cfg, char* out_path);

#endif