#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <limits.h>
#include "worker.h"
#include "http.h"
#include "parse.h"
#include "router.h"
#include "logger.h"

// Timeout in seconds for client connections (prevents slowloris-style hangs)
#define CLIENT_TIMEOUT_SEC 10

void* handle_connection(void* arg) {
    ConnData* d = (ConnData*)arg;
    int fd = d->fd;
    Config cfg = d->cfg;
    free(d);

    struct timeval tv;
    tv.tv_sec = CLIENT_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[REQ_BUF_SIZE];
    size_t n = 0;
    ssize_t r;

    while (n < REQ_BUF_SIZE - 1) {
        r = recv(fd, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) { close(fd); return NULL; }
        n += r;
        buf[n] = '\0';
        if (strstr(buf, "\r\n\r\n") != NULL) break;
    }

    //  Protocol Layer
    HttpRequest req;
    memset(&req, 0, sizeof(req));
    int status = parse_request(buf, n, &req);

    if (status != 200) {
        send_error(fd, status, status == 431 ? "Request Header Fields Too Large" : "Bad Request");
        close(fd);
        return NULL;
    }

    //  Application Layer
    char final_path[PATH_MAX];
    status = route_request(&req, &cfg, final_path);

    if (status != 200) {
        send_error(fd, status, status == 403 ? "Forbidden" : "Not Found");
        close(fd);
        return NULL;
    }

    //  I/O Layer
    log_info("GET %s %s", req.uri, req.host);
    send_file(fd, final_path);
    close(fd);
    return NULL;
}