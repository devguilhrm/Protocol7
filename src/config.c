#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "logger.h"

static char* trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int parse_string(const char* line, char* out, size_t max_len) {
    const char* start = strchr(line, '"');
    if (!start) return -1;
    start++;
    const char* end = strchr(start, '"');
    if (!end) return -1;
    size_t len = end - start;
    if (len >= max_len) return -1;
    strncpy(out, start, len);
    out[len] = '\0';
    return 0;
}

int parse_config(const char* filename, Config* config) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        log_error("Cannot open config file: %s", filename);
        return -1;
    }

    memset(config, 0, sizeof(Config));
    strcpy(config->listen, "127.0.0.1");
    config->port = 8080;
    config->site_count = 0;

    char line[1024];
    int in_server = 0;
    int in_site = 0;

    while (fgets(line, sizeof(line), f)) {
        char* trimmed = trim(line);
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        if (strcmp(trimmed, "[server]") == 0) {
            in_server = 1;
            in_site = 0;
            continue;
        }
        if (strcmp(trimmed, "[[sites]]") == 0) {
            in_server = 0;
            in_site = 1;
            if (config->site_count < MAX_SITES) {
                config->site_count++;
            }
            continue;
        }

        char* eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trim(trimmed);
        char* value = trim(eq + 1);

        if (in_server) {
            if (strcmp(key, "listen") == 0) {
                parse_string(value, config->listen, sizeof(config->listen));
            } else if (strcmp(key, "port") == 0) {
                config->port = atoi(value);
            }
        } else if (in_site && config->site_count > 0) {
            Site* site = &config->sites[config->site_count - 1];
            if (strcmp(key, "host") == 0) {
                parse_string(value, site->host, sizeof(site->host));
            } else if (strcmp(key, "root") == 0) {
                parse_string(value, site->root, sizeof(site->root));
            }
        }
    }

    fclose(f);
    return 0;
}