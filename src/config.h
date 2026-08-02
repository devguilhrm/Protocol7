#ifndef CONFIG_H
#define CONFIG_H

#define MAX_SITES 32
#define MAX_PATH_LEN 512

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

int parse_config(const char* filename, Config* config);

#endif