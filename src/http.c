#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include "http.h"
#include "utils.h"

void send_response(int client_fd, int status_code, const char* status_text,
                   const char* content_type, const char* body, size_t body_len) {
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

void send_error(int client_fd, int status, const char* reason) {
    char body[256];
    int len = snprintf(body, sizeof(body), "<h1>%d %s</h1>", status, reason);
    send_response(client_fd, status, reason, "text/html", body, len);
}

void send_file(int client_fd, const char* filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        send_error(client_fd, 404, "Not Found");
        return;
    }

    struct stat stat_buf;
    if (fstat(fd, &stat_buf) == -1) {
        close(fd);
        send_error(client_fd, 500, "Internal Server Error");
        return;
    }

    if (S_ISDIR(stat_buf.st_mode)) {
        close(fd);
        send_error(client_fd, 403, "Forbidden");
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