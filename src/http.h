#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

void send_response(int client_fd, int status_code, const char* status_text, 
                   const char* content_type, const char* body, size_t body_len);
void send_file(int client_fd, const char* filepath);

#endif