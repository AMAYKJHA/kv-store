#include <stdint.h>
#include <stdio.h>

#include "rkv/util_clock.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #cond);                                            \
            failures++;                                                \
        }                                                              \
    } while (0)

static void test_real_clock(void) {
    /* The real monotonic clock is non-negative and never runs backwards. */
    int64_t a = rkv_now_ms();
    int64_t b = rkv_now_ms();
    CHECK(a >= 0);
    CHECK(b >= a); /* monotonic: later reading is never smaller */
}

static void test_mock_freezes_time(void) {
    rkv_clock_set_mock(5000);
    CHECK(rkv_now_ms() == 5000);
    CHECK(rkv_now_ms() == 5000); /* frozen: does not advance on its own */
    rkv_clock_use_real();
}

static void test_mock_advance(void) {
    rkv_clock_set_mock(1000);
    rkv_clock_advance(250);
    CHECK(rkv_now_ms() == 1250);
    rkv_clock_advance(750);
    CHECK(rkv_now_ms() == 2000);
    rkv_clock_use_real();
}

static void test_use_real_restores(void) {
    rkv_clock_set_mock(42);
    CHECK(rkv_now_ms() == 42);
    rkv_clock_use_real();
    /* Back on the real clock now: won't be stuck at the mock value. */
    CHECK(rkv_now_ms() != 42);
}

int main(void) {
    test_real_clock();
    test_mock_freezes_time();
    test_mock_advance();
    test_use_real_restores();

    if (failures == 0) {
        printf("test_clock: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_clock: %d check(s) failed\n", failures);
    return 1;
}
