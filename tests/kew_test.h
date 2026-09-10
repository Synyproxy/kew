#ifndef KEW_TEST_H
#define KEW_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kt_failures = 0;
static int kt_checks = 0;

#define CHECK(cond)                                                             \
        do {                                                                    \
                kt_checks++;                                                    \
                if (!(cond)) {                                                  \
                        kt_failures++;                                          \
                        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,  \
                                __LINE__, #cond);                               \
                }                                                               \
        } while (0)

#define CHECK_STR(a, b)                                                         \
        do {                                                                    \
                kt_checks++;                                                    \
                const char *kt_a = (a);                                         \
                const char *kt_b = (b);                                         \
                if (!kt_a || !kt_b || strcmp(kt_a, kt_b) != 0) {                \
                        kt_failures++;                                          \
                        fprintf(stderr, "%s:%d: CHECK_STR failed: \"%s\" != \"%s\"\n", \
                                __FILE__, __LINE__, kt_a ? kt_a : "(null)",     \
                                kt_b ? kt_b : "(null)");                        \
                }                                                               \
        } while (0)

#define KT_MAIN_END()                                                           \
        do {                                                                    \
                fprintf(stderr, "%s: %d checks, %d failures\n", __FILE__,       \
                        kt_checks, kt_failures);                                \
                return kt_failures ? 1 : 0;                                     \
        } while (0)

#endif
