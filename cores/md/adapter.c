#include "retrogo/core.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "m68k.h"
#include "ym2612.h"
#include "z80inst.h"
#include <string.h>
#include <stdbool.h>

enum { Width = 320, Height = 240, Page = 1024, MaxPages = 4096, LocalSlots = 16, Uncached = 255 };
struct CachePage { uint8_t bytes[Page]; unsigned number, accessed; };
static uint16_t pixels[Width * Height];
static struct CachePage local_cache[LocalSlots];
static struct CachePage *cache;
static uint8_t resident[MaxPages];
static unsigned cache_slots, access_count, rom_bytes;
static const uint8_t *fast;
static RgHost host;
static unsigned active_page;
static bool io_error;
static uint32_t input;
int scan_line, lines_per_frame, system_clock;
extern int screen_height, screen_width, hint_pending;
static int hint_counter;
extern unsigned short gwenesis_vdp_status;
extern unsigned char gwenesis_vdp_regs[];
void md_end_m68k_frame(unsigned cycles);

static void reset_cache(const RgHostV3 *services)
{
    cache = local_cache;
    cache_slots = LocalSlots;
    const unsigned slots = services->workspace_bytes / sizeof(*cache);
    if (services->workspace && slots > LocalSlots &&
        (uintptr_t)services->workspace % _Alignof(struct CachePage) == 0) {
        cache = services->workspace;
        cache_slots = slots < Uncached ? slots : Uncached;
    }
    access_count = 0;
    active_page = UINT32_MAX;
    memset(resident, Uncached, sizeof(resident));
    for (unsigned i = 0; i < cache_slots; ++i) {
        cache[i].number = UINT32_MAX;
        cache[i].accessed = 0;
    }
}

static const uint8_t *load_page(unsigned index)
{
    if (index >= MaxPages) return NULL;
    const unsigned start = index * Page;
    if (start >= rom_bytes) return NULL;

    const unsigned now = ++access_count;
    unsigned slot = resident[index];
    if (slot != Uncached) {
        cache[slot].accessed = now;
        return cache[slot].bytes;
    }

    slot = 0;
    for (unsigned i = 1; i < cache_slots; ++i)
        if (now - cache[i].accessed > now - cache[slot].accessed) slot = i;
    struct CachePage *page = &cache[slot];
    if (page->number != UINT32_MAX) resident[page->number] = Uncached;

    unsigned length = rom_bytes - start;
    if (length > Page) length = Page;
    if (io_error || host.read_rom(host.context, start, page->bytes, length)) {
        io_error = true;
        memset(page->bytes, 0xff, Page);
    } else if (length < Page) {
        memset(page->bytes + length, 0xff, Page - length);
    }
    page->number = index;
    page->accessed = now;
    resident[index] = slot;
    return page->bytes;
}

unsigned md_rom_read8(unsigned address)
{
    const unsigned page = address / Page;
    if (active_page != page) {
        fast = load_page(page);
        active_page = page;
    }
    return fast ? fast[address & (Page - 1)] : 0xff;
}

unsigned md_rom_read16(unsigned address)
{
    if ((address & (Page - 1)) == Page - 1) {
        const unsigned high = md_rom_read8(address);
        return (high << 8) | md_rom_read8(address + 1);
    }
    const unsigned page = address / Page;
    if (active_page != page) {
        fast = load_page(page);
        active_page = page;
    }
    if (!fast) return 0xffff;
    const uint8_t *p = fast + (address & (Page - 1));
    return (p[0] << 8) | p[1];
}

void gwenesis_io_get_buttons(void)
{
    static const uint8_t pad[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_S, 255, PAD_C };
    for (unsigned i = 0; i < sizeof(pad); ++i) {
        if (pad[i] == 255) continue;
        if (input & (1u << i)) gwenesis_io_pad_press_button(0, pad[i]);
        else gwenesis_io_pad_release_button(0, pad[i]);
    }
}

enum RgResult rg_open(const RgHost *services, uint32_t bytes)
{
    if (!services || !services->read_rom || !services->set_video || bytes < 512 || (bytes & 1))
        return RG_BAD_ROM;
    if (bytes > MaxPages * Page) return RG_UNSUPPORTED_ROM;
    host = *services;
    rom_bytes = bytes;
    reset_cache((const RgHostV3 *)services);
    io_error = false;
    const uint8_t *first = load_page(0);
    if (io_error) return RG_ROM_IO;
    if (memcmp(first + 0x100, "SEGA", 4)) return RG_BAD_ROM;

    input = scan_line = system_clock = 0;
    power_on();
    load_cartridge();
    const bool pal = (gwenesis_io_read_ctrl(0) & 0x40) != 0;
    lines_per_frame = pal ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
    reset_emulation();

    const RgVideoMode mode = { Width, Height, 1000000, pal ? 50 : 60 };
    return host.set_video(host.context, mode) ? RG_BAD_ROM : RG_OK;
}

enum RgResult rg_run(uint32_t actions, const uint16_t **out)
{
    input = actions;
    screen_height = REG1_PAL ? 240 : 224;
    screen_width = REG12_MODE_H40 ? 320 : 256;
    system_clock = 0;
    memset(pixels, 0, sizeof(pixels));
    gwenesis_vdp_set_buffer(pixels + ((Height - screen_height) / 2) * Width);
    gwenesis_vdp_render_config();
    for (scan_line = 0; scan_line < lines_per_frame && !io_error; ++scan_line) {
        const int end = system_clock + VDP_CYCLES_PER_LINE;
        m68k_run(end);
        z80_run(end);
        if (scan_line < screen_height) gwenesis_vdp_render_line(scan_line);
        if (scan_line == 0 || scan_line >= screen_height) hint_counter = REG10_LINE_COUNTER;
        if (--hint_counter < 0) {
            if (REG0_LINE_INTERRUPT && scan_line <= screen_height) {
                hint_pending = 1;
                if (!(gwenesis_vdp_status & STATUS_VIRQPENDING)) m68k_update_irq(4);
            }
            hint_counter = REG10_LINE_COUNTER;
        }
        if (scan_line + 1 == screen_height) {
            if (REG1_VBLANK_INTERRUPT) {
                gwenesis_vdp_status |= STATUS_VIRQPENDING;
                m68k_set_irq(6);
            }
            z80_irq_line(1);
        } else if (scan_line == screen_height)
            z80_irq_line(0);
        system_clock = end;
    }
    if (io_error) return RG_ROM_IO;
    md_end_m68k_frame(system_clock);
    md_end_z80_frame(system_clock);
    md_end_audio_frame(system_clock);
    *out = pixels;
    return RG_OK;
}
