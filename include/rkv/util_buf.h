#ifndef RKV_UTIL_BUF_H
#define RKV_UTIL_BUF_H

#include <stddef.h>

// Buffer to store incoming network bytes
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} rkv_buf;

// Initialize buffer
void rkv_buf_init(rkv_buf *b);

// Check if the buffer can hold the additional bytes
int rkv_buf_ensure(rkv_buf *b, size_t needed, size_t max_cap);

// Append new bytes to the existing buffer
int rkv_buf_append(rkv_buf *b, const void *data, size_t len, size_t max_cap);

// Consume bytes from the buffer
void rkv_buf_consume(rkv_buf *b, size_t n);

// Free memory allocated to the buffer
void rkv_buf_free(rkv_buf *b);

#endif