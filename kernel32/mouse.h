// kernel32/mouse.h - PS/2 mouse, polling.
//
// 3-byte protocol.  Two pointer modes share one accumulator:
//   mode 0 (text)  - pointer is a character cell, drawn as a solid block
//   mode 1 (pixel)  - pointer is a raw 0..(screen-1) coordinate, 1:1
//
// mouse_poll() resynchronises on a bad status byte or on an X/Y overflow
// rather than letting a desynced stream corrupt the position.
#ifndef MANGO_MOUSE_H
#define MANGO_MOUSE_H

#include <stdint.h>      /* mouse.c needs int8_t for the signed deltas */

/* Current pointer position in the active mode.  These are globals so the
   GUI event loop and the shell can read them without a call. */
extern int mouse_x, mouse_y;
extern int mouse_enabled;

int mouse_init(void);

/* Drain the PS/2 output port and update the position. */
void mouse_poll(void);

/* Bit 0 = left, bit 1 = right, bit 2 = middle. */
int mouse_read_buttons(void);

/* Switch pointer mode; returns the previous mode.  Callers must restore
   it when leaving a pixel-mode screen. */
int mouse_set_mode(int mode);     /* 0 = text, 1 = pixel */

/* Current position.  Returns 0 on success, -1 when the active mode has no
   absolute pixel position to report (i.e. text mode). */
int mouse_get_pos(int *x, int *y);

#endif /* MANGO_MOUSE_H */