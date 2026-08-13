#include "rkv/util_str.h"

int rkv_str_to_i64(const char *s, size_t len, int64_t *out) {
    if (len == 0) {
        return -1;
    }
    int neg = 0;
    size_t i = 0;

    // Check for negative number
    if (s[0] == '-') {
        neg = 1;
        i = 1;
        if (i == len) {
            return -1;
        }
    }

    if (s[i] == '0' && len - i > 1) {
        return -1;
    }

    int64_t acc = 0;
    for (; i < len; i++) {
        unsigned char c = (unsigned char) s[i];
        if (c < '0' || c > '9') {
            return -1;
        }
        int d = c - '0'; // digit-char -> its value (the '- 0' trick)

        if (acc < (INT64_MIN + d) / 10) {
            return -1;
        }
        acc = acc * 10 - d;
    }

    if (neg) {
        *out = acc;
    } else {
        if (acc == INT64_MIN) { // the one positive value that can't be flipped
            return -1;
        }
        *out = -acc;
    }
    return 0;
}

int rkv_i64_to_str(int64_t v, char *buf, size_t bufsize) {
    char tmp[21]; // scratch: max 20 digits + '-'
    size_t pos = 0;
    int neg = v < 0;

    int64_t n = v;
    if (n > 0) {
        n = -n; // make it <= 0 so INT64_MIN is safe too
    }

    // Pull digits off the end; they come out reversed (ones digit first).
    do {
        int d = (int) (-(n % 10));
        tmp[pos++] = (char) ('0' + d);
        n /= 10;
    } while (n != 0);

    if (neg) {
        tmp[pos++] = '-';
    }

    if (bufsize < pos + 1) {
        return -1;
    }

    for (size_t k = 0; k < pos; k++) {
        buf[k] = tmp[pos - 1 - k];
    }
    buf[pos] = '\0';

    return (int) pos;
}