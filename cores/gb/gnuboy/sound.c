/* Game Boy APU register state and frame sequencer. There is no audio output. */
#include "sound.h"
#include "hw.h"
#include "regs.h"

struct snd snd;

static void sweep(void)
{
    const byte period = (R_NR10 >> 4) & 7;
    if (snd.sweep_timer && --snd.sweep_timer) return;
    snd.sweep_timer = period ? period : 8;
    if (!snd.sweep_enabled || !period) return;
    const byte shift = R_NR10 & 7;
    if (shift == 0) return;
    const int frequency = snd.sweep_shadow;
    const int delta = frequency >> shift;
    const int next = R_NR10 & 8 ? frequency - delta : frequency + delta;
    if (R_NR10 & 8) snd.sweep_subtracted = 1;
    if (next > 2047) {
        snd.active &= ~1u;
        return;
    }
    snd.sweep_shadow = next;
    R_NR13 = next & 255;
    R_NR14 = (R_NR14 & 0xf8) | (next >> 8);
    if (!(R_NR10 & 8) && next + (next >> shift) > 2047) snd.active &= ~1u;
}

void sound_clock(void)
{
    if (!(R_NR52 & 128)) {
        snd.cycles = 0;
        return;
    }
    while (snd.cycles >= 4096) {
        snd.cycles -= 4096;
        if ((snd.step & 1) == 0) {
            const byte control[] = { R_NR14, R_NR24, R_NR34, R_NR44 };
            for (unsigned i = 0; i < 4; ++i)
                if ((control[i] & 64) && snd.length[i] && --snd.length[i] == 0)
                    snd.active &= ~(1u << i);
        }
        if ((snd.step & 3) == 2) sweep();
        snd.step = (snd.step + 1) & 7;
    }
}

byte sound_read(byte r)
{
    static const byte unused[0x17] = {
        0x80, 0x3f, 0, 0xff, 0xbf, 0xff, 0x3f, 0, 0xff, 0xbf, 0x7f, 0xff,
        0x9f, 0xff, 0xbf, 0xff, 0xff, 0, 0, 0xbf, 0, 0, 0x70
    };
    sound_clock();
    if (r == RI_NR52) return 0x70 | (R_NR52 & 128) | snd.active;
    if (r >= 0x30) return hw.cgb || !(snd.active & 4) ? REG(r) : 0xff;
    if (r > RI_NR52) return 0xff;
    return REG(r) | unused[r - RI_NR10];
}

static void trigger(unsigned channel)
{
    if (!snd.length[channel]) snd.length[channel] = channel == 2 ? 256 : 64;
    const byte dac[] = { R_NR12 & 0xf8, R_NR22 & 0xf8, R_NR30 & 0x80, R_NR42 & 0xf8 };
    if (dac[channel]) snd.active |= 1u << channel;
    if (channel != 0) return;
    const byte period = (R_NR10 >> 4) & 7, shift = R_NR10 & 7;
    snd.sweep_shadow = ((R_NR14 & 7) << 8) | R_NR13;
    snd.sweep_timer = period ? period : 8;
    snd.sweep_enabled = period || shift;
    snd.sweep_subtracted = 0;
    if (shift && !(R_NR10 & 8) && snd.sweep_shadow + (snd.sweep_shadow >> shift) > 2047)
        snd.active &= ~1u;
}

void sound_write(byte r, byte b)
{
    sound_clock();
    if (r >= 0x30) {
        if (hw.cgb || !(snd.active & 4)) REG(r) = b;
        return;
    }
    if (r == RI_NR52) {
        if ((R_NR52 & 128) && !(b & 128)) {
            memset(ram.hi + 0x10, 0, 0x17);
            memset(&snd, 0, sizeof(snd));
        }
        R_NR52 = b & 128;
        return;
    }
    if (!(R_NR52 & 128) || r > RI_NR52) return;
    REG(r) = b;
    switch (r) {
    case RI_NR10: if (!(b & 8) && snd.sweep_subtracted) snd.active &= ~1u; break;
    case RI_NR11: snd.length[0] = 64 - (b & 63); break;
    case RI_NR21: snd.length[1] = 64 - (b & 63); break;
    case RI_NR31: snd.length[2] = 256 - b; break;
    case RI_NR41: snd.length[3] = 64 - (b & 63); break;
    case RI_NR12: if (!(b & 0xf8)) snd.active &= ~1u; break;
    case RI_NR22: if (!(b & 0xf8)) snd.active &= ~2u; break;
    case RI_NR30: if (!(b & 0x80)) snd.active &= ~4u; break;
    case RI_NR42: if (!(b & 0xf8)) snd.active &= ~8u; break;
    case RI_NR14: if (b & 128) trigger(0); break;
    case RI_NR24: if (b & 128) trigger(1); break;
    case RI_NR34: if (b & 128) trigger(2); break;
    case RI_NR44: if (b & 128) trigger(3); break;
    }
}

void sound_reset(void)
{
    static const byte dmg_wave[16] = {
        0xac, 0xdd, 0xda, 0x48, 0x36, 0x02, 0xcf, 0x16,
        0x2c, 0x04, 0xe5, 0x2c, 0xac, 0xdd, 0xda, 0x48
    };
    memset(&snd, 0, sizeof(snd));
    memcpy(ram.hi + 0x30, dmg_wave, 16);
    if (hw.cgb)
        for (unsigned i = 0; i < 16; ++i) REG(0x30 + i) = i & 1 ? 255 : 0;
    R_NR12 = 0xf3;
    R_NR50 = 0x77;
    R_NR51 = 0xf3;
    R_NR52 = 0x80;
    snd.active = 1;
    snd.length[0] = 1;
}
