#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "logger.h"

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