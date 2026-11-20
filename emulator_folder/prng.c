#include "prng.h"

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

void prng_init(struct prng_state *rng, uint64_t master_seed, uint64_t stream_id) {
    uint64_t sm_state = master_seed ^ ((stream_id + 1ULL) * 0x9E3779B97F4A7C15ULL);
    for (int i = 0; i < 4; i++) {
        rng->s[i] = splitmix64(&sm_state);
    }
    if ((rng->s[0] | rng->s[1] | rng->s[2] | rng->s[3]) == 0ULL) {
        rng->s[0] = 0x123456789ABCDEF0ULL;
    }
}

uint64_t prng_next_u64(struct prng_state *rng) {
    const uint64_t result = rotl64(rng->s[1] * 5ULL, 7) * 9ULL;
    const uint64_t t = rng->s[1] << 17;

    rng->s[2] ^= rng->s[0];
    rng->s[3] ^= rng->s[1];
    rng->s[1] ^= rng->s[2];
    rng->s[0] ^= rng->s[3];

    rng->s[2] ^= t;
    rng->s[3] = rotl64(rng->s[3], 45);

    return result;
}

double prng_uniform01(struct prng_state *rng) {
    return (double)(prng_next_u64(rng) >> 11) * (1.0 / 9007199254740992.0);
}

uint64_t prng_uniform_u64(struct prng_state *rng, uint64_t bound) {
    if (bound <= 1ULL) {
        return 0ULL;
    }
    uint64_t threshold = (0ULL - bound) % bound;
    while (1) {
        uint64_t r = prng_next_u64(rng);
        if (r >= threshold) {
            return r % bound;
        }
    }
}
