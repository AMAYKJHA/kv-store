#ifndef RKV_UTIL_CLOCK_H
#define RKV_UTIL_CLOCK_H

#include <stdint.h>
#include <time.h>

int64_t rkv_now_ms(void);

void rkv_clock_set_mock(int64_t ms);
void rkv_clock_advance(int64_t delta);
void rkv_clock_use_real(void);

#endif