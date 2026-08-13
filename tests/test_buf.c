#include <stdio.h>
#include <string.h>

#include "rkv/util_buf.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #cond);                                            \
            failures++;                                                \
        }                                                              \
    } while (0)

#define MAXC (1024u * 1024u) /* 1 MiB cap for most tests */

static void test_init(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(b.data == NULL);
    CHECK(b.len == 0);
    CHECK(b.cap == 0);
    rkv_buf_free(&b);
}

static void test_ensure_grows_to_min(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_ensure(&b, 10, MAXC) == 0);
    CHECK(b.data != NULL);
    CHECK(b.cap == 64); /* minimum initial capacity */
    CHECK(b.len == 0);
    rkv_buf_free(&b);
}

static void test_ensure_doubles(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_ensure(&b, 100, MAXC) == 0);
    CHECK(b.cap == 128); /* 64 -> 128 covers 100 */
    rkv_buf_free(&b);
}

static void test_ensure_noop_when_fits(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_ensure(&b, 10, MAXC) == 0);
    size_t cap_before = b.cap;
    unsigned char *data_before = b.data;
    CHECK(rkv_buf_ensure(&b, cap_before, MAXC) == 0); /* needed <= cap */
    CHECK(b.cap == cap_before);
    CHECK(b.data == data_before); /* no realloc */
    rkv_buf_free(&b);
}

static void test_ensure_rejects_over_max(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_ensure(&b, MAXC + 1, MAXC) == -1);
    CHECK(b.data == NULL); /* untouched */
    CHECK(b.cap == 0);
    rkv_buf_free(&b);
}

static void test_ensure_caps_at_max(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    /* needed just below max: growth must clamp to max, not overshoot */
    CHECK(rkv_buf_ensure(&b, MAXC - 1, MAXC) == 0);
    CHECK(b.cap == MAXC);
    rkv_buf_free(&b);
}

static void test_append_basic(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    const char *msg = "hello";
    CHECK(rkv_buf_append(&b, msg, 5, MAXC) == 0);
    CHECK(b.len == 5);
    CHECK(memcmp(b.data, "hello", 5) == 0);
    rkv_buf_free(&b);
}

static void test_append_accumulates(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_append(&b, "foo", 3, MAXC) == 0);
    CHECK(rkv_buf_append(&b, "bar", 3, MAXC) == 0);
    CHECK(b.len == 6);
    CHECK(memcmp(b.data, "foobar", 6) == 0);
    rkv_buf_free(&b);
}

static void test_append_rejects_over_max(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_append(&b, "ab", 2, 4) == 0); /* len now 2, cap 4 */
    /* 3 more would exceed max_cap of 4 -> reject, leave buffer intact */
    CHECK(rkv_buf_append(&b, "xyz", 3, 4) == -1);
    CHECK(b.len == 2);
    CHECK(memcmp(b.data, "ab", 2) == 0);
    rkv_buf_free(&b);
}

static void test_consume_partial(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_append(&b, "abcdef", 6, MAXC) == 0);
    rkv_buf_consume(&b, 2); /* drop "ab" */
    CHECK(b.len == 4);
    CHECK(memcmp(b.data, "cdef", 4) == 0);
    rkv_buf_free(&b);
}

static void test_consume_all(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_append(&b, "abc", 3, MAXC) == 0);
    rkv_buf_consume(&b, 3);
    CHECK(b.len == 0);
    rkv_buf_free(&b);
}

static void test_free_resets(void) {
    rkv_buf b;
    rkv_buf_init(&b);
    CHECK(rkv_buf_append(&b, "abc", 3, MAXC) == 0);
    rkv_buf_free(&b);
    CHECK(b.data == NULL);
    CHECK(b.len == 0);
    CHECK(b.cap == 0);
}

int main(void) {
    test_init();
    test_ensure_grows_to_min();
    test_ensure_doubles();
    test_ensure_noop_when_fits();
    test_ensure_rejects_over_max();
    test_ensure_caps_at_max();
    test_append_basic();
    test_append_accumulates();
    test_append_rejects_over_max();
    test_consume_partial();
    test_consume_all();
    test_free_resets();

    if (failures == 0) {
        printf("test_buf: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_buf: %d check(s) failed\n", failures);
    return 1;
}
