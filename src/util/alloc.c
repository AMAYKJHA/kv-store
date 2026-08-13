#include <stdio.h>
#include <stdlib.h>

#include "rkv/util_alloc.h"

static void oom(size_t size) {
    fprintf(stderr, "rkv: out of memory (requested %zu bytes)\n", size);
    abort();
}

void *rkv_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL && size != 0) {
        oom(size);
    }
    return ptr;
}

void *rkv_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL && size != 0) {
        oom(size);
    }
    return new_ptr;
}

void rkv_free(void *ptr) {
    free(ptr);
}