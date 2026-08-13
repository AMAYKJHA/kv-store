#include <stdlib.h>
#include <string.h>

#include "rkv/util_buf.h"

int rkv_buf_ensure(rkv_buf *b, size_t needed, size_t max_cap) {
    if (needed <= b->cap) {
        return 0;
    }

    if (needed > max_cap) {
        return -1;
    }

    size_t new_cap = b->cap == 0 ? 64 : b->cap;
    new_cap = new_cap > max_cap ? max_cap : new_cap;

    while (new_cap < needed) {
        if (new_cap > max_cap / 2) {
            new_cap = max_cap;
            break;
        }
        new_cap *= 2;
    }

    unsigned char *new_data = realloc(b->data, new_cap);
    if (new_data == NULL) {
        return -1;
    }

    b->data = new_data;
    b->cap = new_cap;

    return 0;
}

void rkv_buf_init(rkv_buf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

int rkv_buf_append(rkv_buf *b, const void *data, size_t len, size_t max_cap) {
    if (len > max_cap - b->len) {
        return -1;
    }

    if (rkv_buf_ensure(b, b->len + len, max_cap) < 0) {
        return -1;
    }

    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

void rkv_buf_consume(rkv_buf *b, size_t n) {
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}

void rkv_buf_free(rkv_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}