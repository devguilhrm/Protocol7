#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include "router.h"

int route_request(const HttpRequest* req, const Config* cfg, char* out_path) {
    // Block path traversal
    if (strstr(req->uri, "..") != NULL) return 403;

    // Match Virtual Host
    Site* site = NULL;
    for (int i = 0; i < cfg->site_count; i++) {
        if (strcmp(cfg->sites[i].host, req->host) == 0) {
            site = &cfg->sites[i];
            break;
        }
    }
    if (!site) return 404;

    // Build and resolve path
    char fpath[MAX_PATH_LEN * 2];
    snprintf(fpath, sizeof(fpath), "%s%s", site->root, req->uri);

    char rryoot[PATH_MAX];
    if (realpath(site->root, rroot) == NULL) return 500;
    if (realpath(fpath, out_path) == NULL) return 404;

    // Verify path is within root
    size_t root_len = strlen(rroot);
    if (strncmp(out_path, rroot, root_len) != 0 || 
        (out_path[root_len] != '/' && out_path[root_len] != '\0')) {
        return 403;
    }

    // Directory fallback to index.html
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