#ifndef ROUTER_H
#define ROUTER_H

#include "parse.h"
#include <sys/types.h>

int route_request(const HttpRequest* req, const Config* cfg, char* out_path);

#endif