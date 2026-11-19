#ifndef _EMU_PRNG_H
#define _EMU_PRNG_H

#include <stdint.h>

enum prng_stream_id {
    PRNG_JITTER = 0,
    PRNG_LOSS,
    PRNG_CORRUPT,
    PRNG_CORRUPT_BIT,
    PRNG_DUP,
    PRNG_REORDER,
    PRNG_STREAM_COUNT
};

struct prng_state {
    uint64_t s[4];
};

/** Initialize a xoshiro256** PRNG stream from master_seed and stream_id via SplitMix64. */
void prng_init(struct prng_state *rng, uint64_t master_seed, uint64_t stream_id);

/** Generate the next 64-bit pseudo-random unsigned integer. */
uint64_t prng_next_u64(struct prng_state *rng);

/** Return a uniform double in [0.0, 1.0). */
double prng_uniform01(struct prng_state *rng);

/** Return an unbiased uniform integer in [0, bound). */
uint64_t prng_uniform_u64(struct prng_state *rng, uint64_t bound);

#endif
