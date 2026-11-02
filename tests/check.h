#ifndef ROLLING_ROB_CHECK_H
#define ROLLING_ROB_CHECK_H

/*
 * A tiny test harness: CHECK records a failure and goes on, so one run shows
 * every broken rule; main() returns check_result(). No dependencies.
 */

#include <stdio.h>

static int check_failures;
static int check_count;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        check_count++;                                                              \
        if(!(cond)) {                                                               \
            check_failures++;                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while(0)

#define CHECK_EQ(a, b)                                                                         \
    do {                                                                                       \
        long long check_a_ = (long long)(a), check_b_ = (long long)(b);                        \
        check_count++;                                                                         \
        if(check_a_ != check_b_) {                                                             \
            check_failures++;                                                                  \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (%lld != %lld)\n", __FILE__,      \
                    __LINE__, #a, #b, check_a_, check_b_);                                     \
        }                                                                                      \
    } while(0)

#define RUN(test)                                   \
    do {                                            \
        int check_before_ = check_failures;         \
        test();                                     \
        printf("%-40s %s\n", #test, check_failures == check_before_ ? "ok" : "FAILED"); \
    } while(0)

static int check_result(void)
{
    printf("%d checks, %d failed\n", check_count, check_failures);
    return check_failures ? 1 : 0;
}

#endif /* ROLLING_ROB_CHECK_H */
