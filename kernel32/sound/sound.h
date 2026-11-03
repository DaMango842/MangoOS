// kernel32/sound/sound.h
// MangoOS audio subsystem: shared definitions.

#ifndef MANGO_SOUND_H
#define MANGO_SOUND_H

#include <stdint.h>

/*
 * MangoOS Audio Subsystem
 *
 * Provides a hardware-independent audio interface.
 * Drivers (PC Speaker, SB16, AC97...) implement the backend.
 */

#define SOUND_MAX_CHANNELS 2

typedef enum {
    SOUND_MONO   = 1,
    SOUND_STEREO = 2
} SoundChannels;


typedef enum {
    SOUND_FORMAT_U8  = 8,
    SOUND_FORMAT_S16 = 16
} SoundSampleFormat;


/*
 * Audio format description
 */
typedef struct
{
    uint32_t sample_rate;      // Hz
    SoundChannels channels;
    SoundSampleFormat format;

} SoundFormat;


/*
 * Audio device information
 */
typedef struct
{
    const char *name;

    uint32_t capabilities;

    int (*init)(SoundFormat *format);

    int (*write)(const void *buffer, uint32_t size);

    void (*stop)(void);

} SoundDevice;


/*
 * Sound system API
 */

int sound_init(void);

int sound_register_device(SoundDevice *device);

int sound_set_format(SoundFormat *format);

int sound_play(
    const void *buffer,
    uint32_t size
);

void sound_stop(void);


/*
 * Mixer interface
 */

int sound_channel_create(void);

int sound_channel_play(
    int channel,
    const void *buffer,
    uint32_t size
);

void sound_channel_stop(
    int channel
);


#endif