#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* True when the n characters at s equal the NUL-terminated lit, ignoring ASCII case. */
static inline bool str_ieq(const char *s, size_t n, const char *lit)
{
    if (strlen(lit) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char a = s[i], b = lit[i];
        if (a >= 'a' && a <= 'z') {
            a = (char)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (char)(b - 'a' + 'A');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}
