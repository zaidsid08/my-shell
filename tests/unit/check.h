/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

/* Minimal unit-test helpers: no framework dependency, one header.
 *
 *   static void test_something(void) { CHECK_INT(1 + 1, 2); }
 *   int main(void) { RUN(test_something); return check_summary(); }
 */
#ifndef MSH_CHECK_H
#define MSH_CHECK_H

#include <stdio.h>
#include <string.h>

static int check_failures = 0;
static int check_count = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        check_count++;                                                           \
        if (!(cond)) {                                                           \
            check_failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                        \
    } while (0)

#define CHECK_INT(actual, expected)                                              \
    do {                                                                         \
        long long a_ = (long long)(actual), e_ = (long long)(expected);          \
        check_count++;                                                           \
        if (a_ != e_) {                                                          \
            check_failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s == %lld, expected %lld\n",         \
                    __FILE__, __LINE__, #actual, a_, e_);                        \
        }                                                                        \
    } while (0)

#define CHECK_STR(actual, expected)                                              \
    do {                                                                         \
        const char *a_ = (actual), *e_ = (expected);                             \
        check_count++;                                                           \
        if (!a_ || strcmp(a_, e_) != 0) {                                        \
            check_failures++;                                                    \
            fprintf(stderr, "  FAIL %s:%d: %s == \"%s\", expected \"%s\"\n",     \
                    __FILE__, __LINE__, #actual, a_ ? a_ : "(null)", e_);        \
        }                                                                        \
    } while (0)

#define RUN(fn)                                                                  \
    do {                                                                         \
        int before_ = check_failures;                                            \
        fn();                                                                    \
        printf("%s %s\n", check_failures == before_ ? "  ok  " : "  FAIL", #fn); \
    } while (0)

static inline int check_summary(void) {
    printf("%d checks, %d failed\n", check_count, check_failures);
    return check_failures ? 1 : 0;
}

#endif
