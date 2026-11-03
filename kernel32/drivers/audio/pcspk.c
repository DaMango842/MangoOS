// kernel32/drivers/audio/pcspk.c
// IBM PC Speaker driver

#include "sound/sound.h"

#include <stdint.h>


#define PIT_FREQUENCY 1193182

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define SPEAKER_PORT 0x61



static inline void outb(
    uint16_t port,
    uint8_t value
)
{
    __asm__ volatile(
        "outb %0,%1"
        :
        : "a"(value),
          "Nd"(port)
    );
}



static inline uint8_t inb(
    uint16_t port
)
{
    uint8_t ret;

    __asm__ volatile(
        "inb %1,%0"
        : "=a"(ret)
        : "Nd"(port)
    );

    return ret;
}



static void pcspk_beep(
    uint32_t frequency
)
{
    uint32_t divisor;


    if(frequency == 0)
        return;


    divisor =
        PIT_FREQUENCY / frequency;


    // PIT channel 2
    outb(
        PIT_COMMAND,
        0xB6
    );


    outb(
        PIT_CHANNEL2,
        divisor & 0xff
    );


    outb(
        PIT_CHANNEL2,
        divisor >> 8
    );


    uint8_t value =
        inb(SPEAKER_PORT);


    value |= 3;


    outb(
        SPEAKER_PORT,
        value
    );
}



static void pcspk_stop(void)
{
    uint8_t value =
        inb(SPEAKER_PORT);


    value &= ~3;


    outb(
        SPEAKER_PORT,
        value
    );
}


static void pcspk_delay(uint32_t ticks)
{
    volatile uint32_t i;

    for (i = 0; i < ticks; i++)
    {
        __asm__ volatile("nop");
    }
}


static int pcspk_init(
    SoundFormat *format
)
{
    /*
     * PC Speaker doesn't support PCM.
     */

    return 0;
}



SoundDevice pcspk_device =
{
    .name = "PC Speaker",

    .capabilities = 0,

    .init = pcspk_init,

    .write = 0,

    .stop = pcspk_stop
};



void pcspk_register(void)
{
    sound_register_device(
        &pcspk_device
    );
}



void pcspk_beep_test(void)
{
    pcspk_beep(1000);
    pcspk_delay(600000);
    pcspk_stop();
}