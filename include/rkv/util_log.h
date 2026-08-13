#ifndef RKV_UTIL_LOG_H
#define RKV_UTIL_LOG_H

typedef enum {
    RKV_LOG_DEBUG, // 0
    RKV_LOG_INFO,
    RKV_LOG_WARN,
    RKV_LOG_ERROR,
} rkv_log_level;

void rkv_log_set_level(rkv_log_level level);
void rkv_log(rkv_log_level level, const char *fmt, ...);

#endif
