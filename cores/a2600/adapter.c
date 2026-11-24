#include "retrogo/core.h"
#include "tia/bus.h"
#include <string.h>

enum { NtscHeight = 228, PalHeight = 240, MaxSteps = 50000 };
static struct cpu cpu;
static struct tia tia;
static struct bus bus;
static struct riot riot;
static struct cart cart;
static uint8_t rom[CART_MAX_SIZE];
static unsigned first_line;

static int run_frame(void)
{
    unsigned steps = 0;
    while (!tia.frame_ready && !cpu.halted && steps++ < MaxSteps)
        cpu_step(&cpu);
    const int ready = tia.frame_ready;
    tia.frame_ready = false;
    return ready;
}

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || !host->set_video || bytes < 1024) return RG_BAD_ROM;
    if (bytes > sizeof(rom)) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, 0, rom, bytes)) return RG_ROM_IO;
    if (!cart_load(&cart, rom, bytes)) return RG_UNSUPPORTED_ROM;

    tia_init(&tia);
    riot_init(&riot);
    bus_init(&bus, &cpu, &tia, &riot, &cart);
    cpu_init(&cpu, (struct cpu_bus){ bus_read, bus_write, &bus });
    cart.cpu_cycles = &cpu.cycles;
    cpu_reset(&cpu);
    for (unsigned i = 0; i < TIA_DETECT_FRAMES + 2; ++i)
        if (!run_frame()) return RG_BAD_ROM;

    const bool pal = tia.detect_locked && tia.detected_region == TIA_REGION_PAL;
    tia_set_region(&tia, pal ? TIA_REGION_PAL : TIA_REGION_NTSC);
    first_line = pal ? 58 : 34;
    const RgVideoMode mode = { TIA_OUTPUT_WIDTH, pal ? PalHeight : NtscHeight,
                               1000000, pal ? 50 : 60 };
    return host->set_video(host->context, mode) ? RG_BAD_ROM : RG_OK;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **pixels)
{
    static const uint8_t direction[] = { 0x10, 0x20, 0x40, 0x80 };
    uint8_t held = 0;
    for (unsigned i = 0; i < sizeof(direction); ++i)
        if (buttons & (1u << i)) held |= direction[i];
    riot.pa_in = ~held;
    riot.pb_in = 0xff;
    if (buttons & (1u << RG_START)) riot.pb_in &= ~1u;
    if (buttons & (1u << RG_SELECT)) riot.pb_in &= ~2u;
    tia.inpt[4] = buttons & ((1u << RG_A) | (1u << RG_B)) ? 0 : 0x80;
    if (!run_frame()) return RG_BAD_ROM;
    *pixels = tia.fb + first_line * TIA_OUTPUT_WIDTH;
    return RG_OK;
}
