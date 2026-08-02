#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

void url_decode(char* src);
const char* get_content_type(const char* path);

#endif