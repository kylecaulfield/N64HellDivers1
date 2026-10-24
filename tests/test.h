#ifndef TEST_H
#define TEST_H

// Minimal test harness. Each test is a function registered in test_main.c.
// A failing CHECK records the failure and keeps going; REQUIRE aborts the test.

#include <math.h>
#include <stdio.h>

extern int test_failures;
extern const char *test_current;

#define CHECK(cond) do { \
    if (!(cond)) { \
        test_failures++; \
        printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, test_current, #cond); \
    } \
} while (0)

#define REQUIRE(cond) do { \
    if (!(cond)) { \
        test_failures++; \
        printf("  FAIL %s:%d [%s] %s (aborting test)\n", __FILE__, __LINE__, test_current, #cond); \
        return; \
    } \
} while (0)

#define CHECK_EQ_INT(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        test_failures++; \
        printf("  FAIL %s:%d [%s] %s == %s (%lld != %lld)\n", \
               __FILE__, __LINE__, test_current, #a, #b, _a, _b); \
    } \
} while (0)

// Float compare with an absolute tolerance. The sim is not bit-exact between
// the host and the VR4300, so tests never compare floats exactly.
#define CHECK_NEAR(a, b, eps) do { \
    double _a = (double)(a), _b = (double)(b); \
    if (fabs(_a - _b) > (eps)) { \
        test_failures++; \
        printf("  FAIL %s:%d [%s] %s ~= %s (%g vs %g)\n", \
               __FILE__, __LINE__, test_current, #a, #b, _a, _b); \
    } \
} while (0)

typedef void (*test_fn)(void);

#endif
