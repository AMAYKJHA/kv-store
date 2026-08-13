#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rkv/util_str.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #cond);                                            \
            failures++;                                                \
        }                                                              \
    } while (0)

/* Helper: parse a C-string literal (uses strlen for length). */
static int parse(const char *s, int64_t *out) {
    return rkv_str_to_i64(s, strlen(s), out);
}

static void test_parse_valid(void) {
    int64_t v;
    CHECK(parse("0", &v) == 0 && v == 0);
    CHECK(parse("7", &v) == 0 && v == 7);
    CHECK(parse("123", &v) == 0 && v == 123);
    CHECK(parse("-45", &v) == 0 && v == -45);
    CHECK(parse("9223372036854775807", &v) == 0 && v == INT64_MAX);
    CHECK(parse("-9223372036854775808", &v) == 0 && v == INT64_MIN);
}

static void test_parse_binary_safe(void) {
    /* len is honored, not a trailing '\0': only the first 3 bytes count. */
    int64_t v;
    CHECK(rkv_str_to_i64("123", 3, &v) == 0 && v == 123);
    /* An embedded NUL mid-input is a non-digit -> reject. */
    CHECK(rkv_str_to_i64("12\0" "3", 4, &v) == -1);
}

static void test_parse_rejects(void) {
    int64_t v;
    CHECK(parse("", &v) == -1);        /* empty */
    CHECK(parse("-", &v) == -1);       /* minus with no digits */
    CHECK(parse("12x", &v) == -1);     /* trailing junk */
    CHECK(parse("x12", &v) == -1);     /* leading junk */
    CHECK(parse(" 12", &v) == -1);     /* leading space */
    CHECK(parse("12 ", &v) == -1);     /* trailing space */
    CHECK(parse("+12", &v) == -1);     /* explicit plus not allowed */
    CHECK(parse("007", &v) == -1);     /* leading zero */
    CHECK(parse("-05", &v) == -1);     /* leading zero after sign */
    CHECK(parse("--5", &v) == -1);     /* double sign */
    /* Just past the limits on both ends. */
    CHECK(parse("9223372036854775808", &v) == -1);
    CHECK(parse("-9223372036854775809", &v) == -1);
}

static void test_format(void) {
    char buf[21];
    int n;

    n = rkv_i64_to_str(0, buf, sizeof buf);
    CHECK(n == 1 && strcmp(buf, "0") == 0);

    n = rkv_i64_to_str(123, buf, sizeof buf);
    CHECK(n == 3 && strcmp(buf, "123") == 0);

    n = rkv_i64_to_str(-45, buf, sizeof buf);
    CHECK(n == 3 && strcmp(buf, "-45") == 0);

    n = rkv_i64_to_str(INT64_MAX, buf, sizeof buf);
    CHECK(n == 19 && strcmp(buf, "9223372036854775807") == 0);

    n = rkv_i64_to_str(INT64_MIN, buf, sizeof buf);
    CHECK(n == 20 && strcmp(buf, "-9223372036854775808") == 0);
}

static void test_format_buffer_too_small(void) {
    char buf[4];
    /* "12345" needs 5 chars + NUL = 6 > 4 -> reject, return -1. */
    CHECK(rkv_i64_to_str(12345, buf, sizeof buf) == -1);
    /* Exact fit: "999" is 3 chars + NUL = 4 == bufsize -> ok. */
    CHECK(rkv_i64_to_str(999, buf, sizeof buf) == 3 && strcmp(buf, "999") == 0);
}

static void test_round_trip(void) {
    /* Format then parse should return the original value. */
    int64_t values[] = {0, 1, -1, 42, -42, INT64_MAX, INT64_MIN};
    char buf[21];
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        int n = rkv_i64_to_str(values[i], buf, sizeof buf);
        CHECK(n > 0);
        int64_t back;
        CHECK(rkv_str_to_i64(buf, (size_t)n, &back) == 0);
        CHECK(back == values[i]);
    }
}

int main(void) {
    test_parse_valid();
    test_parse_binary_safe();
    test_parse_rejects();
    test_format();
    test_format_buffer_too_small();
    test_round_trip();

    if (failures == 0) {
        printf("test_str: all checks passed\n");
        return 0;
    }
    fprintf(stderr, "test_str: %d check(s) failed\n", failures);
    return 1;
}
