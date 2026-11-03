// kernel32/fc/fc_input.c - NES joypad #1 driven by the PS/2 keyboard.
// Level state comes from keyboard.c's scancode bitmap (kbd_copy_state).
// The driver stores standard keys as their raw make byte and extended
// keys (arrows) with bit7 set.  Both scan code sets are accepted because
// whether the i8042 translates to set 1 depends on the firmware:
//   set 2 raw   = translation off
//   set 1       = translation on (QEMU/SeaBIOS default)

#include "fc.h"

/* ---- set 2 raw codes (translation off): arrows only; letters are
   NOT dual-listed because set-1 codes collide (e.g. set-1 Enter 0x1C ==
   set-2 A 0x1C made Enter also press Left) ---- */
#define KC_UP    0xF5u  /* E0 75 */
#define KC_DOWN  0xF2u  /* E0 72 */
#define KC_LEFT  0xEBu  /* E0 6B */
#define KC_RIGHT 0xF4u  /* E0 74 */

/* ---- set 1 translated codes (QEMU/SeaBIOS default) ---- */
#define K1_UP    0xC8u  /* E0 48 */
#define K1_DOWN  0xD0u  /* E0 50 */
#define K1_LEFT  0xCBu  /* E0 4B */
#define K1_RIGHT 0xCDu  /* E0 4D */
#define K1_W     0x11u
#define K1_A     0x1Eu
#define K1_S     0x1Fu
#define K1_D     0x20u
#define K1_J     0x24u
#define K1_K     0x25u
#define K1_ENTER 0x1Cu
#define K1_NPAD_ENTER 0x9Cu /* E0 1C: numpad Enter, ext bit set by driver */
#define K1_SHIFT 0x2Au
#define K1_Z     0x2Cu
#define K1_X     0x2Du

/* button bits in the standard NES order (A at bit7 ... Right at bit0) */
#define PAD_A     0x80u
#define PAD_B     0x40u
#define PAD_SELECT 0x20u
#define PAD_START 0x10u
#define PAD_UP    0x08u
#define PAD_DOWN  0x04u
#define PAD_LEFT  0x02u
#define PAD_RIGHT 0x01u

static int key_is_down_in_snapshot(const unsigned char *state, unsigned char sc) {
    unsigned int idx = (unsigned int)(sc >> 3);
    unsigned int bit = (unsigned int)(sc & 7u);

    return (state[idx] >> bit) & 1u;
}

/* either scan code set counts as "held" */
static int key_down2(const unsigned char *state, unsigned char s2,
                     unsigned char s1) {
    return key_is_down_in_snapshot(state, s2) ||
           key_is_down_in_snapshot(state, s1);
}

static unsigned char pad_state_from_snapshot(const unsigned char *state) {
    unsigned char v = 0;

    if (key_is_down_in_snapshot(state, K1_J) ||
        key_is_down_in_snapshot(state, K1_X))
        v |= PAD_A;
    if (key_is_down_in_snapshot(state, K1_K) ||
        key_is_down_in_snapshot(state, K1_Z))
        v |= PAD_B;
    if (key_is_down_in_snapshot(state, K1_SHIFT))
        v |= PAD_SELECT;
    /* main Enter + numpad Enter (extended) + set-2 raw Enter */
    if (key_is_down_in_snapshot(state, K1_ENTER) ||
        key_is_down_in_snapshot(state, K1_NPAD_ENTER))
        v |= PAD_START;

    {
        int up = key_is_down_in_snapshot(state, K1_W) ||
                 key_down2(state, KC_UP, K1_UP);
        int down = key_is_down_in_snapshot(state, K1_S) ||
                   key_down2(state, KC_DOWN, K1_DOWN);
        int left = key_is_down_in_snapshot(state, K1_A) ||
                   key_down2(state, KC_LEFT, K1_LEFT);
        int right = key_is_down_in_snapshot(state, K1_D) ||
                    key_down2(state, KC_RIGHT, K1_RIGHT);

        if (up)
            v |= PAD_UP;
        else if (down)
            v |= PAD_DOWN;

        if (left)
            v |= PAD_LEFT;
        else if (right)
            v |= PAD_RIGHT;
    }

    return v;
}

/* called once per frame before the CPU runs */
void fc_input_sample(void) {
    unsigned char snapshot[32];

    kbd_copy_state(snapshot);
    fc.pad_latch[0] = pad_state_from_snapshot(snapshot);
    fc.pad_latch[1] = 0;               /* joypad #2 unused */
}

/* serial "fcbtn <hex>" override for scripted verification */
void fc_input_override(unsigned int mask) {
    fc.pad_latch[0] = (unsigned char)(mask & 0xFFu);
}