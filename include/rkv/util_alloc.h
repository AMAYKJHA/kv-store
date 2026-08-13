#ifndef RKV_UTIL_ALLOC_H
#define RKV_UTIL_ALLOC_H

#include <stddef.h>

void *rkv_malloc(size_t size);
void *rkv_realloc(void *ptr, size_t size);
void rkv_free(void *ptr);

#endif