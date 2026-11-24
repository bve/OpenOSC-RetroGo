#include "retrogo/core.h"
#include <string.h>

#define CHIPS_IMPL
#define CHIPS_ASSERT(condition) ((void)0)
#include "vendor/chips/chips_common.h"
#include "vendor/chips/clk.h"
#include "vendor/chips/z80.h"
#include "vendor/chips/ay38910.h"
#include "vendor/chips/i8255.h"
#include "vendor/chips/mc6845.h"
#include "vendor/chips/upd765.h"
#include "vendor/chips/am40010.h"
#include "vendor/chips/mem.h"
#include "vendor/chips/kbd.h"
#include "vendor/chips/fdd.h"
#include "vendor/systems/cpc.h"
#include "keyboard.h"

enum { HeaderSize = 256, SectorTable = 24, FrameTime = 19968 };
static cpc_t console;
static RgKeyboard keyboard;
static int key;

static unsigned word_le(const uint8_t *buffer)
{
    return buffer[0] | ((unsigned)buffer[1] << 8);
}

static enum RgResult mount_disc(const RgHost *host, unsigned bytes)
{
    uint8_t header[HeaderSize], track_header[HeaderSize];
    if (host->read_rom(host->context, 0, header, sizeof(header))) return RG_ROM_IO;
    const bool extended = memcmp(header, "EXTENDED CPC DSK File", 21) == 0;
    if (!extended && memcmp(header, "MV - CPCEMU Disk-File", 21)) return RG_BAD_ROM;
    const unsigned tracks = header[48], sides = header[49];
    if (!tracks || tracks > FDD_MAX_TRACKS || !sides || sides > FDD_MAX_SIDES)
        return RG_UNSUPPORTED_ROM;
    fdd_t *drive = &console.fdd;
    fdd_disc_t *disc = &drive->disc;
    disc->num_sides = sides;
    disc->num_tracks = tracks;
    disc->write_protected = true;

    unsigned start = HeaderSize;
    for (unsigned t = 0; t < tracks; ++t) {
        for (unsigned h = 0; h < sides; ++h) {
            const unsigned size = extended ? header[52 + t * sides + h] * 256 : word_le(header + 50);
            if (!size) continue;
            if (size < HeaderSize || start > bytes || size > bytes - start) return RG_BAD_ROM;
            if (host->read_rom(host->context, start, track_header, sizeof(track_header))) return RG_ROM_IO;
            if (memcmp(track_header, "Track-Info", 10)) return RG_BAD_ROM;
            fdd_track_t *track = &disc->tracks[h][t];
            const unsigned sectors = track_header[21];
            if (sectors > FDD_MAX_SECTORS || (!extended && track_header[20] > 2))
                return RG_UNSUPPORTED_ROM;

            track->num_sectors = sectors;
            track->data_offset = start + HeaderSize;
            track->data_size = size - HeaderSize;
            unsigned offset = track->data_offset;
            for (unsigned i = 0; i < sectors; ++i) {
                const uint8_t *info = track_header + SectorTable + i * 8;
                const unsigned count = extended ? word_le(info + 6) : 128u << track_header[20];
                if (count > FDD_MAX_SECTOR_SIZE) return RG_UNSUPPORTED_ROM;
                if (count > start + size - offset) return RG_BAD_ROM;
                fdd_sector_t *sector = &track->sectors[i];
                memcpy(sector->info.raw, info, sizeof(sector->info.raw));
                sector->data_offset = offset;
                sector->data_size = count;
                offset += count;
            }
            start += size;
        }
    }
    drive->has_disc = disc->formatted = true;
    drive->context = host->context;
    drive->read = host->read_rom;
    drive->cached_offset = -1;
    return RG_OK;
}

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || bytes < HeaderSize) return RG_BAD_ROM;
    if (bytes > 2 * 1024 * 1024) return RG_UNSUPPORTED_ROM;
    cpc_init(&console, &(cpc_desc_t){ .type = CPC_TYPE_6128, .joystick_type = CPC_JOYSTICK_NONE });
    const enum RgResult mounted = mount_disc(host, bytes);
    if (mounted != RG_OK) return mounted;
    if (!host->read_bios) return RG_MISSING_BIOS;

    static const char *const names[] = { "cpc6128-os.rom", "cpc6128-basic.rom", "amsdos.rom" };
    uint8_t *const images[] = { console.rom_os, console.rom_basic, console.rom_amsdos };
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        const enum RgResult result = host->read_bios(host->context, names[i], images[i], 16384);
        if (result != RG_OK) return result;
    }
    cpc_reset(&console);
    keyboard = (RgKeyboard){0};
    key = 0;
    return RG_OK;
}

static void update_input(uint32_t buttons)
{
    int next_key = rg_keyboard_input(&keyboard, buttons);
    const uint8_t joystick = keyboard.visible ? 0 : buttons & 31;
    if (next_key == 8) next_key = 1; /* CPC Delete. */
    if (!keyboard.visible && (buttons & (1u << RG_B))) next_key = ' ';
    if (key != next_key) {
        if (key) cpc_key_up(&console, key);
        if (next_key) cpc_key_down(&console, next_key);
        key = next_key;
    }
    cpc_joystick(&console, joystick);
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **pixels)
{
    update_input(buttons);
    cpc_exec(&console, FrameTime);
    if (console.fdd.fault) return RG_ROM_IO;
    rg_keyboard_draw(&keyboard, console.fb, 320, 240);
    *pixels = console.fb;
    return RG_OK;
}
