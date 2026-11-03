// kernel32/timer.h - PIC / PIT / IRQ0 ownership.
//
// PIT channel 0 has exactly ONE owner at a time: the kernel tick
// (timer_init, 100 Hz mode 3 -> IRQ0 -> proc_tick).  Timing-sensitive
// subsystems (MangoWin stats, the NES emulator) need channel 0 as a
// free-running microsecond down counter instead, which is a DIFFERENT
// program mode.  They must therefore borrow the channel and give it
// back, otherwise proc_tick stops and every proc_sleep / time slice
// stalls for the rest of the session.
//
//   timer_pit_borrow()  -> ch0 becomes a free-running 16-bit counter
//   timer_pit_return()  -> ch0 is reprogrammed back to the 100 Hz tick
#ifndef MANGO_TIMER_H
#define MANGO_TIMER_H

#include <stdint.h>

/* PIT input clock (Hz).  All period and tick->us math derives from it. */
#define PIT_INPUT_HZ 1193182ul

/* PIT channel 0 wraps every 65536 / PIT_INPUT_HZ = 54.9 ms while free
   running, so pit_us_since_export() is only valid for deltas < 54.9 ms. */
#define PIT_FREE_RUN_PERIOD_US 54925ul

void timer_init(void);

/* Switch PIT ch0 from the 100 Hz tick to a free-running down counter.
   Call exactly once before reading pit_now_export(). */
void timer_pit_borrow(void);

/* Restore the 100 Hz tick.  Safe to call even if timer_pit_borrow() was
   never called. */
void timer_pit_return(void);

/* True while the channel is borrowed (i.e. the 100 Hz tick is OFF). */
int timer_pit_is_borrowed(void);

/* Latch ch0 and read it.  Only meaningful after timer_pit_borrow(). */
unsigned int pit_now_export(void);

/* Microseconds elapsed since a pit_now_export() sample.  Must be called
   with the free-running timer borrowed. */
unsigned long pit_us_since_export(unsigned int since);

#endif /* MANGO_TIMER_H */