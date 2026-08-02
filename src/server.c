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

#define BUFFER_SIZE 8192

volatile sig_atomic_t keep_running = 1;
int listen_fd = -1;

void handle_signal(int sig) {
    (void)sig; // Suprime warning de parâmetro não usado
    keep_running = 0;
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
}

typedef struct {
    int client_fd;
    Config config;
} ClientData;

void* handle_client(void* arg) {
    ClientData* data = (ClientData*)arg;
    int client_fd = data->client_fd;
    Config config = data->config;
    free(data);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    char method[16], path[MAX_PATH_LEN], version[16];
    if (sscanf(buffer, "%15s %511s %15s", method, path, version) != 3) {
        const char* msg = "<h1>400 Bad Request</h1>";
        send_response(client_fd, 400, "Bad Request", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    if (strcmp(method, "GET") != 0) {
        const char* msg = "<h1>405 Method Not Allowed</h1>";
        send_response(client_fd, 405, "Method Not Allowed", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    char req_host[MAX_PATH_LEN] = "";
    char* host_header = strcasestr(buffer, "Host:");
    if (host_header) {
        host_header += 5;
        while (isspace((unsigned char)*host_header)) host_header++;
        char* host_end = strchr(host_header, '\r');
        if (!host_end) host_end = strchr(host_header, '\n');
        if (host_end) {
            int host_len = host_end - host_header;
            if (host_len > 0 && host_len < MAX_PATH_LEN) {
                strncpy(req_host, host_header, host_len);
                req_host[host_len] = '\0';
            }
        }
    }

    char* colon = strchr(req_host, ':');
    if (colon) *colon = '\0';

    char* query = strchr(path, '?');
    if (query) *query = '\0';

    url_decode(path);

    // SEGURANÇA: Bloquear Directory Traversal ANTES de resolver no disco
    if (strstr(path, "..") != NULL) {
        const char* msg = "<h1>403 Forbidden</h1>";
        send_response(client_fd, 403, "Forbidden", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    Site* matched_site = NULL;
    for (int i = 0; i < config.site_count; i++) {
        if (strcmp(config.sites[i].host, req_host) == 0) {
            matched_site = &config.sites[i];
            break;
        }
    }

    if (!matched_site) {
        const char* msg = "<h1>404 Not Found</h1><p>Host não configurado.</p>";
        send_response(client_fd, 404, "Not Found", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    char full_path[MAX_PATH_LEN * 2];
    snprintf(full_path, sizeof(full_path), "%s%s", matched_site->root, path);

    char resolved_path[PATH_MAX];
    char resolved_root[PATH_MAX];

    if (realpath(matched_site->root, resolved_root) == NULL) {
        const char* msg = "<h1>500 Internal Server Error</h1>";
        send_response(client_fd, 500, "Internal Server Error", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    if (realpath(full_path, resolved_path) == NULL) {
        const char* msg = "<h1>404 Not Found</h1>";
        send_response(client_fd, 404, "Not Found", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    size_t root_len = strlen(resolved_root);
    if (strncmp(resolved_path, resolved_root, root_len) != 0 || 
        (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
        const char* msg = "<h1>403 Forbidden</h1>";
        send_response(client_fd, 403, "Forbidden", "text/html", msg, strlen(msg));
        close(client_fd);
        return NULL;
    }

    struct stat stat_buf;
    if (stat(resolved_path, &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode)) {
        size_t len = strlen(resolved_path);
        if (len + 12 < sizeof(resolved_path)) {
            strcat(resolved_path, "/index.html");
            if (stat(resolved_path, &stat_buf) != 0) {
                const char* msg = "<h1>403 Forbidden</h1><p>Directory listing not allowed.</p>";
                send_response(client_fd, 403, "Forbidden", "text/html", msg, strlen(msg));
                close(client_fd);
                return NULL;
            }
        }
    }

    log_info("Served %s for Host: %s", path, req_host);
    send_file(client_fd, resolved_path);
    close(client_fd);
    return NULL;
}

void start_server(Config* config) {
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        log_error("Falha ao criar socket");
        return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config->listen);
    server_addr.sin_port = htons(config->port);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_error("Falha ao fazer bind em %s:%d", config->listen, config->port);
        close(listen_fd);
        return;
    }

    if (listen(listen_fd, 128) == -1) {
        log_error("Falha ao colocar socket em modo de escuta");
        close(listen_fd);
        return;
    }

    log_info("Servidor iniciado em %s:%d", config->listen, config->port);

    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd == -1) {
            if (!keep_running) break;
            continue;
        }

        ClientData* data = malloc(sizeof(ClientData));
        if (!data) {
            close(client_fd);
            continue;
        }
        data->client_fd = client_fd;
        data->config = *config;

        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_client, data) != 0) {
            log_error("Falha ao criar thread");
            close(client_fd);
            free(data);
        } else {
            pthread_detach(thread);
        }
    }

    log_info("Servidor encerrado com segurança.");
}