#pragma once

#include <stdio.h>
#include <string.h>

/* Minimal test harness: CHECK counts failures, RUN runs a test (or only the one named in argv[1]). */
static int t_failures;

#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
            t_failures++;                                                \
        }                                                                \
    } while (0)

#define RUN(fn)                                         \
    do {                                                \
        if (argc < 2 || strcmp(argv[1], #fn) == 0) {    \
            printf("- %s\n", #fn);                      \
            fn();                                       \
        }                                               \
    } while (0)

#define TEST_RESULT() (printf(t_failures ? "FAILED (%d)\n" : "ok\n", t_failures), t_failures ? 1 : 0)
