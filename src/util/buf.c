#include <stdlib.h>

#include "../../include/rkv/util_buf.h"

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