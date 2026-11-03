// kernel32/random.h - xorshift32 pseudo-random numbers.
#ifndef MANGO_RANDOM_H
#define MANGO_RANDOM_H

/* Seed the generator.  State 0 is a fixed point of xorshift, so the
   module self-seeds from the RTC on first use if never seeded. */
void random_seed(unsigned int seed);

/* Next 32-bit value. */
unsigned int random_next(void);

/* Value in [lo, hi] inclusive. */
int random_range(int lo, int hi);

#endif /* MANGO_RANDOM_H */