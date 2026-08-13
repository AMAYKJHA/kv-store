#include "rkv/util_clock.h"

static int64_t mock_ms = -1;

int64_t rkv_now_ms(void) {
    if (mock_ms >= 0) {
        return mock_ms;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000 + (int64_t) ts.tv_nsec / 1000000;
}

void rkv_clock_set_mock(int64_t ms) {
    mock_ms = ms;
}

// Advance the clock by delta ms
void rkv_clock_advance(int64_t delta) {
    if (mock_ms >= 0) {
        mock_ms += delta;
    }
}

void rkv_clock_use_real(void) {
    mock_ms = -1;
}
