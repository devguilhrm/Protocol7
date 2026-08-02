#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static void log_message(const char* level, const char* format, va_list args) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", t);
    
    fprintf(stdout, "[%s] [%s] ", time_buf, level);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    fflush(stdout);
}

void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message("INFO", format, args);
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message("ERROR", format, args);
    va_end(args);
}