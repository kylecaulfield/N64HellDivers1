#include "rng.h"

void rng_seed(rng_t *r, uint32_t seed)
{
    // xorshift has a fixed point at 0.
    r->state = seed ? seed : 0x9E3779B9u;
}

uint32_t rng_next(rng_t *r)
{
    uint32_t x = r->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->state = x;
    return x;
}

uint32_t rng_range(rng_t *r, uint32_t n)
{
    // Multiply-shift avoids the modulo bias and the slow divide.
    return (uint32_t)(((uint64_t)rng_next(r) * n) >> 32);
}

float rng_float(rng_t *r)
{
    // 24 random bits fit exactly in a float mantissa.
    return (float)(rng_next(r) >> 8) * (1.0f / 16777216.0f);
}
