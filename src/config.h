#ifndef CONFIG_H
#define CONFIG_H

#define MAX_PATH_LEN 512
#define MAX_SITES 16

typedef struct {
    char host[MAX_PATH_LEN];
    char root[MAX_PATH_LEN];
} Site;

typedef struct {
    char listen[64];
    int port;
    int site_count;
    Site sites[MAX_SITES];
} Config;

int parse_config(const char* filename, Config* config);

#endif