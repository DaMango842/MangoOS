// kernel32/sound/sound.c
// MangoOS Sound System Core

#include "sound.h"

#define MAX_SOUND_DEVICES 8
#define MAX_SOUND_CHANNELS 16


static SoundDevice *devices[MAX_SOUND_DEVICES];

static int device_count = 0;

static SoundDevice *active_device = 0;

static SoundFormat current_format =
{
    .sample_rate = 44100,
    .channels = SOUND_STEREO,
    .format = SOUND_FORMAT_S16
};


static int channel_used[MAX_SOUND_CHANNELS];



/*
 * Initialize sound subsystem
 */
int sound_init(void)
{
    device_count = 0;
    active_device = 0;

    for (int i = 0; i < MAX_SOUND_CHANNELS; i++)
    {
        channel_used[i] = 0;
    }

    return 0;
}



/*
 * Register a hardware driver
 */
int sound_register_device(SoundDevice *device)
{
    if (!device)
        return -1;


    if (device_count >= MAX_SOUND_DEVICES)
        return -2;


    devices[device_count++] = device;


    /*
     * First registered device becomes default device
     */
    if (!active_device)
    {
        active_device = device;

        if (active_device->init)
        {
            active_device->init(&current_format);
        }
    }


    return 0;
}



/*
 * Change audio format
 */
int sound_set_format(SoundFormat *format)
{
    if (!format)
        return -1;


    current_format = *format;


    if (active_device &&
        active_device->init)
    {
        return active_device->init(&current_format);
    }


    return 0;
}



/*
 * Send PCM data to device
 */
int sound_play(
    const void *buffer,
    uint32_t size
)
{
    if (!active_device)
        return -1;


    if (!active_device->write)
        return -2;


    return active_device->write(buffer, size);
}



/*
 * Stop playback
 */
void sound_stop(void)
{
    if (active_device &&
        active_device->stop)
    {
        active_device->stop();
    }
}



/*
 * Create virtual mixer channel
 */
int sound_channel_create(void)
{
    for (int i = 0; i < MAX_SOUND_CHANNELS; i++)
    {
        if (!channel_used[i])
        {
            channel_used[i] = 1;
            return i;
        }
    }


    return -1;
}



/*
 * Play sound on channel
 *
 * Mixer is not implemented yet.
 * Currently forwards directly.
 */
int sound_channel_play(
    int channel,
    const void *buffer,
    uint32_t size
)
{
    if (channel < 0 ||
        channel >= MAX_SOUND_CHANNELS)
    {
        return -1;
    }


    if (!channel_used[channel])
        return -2;


    return sound_play(buffer, size);
}



/*
 * Stop channel
 */
void sound_channel_stop(int channel)
{
    if (channel < 0 ||
        channel >= MAX_SOUND_CHANNELS)
        return;


    channel_used[channel] = 0;
}