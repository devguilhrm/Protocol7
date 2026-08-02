#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "server.h"
#include "worker.h"
#include "logger.h"

volatile sig_atomic_t running = 1;
int srv_fd = -1;

void sig_handler(int sig) {
    (void)sig;
    running = 0;
    if (srv_fd != -1) { close(srv_fd); srv_fd = -1; }
}

void run_server(Config* config) {
    struct sigaction sa;
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd == -1) { log_error("Failed to create socket"); return; }

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

    if (listen(srv_fd, 128) == -1) { log_error("Failed to listen"); close(srv_fd); return; }

    log_info("Server started on %s:%d", config->listen, config->port);

    while (running) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(srv_fd, (struct sockaddr*)&caddr, &clen);
        
        if (cfd == -1) { if (!running) break; continue; }

        ConnData* d = malloc(sizeof(ConnData));
        if (!d) { close(cfd); continue; }
        d->fd = cfd;
        d->cfg = *config;

        pthread_t th;
        if (pthread_create(&th, NULL, handle_connection, d) != 0) {
            log_error("Failed to create thread");
            close(cfd);
            free(d);
        } else {
            pthread_detach(th);
        }
    }
    log_info("Server shut down safely.");
}