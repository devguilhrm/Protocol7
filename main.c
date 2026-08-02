#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

#define MAX_SITES 32
#define MAX_PATH_LEN 512
#define BUFFER_SIZE 8192

typedef struct {
    char host[MAX_PATH_LEN];
    char root[MAX_PATH_LEN];
} Site;

typedef struct {
    char listen[MAX_PATH_LEN];
    int port;
    Site sites[MAX_SITES];
    int site_count;
} Config;

volatile sig_atomic_t keep_running = 1;
int listen_fd = -1;

void log_info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "[INFO] ");
    vfprintf(stdout, fmt, args);
    fprintf(stdout, "\n");
    va_end(args);
}

void log_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void handle_signal(int sig) {
    keep_running = 0;
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
}

int parse_config(const char* filename, Config* config) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        log_error("Não foi possível abrir o arquivo de config: %s", filename);
        return -1;
    }

    memset(config, 0, sizeof(Config));
    config->port = 8080;
    strncpy(config->listen, "127.0.0.1", MAX_PATH_LEN - 1);

    char line[1024];
    int in_sites = 0;
    int current_site = -1;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && isspace((unsigned char)line[len-1])) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        if (strcmp(line, "[server]") == 0) { in_sites = 0; continue; }
        if (strcmp(line, "[[sites]]") == 0) {
            in_sites = 1;
            if (current_site + 1 < MAX_SITES) {
                current_site++;
                config->site_count++;
                memset(&config->sites[current_site], 0, sizeof(Site));
            }
            continue;
        }

        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;

        while (isspace((unsigned char)*key)) key++;
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && isspace((unsigned char)*key_end)) *key_end-- = '\0';

        while (isspace((unsigned char)*value)) value++;
        
        if (*value == '"') {
            value++;
            char* val_end = value + strlen(value) - 1;
            if (val_end >= value && *val_end == '"') *val_end = '\0';
        } else {
            char* val_end = value + strlen(value) - 1;
            while (val_end > value && isspace((unsigned char)*val_end)) *val_end-- = '\0';
        }

        if (!in_sites) {
            if (strcmp(key, "listen") == 0) {
                strncpy(config->listen, value, MAX_PATH_LEN - 1);
                config->listen[MAX_PATH_LEN - 1] = '\0';
            } else if (strcmp(key, "port") == 0) {
                config->port = atoi(value);
            }
        } else {
            if (current_site >= 0) {
                if (strcmp(key, "host") == 0) {
                    strncpy(config->sites[current_site].host, value, MAX_PATH_LEN - 1);
                    config->sites[current_site].host[MAX_PATH_LEN - 1] = '\0';
                } else if (strcmp(key, "root") == 0) {
                    strncpy(config->sites[current_site].root, value, MAX_PATH_LEN - 1);
                    config->sites[current_site].root[MAX_PATH_LEN - 1] = '\0';
                }
            }
        }
    }
    fclose(f);
    return 0;
}

const char* get_content_type(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    return "application/octet-stream";
}

void url_decode(char* src) {
    char* dst = src;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst = ' ';
            src++;
        } else {
            *dst = *src;
            src++;
        }
        dst++;
    }
    *dst = '\0';
}

void send_response(int client_fd, int status_code, const char* status_text, const char* content_type, const char* body, size_t body_len) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", status_code, status_text, content_type, body_len);
    
    write(client_fd, header, header_len);
    if (body && body_len > 0) write(client_fd, body, body_len);
}

void send_file(int client_fd, const char* filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        const char* msg = "<h1>404 Not Found</h1>";
        send_response(client_fd, 404, "Not Found", "text/html", msg, strlen(msg));
        return;
    }

    struct stat stat_buf;
    if (fstat(fd, &stat_buf) == -1) {
        close(fd);
        const char* msg = "<h1>500 Internal Server Error</h1>";
        send_response(client_fd, 500, "Internal Server Error", "text/html", msg, strlen(msg));
        return;
    }

    if (S_ISDIR(stat_buf.st_mode)) {
        close(fd);
        const char* msg = "<h1>403 Forbidden</h1><p>Directory listing not allowed.</p>";
        send_response(client_fd, 403, "Forbidden", "text/html", msg, strlen(msg));
        return;
    }

    const char* content_type = get_content_type(filepath);
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n", content_type, (long)stat_buf.st_size);

    write(client_fd, header, header_len);
    off_t offset = 0;
    sendfile(client_fd, fd, &offset, stat_buf.st_size);
    close(fd);
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

int main(int argc, char* argv[]) {
    if (argc != 3 || strncmp(argv[1], "--config=", 9) != 0) {
        fprintf(stderr, "Uso: %s --config=<arquivo.toml>\n", argv[0]);
        return 1;
    }

    const char* config_file = argv[1] + 9;
    Config config;
    if (parse_config(config_file, &config) != 0) return 1;

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        log_error("Falha ao criar socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(config.listen);
    server_addr.sin_port = htons(config.port);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_error("Falha ao fazer bind em %s:%d", config.listen, config.port);
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 128) == -1) {
        log_error("Falha ao colocar socket em modo de escuta");
        close(listen_fd);
        return 1;
    }

    log_info("Servidor iniciado em %s:%d", config.listen, config.port);

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
        data->config = config;

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
    return 0;
}