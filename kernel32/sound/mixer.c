// kernel32/sound/mixer.c
// MangoOS Software Mixer
//
// STATUS: NOT WIRED IN.  Nothing calls mixer_init/play/stop/mix, and
// sound.h does not declare them.  It is absent from the linked kernel
// because the objects go through `ar` + `ld`, which only pulls archive
// members whose symbols are referenced - nothing here is.
//
// The live audio path is drivers/audio/apu.c -> sb16 DMA; it deliberately
// bypasses sound.c because sound_play() routes to the BLOCKING sb16_play
// and would stall the cooperative kernel every frame.
//
// Keep this file as the starting point for real multi-channel mixing, or
// delete it.  If you do wire it up, declare the entry points in sound.h
// first - they are deliberately not declared there today so that nobody
// starts depending on an API that does not work.

#include "sound.h"

#include <stdint.h>


#define MAX_MIX_CHANNELS 16


typedef struct
{
    int active;

    const int16_t *buffer;

    uint32_t length;

    uint32_t position;

    int volume;

} MixerChannel;



static MixerChannel channels[MAX_MIX_CHANNELS];



void mixer_init(void)
{
    for (int i = 0; i < MAX_MIX_CHANNELS; i++)
    {
        channels[i].active = 0;
        channels[i].buffer = 0;
        channels[i].length = 0;
        channels[i].position = 0;
        channels[i].volume = 100;
    }
}



int mixer_play(
    int channel,
    const int16_t *buffer,
    uint32_t samples
)
{
    if (channel < 0 ||
        channel >= MAX_MIX_CHANNELS)
    {
        return -1;
    }


    channels[channel].buffer = buffer;
    channels[channel].length = samples;
    channels[channel].position = 0;
    channels[channel].volume = 100;
    channels[channel].active = 1;


    return 0;
}



void mixer_stop(int channel)
{
    if(channel < 0 ||
       channel >= MAX_MIX_CHANNELS)
        return;


    channels[channel].active = 0;
}



void mixer_mix(
    int16_t *output,
    uint32_t samples
)
{
    for(uint32_t i = 0; i < samples; i++)
    {
        int mix = 0;


        for(int c = 0;
            c < MAX_MIX_CHANNELS;
            c++)
        {
            MixerChannel *ch =
                &channels[c];


            if(!ch->active)
                continue;


            if(ch->position >= ch->length)
            {
                ch->active = 0;
                continue;
            }


            mix +=
                (ch->buffer[ch->position]
                * ch->volume) / 100;


            ch->position++;
        }


        // 防止溢出

        if(mix > 32767)
            mix = 32767;

        if(mix < -32768)
            mix = -32768;


        output[i] = (int16_t)mix;
    }
}