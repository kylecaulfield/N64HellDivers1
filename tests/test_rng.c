#include "test.h"
#include "rng.h"

void test_rng_deterministic(void)
{
    rng_t a, b;
    rng_seed(&a, 1234);
    rng_seed(&b, 1234);
    for (int i = 0; i < 1000; i++)
        CHECK_EQ_INT(rng_next(&a), rng_next(&b));
}

void test_rng_zero_seed_not_stuck(void)
{
    rng_t r;
    rng_seed(&r, 0);
    CHECK(rng_next(&r) != 0);
    CHECK(rng_next(&r) != 0);
}

void test_rng_range_bounds_and_coverage(void)
{
    rng_t r;
    rng_seed(&r, 42);
    int hist[6] = {0};
    for (int i = 0; i < 6000; i++) {
        uint32_t v = rng_range(&r, 6);
        REQUIRE(v < 6);
        hist[v]++;
    }
    // Loose uniformity check: every bucket within 20% of the expectation.
    for (int i = 0; i < 6; i++)
        CHECK(hist[i] > 800 && hist[i] < 1200);
}

void test_rng_float_unit_interval(void)
{
    rng_t r;
    rng_seed(&r, 7);
    for (int i = 0; i < 10000; i++) {
        float f = rng_float(&r);
        REQUIRE(f >= 0.0f && f < 1.0f);
    }
}
