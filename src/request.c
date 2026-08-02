#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>
#include "request.h"
#include "utils.h"

int parse_http_request(const char* buf, size_t len, HttpRequest* req) {
    if (len >= REQ_BUF_SIZE - 1) return 431;

    // Validate Host header (requires exactly one)
    int n_hosts = 0;
    char* p = (char*)buf;
    char* q;

    while ((q = strstr(p, "\r\n")) != NULL) {
        size_t line_len = q - p;
        if (line_len >= 5 && strncasecmp(p, "Host:", 5) == 0) {
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

    // Parse request line (copy to avoid mutating original buffer)
    char tmp_buf[REQ_BUF_SIZE];
    strncpy(tmp_buf, buf, REQ_BUF_SIZE - 1);
    tmp_buf[REQ_BUF_SIZE - 1] = '\0';

    char* crlf = strstr(tmp_buf, "\r\n");
    if (!crlf) return 400;
    *crlf = '\0';

    if (sscanf(tmp_buf, "%15s %511s %*s", req->method, req->uri) != 2) return 400;
    if (strcmp(req->method, "GET") != 0) return 400;

    //  Basic cleanup
    char* colon = strchr(req->host, ':');
    if (colon) *colon = '\0';

    char* query = strchr(req->uri, '?');
    if (query) *query = '\0';

    url_decode(req->uri);

    return 200;
}

int resolve_secure_path(const HttpRequest* req, const Config* cfg, char* out_path) {
    // Block simple traversal before touching disk
    if (strstr(req->uri, "..") != NULL) return 403;

    // Lookup vhost
    const Site* site = NULL;
    for (int i = 0; i < cfg->site_count; i++) {
        if (strcmp(cfg->sites[i].host, req->host) == 0) {
            site = &cfg->sites[i];
            break;
        }
    }
    if (!site) return 404;

    char fpath[MAX_PATH_LEN * 2];
    snprintf(fpath, sizeof(fpath), "%s%s", site->root, req->uri);

    char rroot[PATH_MAX];
    if (realpath(site->root, rroot) == NULL) return 500;

    if (realpath(fpath, out_path) == NULL) return 404;

    size_t root_len = strlen(rroot);
    if (strncmp(out_path, rroot, root_len) != 0 || 
        (out_path[root_len] != '/' && out_path[root_len] != '\0')) {
        return 403;
    }

    // Support index.html in directories
    struct stat st;
    if (stat(out_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(out_path);
        if (len + 12 < PATH_MAX) {
            strcat(out_path, "/index.html");
            if (stat(out_path, &st) != 0) return 403;
        }
    }

    return 200;
}