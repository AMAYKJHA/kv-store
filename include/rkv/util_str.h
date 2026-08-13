#ifndef RKV_UTIL_STR_H
#define RKV_UTIL_STR_H

#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned char *data;
    size_t len;
} rkv_str;

int rkv_str_to_i64(const char *s, size_t len, int64_t *out);

int rkv_i64_to_str(int64_t v, char *buf, size_t bufsize);

#endif
