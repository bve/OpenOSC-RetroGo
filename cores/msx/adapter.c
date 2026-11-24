#include "retrogo/core.h"
#include "baremsx/PPI.h"
#include "baremsx/diskrom.h"
#include "baremsx/mapper.h"
#include "baremsx/tms9918.h"
#include "baremsx/v9938.h"
#include "keyboard.h"
#include "z80.h"
#include <string.h>

enum { Width = 256, MaxHeight = 212, BankBytes = 8192, BankSlots = 6 };
static uint16_t pixels[Width * MaxHeight];
static uint8_t bios[32768], subrom[16384], disk_bios[16384], ram[65536];
static struct { uint8_t data[BankBytes]; unsigned bank; } cache[BankSlots];
static union {
    tms9918_context_t tms;
    struct { v9938_context_t vdp; uint8_t vram[V9938_VRAM_SIZE]; } msx2;
} video;
static z80 cpu;
static ppi_context_t ppi;
static mapper_t cartridge;
static RgKeyboard keyboard;
static diskrom_t drive;
static RgHost host;
static uint8_t slot, subslot, ram_page[4], psg[16], psg_addr, joystick;
static uint8_t rtc_select, rtc_mode, rtc_cmos[4][13];
static unsigned cache_next, height;
static bool msx2, disk, io_error;

static long scan_read(void *context, uint32_t start, uint8_t *out, uint32_t bytes)
{
    if (host.read_rom(context, start, out, bytes)) {
        io_error = true;
        return -1;
    }
    return bytes;
}

const uint8_t *msx_rom_bank(unsigned bank)
{
    for (unsigned i = 0; i < BankSlots; ++i)
        if (cache[i].bank == bank) return cache[i].data;
    const unsigned i = cache_next++ % BankSlots;
    if (io_error || scan_read(host.context, bank * BankBytes, cache[i].data, BankBytes) < 0)
        memset(cache[i].data, 0xff, BankBytes);
    cache[i].bank = bank;
    return cache[i].data;
}

static unsigned memory_page(uint16_t address)
{
    return address >> 14;
}

static uint8_t read_bus(void *context, uint16_t address)
{
    (void)context;
    const unsigned page = memory_page(address), shift = page * 2;
    switch ((slot >> shift) & 3) {
    case 0: return address < sizeof(bios) ? bios[address] : 0xff;
    case 1:
        return !disk && address >= 0x4000 && address < 0xc000 ?
               mapper_read(&cartridge, address) : 0xff;
    case 2: return disk && page == 1 ? diskrom_read(&drive, address) : 0xff;
    default:
        if (!msx2) return ram[address];
        if (address == 0xffff) return ~subslot;
        switch ((subslot >> shift) & 3) {
        case 0: return ram[ram_page[page] * 0x4000 + (address & 0x3fff)];
        case 1: return page == 0 ? subrom[address] : 0xff;
        default: return 0xff;
        }
    }
}

static void write_bus(void *context, uint16_t address, uint8_t value)
{
    (void)context;
    const unsigned page = memory_page(address), shift = page * 2;
    switch ((slot >> shift) & 3) {
    case 1:
        if (!disk) mapper_write(&cartridge, address, value);
        break;
    case 2:
        if (disk && page == 1) diskrom_write(&drive, address, value);
        break;
    case 3:
        if (!msx2) ram[address] = value;
        else if (address == 0xffff) subslot = value;
        else if (((subslot >> shift) & 3) == 0)
            ram[ram_page[page] * 0x4000 + (address & 0x3fff)] = value;
        break;
    }
}

static int disk_sector(void *context, uint32_t sector, uint8_t *out)
{
    (void)context;
    return scan_read(host.context, sector * WD_SECTOR_SIZE, out, WD_SECTOR_SIZE) < 0 ? -1 : 0;
}

static uint8_t rtc_read(void)
{
    if (rtc_select == 13) return 0xf0 | rtc_mode;
    if (rtc_select > 12) return 0xff;
    if (rtc_mode & 3) return 0xf0 | rtc_cmos[rtc_mode & 3][rtc_select];

    uint32_t seconds = host.seconds ? host.seconds(host.context) : 0;
    const uint8_t clock[] = {seconds % 60 % 10, seconds % 60 / 10,
                            seconds / 60 % 60 % 10, seconds / 60 % 60 / 10,
                            seconds / 3600 % 24 % 10, seconds / 3600 % 24 / 10,
                            2, 1, 0, 1, 0, 0, 0};
    return 0xf0 | clock[rtc_select];
}

static uint8_t in_port(z80 *core, uint8_t port)
{
    (void)core;
    switch (port) {
    case 0x98: return msx2 ? v9938_read_data(&video.msx2.vdp) : tms9918_read(&video.tms, false);
    case 0x99: return msx2 ? v9938_read_status(&video.msx2.vdp) : tms9918_read(&video.tms, true);
    case 0xa2: return psg_addr == 14 ? (psg[15] & 64 ? 0xff : (uint8_t)~joystick) : psg[psg_addr];
    case 0xa8: return slot;
    case 0xa9: return ppi_read_a9(&ppi);
    case 0xaa: return ppi.keyboard_row;
    case 0xb5: return msx2 ? rtc_read() : 0xff;
    default: return (msx2 && port >= 0xfc) ? 0xfc | ram_page[port - 0xfc] : 0xff;
    }
}

static void out_port(z80 *core, uint8_t port, uint8_t value)
{
    (void)core;
    switch (port) {
    case 0x98:
        if (msx2) v9938_write_data(&video.msx2.vdp, value);
        else tms9918_write(&video.tms, false, value);
        break;
    case 0x99:
        if (msx2) v9938_write_ctrl(&video.msx2.vdp, value);
        else tms9918_write(&video.tms, true, value);
        break;
    case 0x9a: if (msx2) v9938_write_palette(&video.msx2.vdp, value); break;
    case 0x9b: if (msx2) v9938_write_indirect(&video.msx2.vdp, value); break;
    case 0xa0: psg_addr = value & 15; break;
    case 0xa1: psg[psg_addr] = value; break;
    case 0xa8: slot = value; break;
    case 0xaa: ppi_write_aa(&ppi, value); break;
    case 0xab:
        if (value & 128) ppi_write_aa(&ppi, 0);
        else {
            const unsigned bit = (value >> 1) & 7;
            if (bit < 4) ppi_write_aa(&ppi, (ppi.keyboard_row & ~(1u << bit)) | ((value & 1) << bit));
        }
        break;
    case 0xb4: rtc_select = value & 15; break;
    case 0xb5:
        if (rtc_select == 13) rtc_mode = value & 15;
        else if (rtc_select < 13) rtc_cmos[rtc_mode & 3][rtc_select] = value & 15;
        break;
    default: if (msx2 && port >= 0xfc) ram_page[port - 0xfc] = value & 3; break;
    }
}

static int valid_cartridge(unsigned bytes)
{
    if (bytes < BankBytes || bytes > 512 * 1024 || bytes % BankBytes) return 0;
    const uint8_t *header = msx_rom_bank(0);
    return header[0] == 'A' && header[1] == 'B';
}

enum RgResult rg_open(const RgHost *services, uint32_t bytes)
{
    if (!services || !services->read_rom || !services->set_video || !services->extension ||
        !bytes) return RG_BAD_ROM;
    host = *services;
    disk = !strcmp(host.extension, "dsk");
    const bool plus = !strcmp(host.extension, "mxp");
    msx2 = disk || plus || !strcmp(host.extension, "mx2");
    const unsigned sectors = bytes / WD_SECTOR_SIZE;
    if (disk && (bytes % WD_SECTOR_SIZE || (sectors != 720 && sectors != 1440)))
        return RG_UNSUPPORTED_ROM;

    io_error = false;
    cache_next = 0;
    for (unsigned i = 0; i < BankSlots; ++i) cache[i].bank = UINT32_MAX;
    if (!disk) {
        const int valid = valid_cartridge(bytes);
        if (io_error) return RG_ROM_IO;
        if (!valid) return RG_BAD_ROM;
        const mapper_type_t type = mapper_detect_stream(host.context, scan_read, bytes);
        if (io_error) return RG_ROM_IO;
        mapper_init(&cartridge, bytes, type);
    } else {
        uint8_t probe;
        if (scan_read(host.context, 0, &probe, 1) < 0) return RG_ROM_IO;
    }
    if (!host.read_bios) return RG_MISSING_BIOS;
    const char *system_bios = plus ? "msx2plus.rom" : msx2 ? "msx2.rom" : "msx.rom";
    enum RgResult result = host.read_bios(host.context, system_bios, bios, sizeof(bios));
    if (result != RG_OK) return result;
    if (msx2) {
        const char *extension_bios = plus ? "msx2plus-ext.rom" : "msx2-ext.rom";
        result = host.read_bios(host.context, extension_bios, subrom, sizeof(subrom));
        if (result != RG_OK) return result;
    }
    if (disk) {
        result = host.read_bios(host.context, "msx-disk.rom", disk_bios, sizeof(disk_bios));
        if (result != RG_OK) return result;
        drive.rom = disk_bios;
        drive.rom_size = sizeof(disk_bios);
        wd2793_init(&drive.fdc, sectors == 720 ? 1 : 2, sectors, NULL, disk_sector);
    }

    slot = subslot = psg_addr = joystick = rtc_select = rtc_mode = 0;
    for (unsigned i = 0; i < 4; ++i) ram_page[i] = 3 - i;
    memset(ram, 0, sizeof(ram));
    memset(&ppi, 0, sizeof(ppi));
    memset(psg, 0, sizeof(psg));
    memset(rtc_cmos, 0, sizeof(rtc_cmos));
    memset(&video, 0, sizeof(video));
    if (msx2) v9938_init(&video.msx2.vdp, video.msx2.vram, plus);
    z80_init(&cpu);
    cpu.read_byte = read_bus;
    cpu.write_byte = write_bus;
    cpu.port_in = in_port;
    cpu.port_out = out_port;
    keyboard = (RgKeyboard){0};
    height = msx2 ? MaxHeight : 192;
    const RgVideoMode mode = { Width, height, 50000, 3 };
    return host.set_video(host.context, mode) ? RG_BAD_ROM : RG_OK;
}

static void update_input(uint32_t actions)
{
    const int key = rg_keyboard_input(&keyboard, actions);
    memset(ppi.keydown, 0, sizeof(ppi.keydown));
    joystick = keyboard.visible ? 0 : actions & 63;
    static const char matrix[] =
        "01234567" "89-^\\@[;" ":],./_ab" "cdefghij" "klmnopqr" "stuvwxyz";
    const char *where = key ? strchr(matrix, key) : NULL;
    if (where) ppi_on_keydown(&ppi, where - matrix);
    else if (key == '"') {
        ppi_on_keydown(&ppi, 6 * 8);
        ppi_on_keydown(&ppi, 2);
    } else if (key == 8) ppi_on_keydown(&ppi, 7 * 8 + 5);
    else if (key == 13) ppi_on_keydown(&ppi, 7 * 8 + 7);
    else if (key == ' ') ppi_on_keydown(&ppi, 8 * 8);
}

static uint16_t rgb565(uint32_t rgb)
{
    return ((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 31);
}

enum RgResult rg_run(uint32_t actions, const uint16_t **frame)
{
    update_input(actions);
    const unsigned lines = msx2 && (video.msx2.vdp.regs[9] & 2) ? 313 : 262;
    unsigned long deadline = cpu.cyc;
    for (unsigned y = 0; y < lines && !io_error; ++y) {
        if (msx2) v9938_line_start(&video.msx2.vdp);
        deadline += 228;
        while ((long)(deadline - cpu.cyc) > 0) {
            const bool irq = msx2 ? v9938_irq_asserted(&video.msx2.vdp) :
                             (video.tms.vdpStatus & 0x80) && (video.tms.registers[1] & 0x20);
            cpu.int_pending = irq;
            cpu.int_data = 0xff;
            z80_step(&cpu);
        }
        if (msx2) v9938_scanline(&video.msx2.vdp, y);
        else if (y == 192) check_and_generate_interrupt(&video.tms);
        if (y >= height) continue;
        uint16_t *destination = pixels + y * Width;
        if (msx2) {
            uint16_t line[512];
            const unsigned width = v9938_render_line_565(&video.msx2.vdp, line, y);
            if (width == Width) {
                memcpy(destination, line, Width * sizeof(*destination));
            } else {
                for (unsigned x = 0; x < Width; ++x) destination[x] = line[x * 2];
            }
        } else {
            uint32_t line[Width];
            tms9918_render_line(&video.tms, line, y);
            for (unsigned x = 0; x < Width; ++x) destination[x] = rgb565(line[x]);
        }
    }
    if (io_error) return RG_ROM_IO;
    rg_keyboard_draw(&keyboard, pixels, Width, height);
    *frame = pixels;
    return RG_OK;
}
