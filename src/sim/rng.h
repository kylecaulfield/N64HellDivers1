#ifndef SIM_RNG_H
#define SIM_RNG_H

// Deterministic PRNG (xorshift32). The sim never calls rand(): every random
// decision comes from an rng_t owned by the sim state, so runs are
// reproducible from a seed on both the N64 and the host tests.

#include <stdint.h>

typedef struct {
    uint32_t state;
} rng_t;

void     rng_seed(rng_t *r, uint32_t seed);
uint32_t rng_next(rng_t *r);
// Uniform integer in [0, n). n must be > 0.
uint32_t rng_range(rng_t *r, uint32_t n);
// Uniform float in [0, 1).
float    rng_float(rng_t *r);

#endif
