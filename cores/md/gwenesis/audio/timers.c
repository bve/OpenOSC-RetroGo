#include "ym2612.h"
#include <stdint.h>
#include <string.h>

/* YM2612 I/O and timers in master clock units. The PSG has no readable state. */
static struct {
    int clock, busy_until;
    uint32_t phase[2];
    uint16_t timer_a;
    uint8_t timer_b, address[2], control, status;
} fm;

static void update(int cycle)
{
    if (cycle <= fm.clock) return;
    const uint32_t ticks = cycle - fm.clock;
    fm.clock = cycle;
    const uint32_t periods[] = { (1024 - fm.timer_a) * 504, (256 - fm.timer_b) * 8064 };
    for (unsigned i = 0; i < 2; ++i) {
        if (!(fm.control & (1u << i))) continue;
        fm.phase[i] += ticks;
        if (fm.phase[i] >= periods[i]) {
            fm.phase[i] %= periods[i];
            if (fm.control & (4u << i)) fm.status |= 1u << i;
        }
    }
}

void YM2612ResetChip(void) { memset(&fm, 0, sizeof(fm)); }
void YM2612Init(void) { YM2612ResetChip(); }

void YM2612Write(unsigned port, unsigned value, int cycle)
{
    update(cycle);
    const unsigned bank = (port >> 1) & 1;
    if (!(port & 1)) {
        fm.address[bank] = value;
        return;
    }
    fm.busy_until = cycle + 224;
    if (bank) return;
    switch (fm.address[0]) {
    case 0x24: fm.timer_a = (fm.timer_a & 3) | ((value & 255) << 2); break;
    case 0x25: fm.timer_a = (fm.timer_a & 0x3fc) | (value & 3); break;
    case 0x26: fm.timer_b = value; break;
    case 0x27:
        for (unsigned i = 0; i < 2; ++i)
            if (!(fm.control & (1u << i)) && (value & (1u << i))) fm.phase[i] = 0;
        fm.status &= ~((value >> 4) & 3);
        fm.control = value;
        break;
    }
}

unsigned YM2612Read(int cycle)
{
    update(cycle);
    return fm.status | (cycle < fm.busy_until ? 128 : 0);
}

void md_end_audio_frame(int cycle)
{
    update(cycle);
    fm.clock -= cycle;
    fm.busy_until = fm.busy_until > cycle ? fm.busy_until - cycle : 0;
}
