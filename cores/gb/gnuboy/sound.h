#ifndef __SOUND_H__
#define __SOUND_H__
#include "defs.h"

struct snd {
    uint32_t cycles, step;
    uint16_t length[4], sweep_shadow;
    byte active, sweep_timer, sweep_enabled, sweep_subtracted;
};

extern struct snd snd;

void sound_write(byte r, byte b);
byte sound_read(byte r);
void sound_reset(void);
void sound_clock(void);

#endif
