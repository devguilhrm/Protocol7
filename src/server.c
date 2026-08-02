#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
#include <limits.h>
#include "server.h"
#include "http.h"
#include "utils.h"
#include "logger.h"

#define BUF_SZ 8192

volatile sig_atomic_t running = 1;
int srv_fd = -1;

// silence unused param warning
void sig_handler(int sig) {
    (void)sig;
    running = 0;
    if (srv_fd != -1) {
        close(srv_fd);
        srv_fd = -1;
    }
}

typedef struct {
    int fd;
    Config cfg;
} ConnData;

void* handle_req(void* arg) {
    ConnData* d = (ConnData*)arg;
    int fd = d->fd;
    Config cfg = d->cfg;
    free(d);

    char buf[BUF_SZ];
    size_t n = 0;
    ssize_t r;

    // read until headers end (\r\n\r\n)
    while (n < BUF_SZ - 1) {
        r = recv(fd, buf + n, sizeof(buf) - 1 - n, 0);
        if (r <= 0) {
            close(fd);
            return NULL;
        }
        n += r;
        buf[n] = '\0';

        if (strstr(buf, "\r\n\r\n") != NULL) break;
    }

    if (n >= BUF_SZ - 1) {
        const char* msg = "<h1>431 Request Header Fields Too Large</h1>";
        send_response(fd, 431, "Request Header Fields Too Large", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    // check exactly one Host header
    int n_hosts = 0;
    char host[MAX_PATH_LEN] = "";
    char* p = buf;
    char* q;

    while ((q = strstr(p, "\r\n")) != NULL) {
        size_t len = q - p;
        if (len >= 5 && strncasecmp(p, "Host:", 5) == 0) {
            n_hosts++;
            if (n_hosts == 1) {
                char* val = p + 5;
                while (val < q && (*val == ' ' || *val == '\t')) val++;
                size_t vlen = q - val;
                if (vlen > 0 && vlen < MAX_PATH_LEN) {
                    strncpy(host, val, vlen);
                    host[vlen] = '\0';
                }

            }
        }
        p = q + 2;
    }

    if (n_hosts != 1) {
        const char* msg = "<h1>400 Bad Request</h1><p>Missing or duplicate Host.</p>";
        send_response(fd, 400, "Bad Request", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    // parse request line
    char* crlf = strstr(buf, "\r\n");
    if (!crlf) {
        const char* msg = "<h1>400 Bad Request</h1>";
        send_response(fd, 400, "Bad Request", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }
    
    char tmp = *crlf;
    *crlf = '\0';
    
    char meth[16], uri[MAX_PATH_LEN], ver[16];
    int ok = sscanf(buf, "%15s %511s %15s", meth, uri, ver);
    *crlf = tmp; // restore

    if (ok != 3 || strcmp(meth, "GET") != 0) {
        const char* msg = "<h1>400 Bad Request</h1>";
        send_response(fd, 400, "Bad Request", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    // clean up uri and host
    char* colon = strchr(host, ':');
    if (colon) *colon = '\0';

    char* query = strchr(uri, '?');
    if (query) *query = '\0';

    url_decode(uri);

    // block path traversal
    if (strstr(uri, "..") != NULL) {
        const char* msg = "<h1>403 Forbidden</h1>";
        send_response(fd, 403, "Forbidden", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    // find vhost
    Site* site = NULL;
    for (int i = 0; i < cfg.site_count; i++) {
        if (strcmp(cfg.sites[i].host, host) == 0) {
            site = &cfg.sites[i];
            break;
        }
    }

    if (!site) {
        const char* msg = "<h1>404 Not Found</h1><p>Host not configured.</p>";
        send_response(fd, 404, "Not Found", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    char fpath[MAX_PATH_LEN * 2];
    snprintf(fpath, sizeof(fpath), "%s%s", site->root, uri);

    char rpath[PATH_MAX];
    char rroot[PATH_MAX];

    if (realpath(site->root, rroot) == NULL) {
        const char* msg = "<h1>500 Internal Server Error</h1>";
        send_response(fd, 500, "Internal Server Error", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    if (realpath(fpath, rpath) == NULL) {
        const char* msg = "<h1>404 Not Found</h1>";
        send_response(fd, 404, "Not Found", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    size_t root_len = strlen(rroot);
    if (strncmp(rpath, rroot, root_len) != 0 || 
        (rpath[root_len] != '/' && rpath[root_len] != '\0')) {
        const char* msg = "<h1>403 Forbidden</h1>";
        send_response(fd, 403, "Forbidden", "text/html", msg, strlen(msg));
        close(fd);
        return NULL;
    }

    struct stat st;
    if (stat(rpath, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(rpath);
        if (len + 12 < sizeof(rpath)) {
            strcat(rpath, "/index.html");
            if (stat(rpath, &st) != 0) {
                const char* msg = "<h1>403 Forbidden</h1><p>Directory listing not allowed.</p>";
                send_response(fd, 403, "Forbidden", "text/html", msg, strlen(msg));
                close(fd);
                return NULL;
            }
        }
    }

    log_info("GET %s %s", uri, host);
    send_file(fd, rpath);
    close(fd);
    return NULL;
}

void run_server(Config* config) {
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd == -1) {
        log_error("Failed to create socket");
        return;
    }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(config->listen);
    addr.sin_port = htons(config->port);

    if (bind(srv_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        log_error("Failed to bind on %s:%d", config->listen, config->port);
        close(srv_fd);
        return;
    }

    if (listen(srv_fd, 128) == -1) {
        log_error("Failed to listen");
        close(srv_fd);
        return;
    }

    log_info("Server started on %s:%d", config->listen, config->port);

    while (running) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(srv_fd, (struct sockaddr*)&caddr, &clen);
        
        if (cfd == -1) {
            if (!running) break;
            continue;
        }

        ConnData* d = malloc(sizeof(ConnData));
        if (!d) {
            close(cfd);
            continue;
        }
        d->fd = cfd;
        d->cfg = *config;

        pthread_t th;
        if (pthread_create(&th, NULL, handle_req, d) != 0) {
            log_error("Failed to create thread");
            close(cfd);
            free(d);
        } else {
            pthread_detach(th);
        }
    }

    log_info("Server shut down safely.");
}