#include <stdarg.h>
#include <stdio.h>

#include "rkv/util_log.h"

// Default log level
static rkv_log_level threshold = RKV_LOG_INFO;

static const char *level_name(rkv_log_level level) {
    switch (level) {
    case RKV_LOG_DEBUG:
        return "DEBUG";
    case RKV_LOG_INFO:
        return "INFO";
    case RKV_LOG_WARN:
        return "WARN";
    case RKV_LOG_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

void rkv_log_set_level(rkv_log_level level) {
    threshold = level;
}

void rkv_log(rkv_log_level level, const char *fmt, ...) {
    if (level < threshold) {
        return;
    }

    fprintf(stderr, "[%s] ", level_name(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
