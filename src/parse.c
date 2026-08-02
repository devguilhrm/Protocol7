#include <stdio.h>
#include <string.h>
#include "parse.h"
#include "utils.h"

int parse_request(const char* buf, size_t len, HttpRequest* req) {
    if (len >= REQ_BUF_SIZE - 1) return 431;

    int n_hosts = 0;
    char* p = (char*)buf;
    char* q;

    while ((q = strstr(p, "\r\n")) != NULL) {
        if (q - p >= 5 && strncasecmp(p, "Host:", 5) == 0) {
            n_hosts++;
            if (n_hosts == 1) {
                char* val = p + 5;
                while (val < q && (*val == ' ' || *val == '\t')) val++;
                size_t vlen = q - val;
                if (vlen > 0 && vlen < MAX_PATH_LEN) {
                    strncpy(req->host, val, vlen);
                    req->host[vlen] = '\0';
                }
            }
        }
        p = q + 2;
    }
    if (n_hosts != 1) return 400;

    char tmp[REQ_BUF_SIZE];
    strncpy(tmp, buf, REQ_BUF_SIZE - 1);
    tmp[REQ_BUF_SIZE - 1] = '\0';

    char* crlf = strstr(tmp, "\r\n");
    if (!crlf) return 400;
    *crlf = '\0';

    char version[16];
    if (sscanf(tmp, "%15s %511s %15s", req->method, req->uri, version) != 3) return 400;
    
    if (strcmp(req->method, "GET") != 0) return 400;

    char* colon = strchr(req->host, ':');
    if (colon) *colon = '\0';

    char* query = strchr(req->uri, '?');
    if (query) *query = '\0';

    url_decode(req->uri);

    return 200;
}