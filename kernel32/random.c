// kernel32/random.c - xorshift32 pseudo-random number generator
#include "random.h"
#include "time.h"

static unsigned int rng_state;

void random_seed(unsigned int seed) {
    rng_state = seed;
}

/* xorshift32.  State 0 is a fixed point, so when never seeded the first
   call replaces it with RTC-derived entropy (each boot differs). */
unsigned int random_next(void) {
    unsigned int x = rng_state;
    if (x == 0) {
        int y, mo, d, h, mi, s;
        rtc_read(&y, &mo, &d, &h, &mi, &s);
        x = 0x9E3779B9u ^ (unsigned int)s ^ ((unsigned int)mi << 8) ^
            ((unsigned int)h << 16) ^ (unsigned int)d;
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

int random_range(int lo, int hi) {
    return lo + (int)(random_next() % ((unsigned int)(hi - lo) + 1u));
}