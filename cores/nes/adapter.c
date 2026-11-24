#include "retrogo/core.h"
#include "nofrendo.h"
#include "nes_input.h"
#include "osd.h"
#include <string.h>

enum { Header = 16, Trainer = 512, MaxCart = 256 * 1024 + Header + Trainer };
static _Alignas(4) uint8_t image[MaxCart];
static uint8_t sram[8 * SRAM_BANK_LENGTH], vram[VRAM_BANK_LENGTH];
static uint16_t palette[256];
static rominfo_t cartridge;
static unsigned save_bytes;

void nes_renderframe(void);
uint16_t *nes_rgb565_buffer(void);

void osd_setpalette(rgb_t *colors)
{
    for (unsigned i = 0; i < 256; ++i)
        palette[i] = ((colors[i].r & 0xf8) << 8) | ((colors[i].g & 0xfc) << 3) |
                     (colors[i].b >> 3);
}

rominfo_t *rom_load(const char *name)
{
    (void)name;
    return &cartridge;
}

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || !host->set_video || bytes < Header) return RG_BAD_ROM;
    if (bytes > sizeof(image)) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, 0, image, Header)) return RG_ROM_IO;
    if (memcmp(image, "NES\x1a", 4) || image[4] == 0) return RG_BAD_ROM;
    if ((image[7] & 12) == 8) return RG_UNSUPPORTED_ROM;
    const unsigned prg = image[4] * ROM_BANK_LENGTH, chr = image[5] * VROM_BANK_LENGTH;
    const unsigned header = Header + ((image[6] & ROM_FLAG_TRAINER) ? Trainer : 0);
    if (header + prg + chr != bytes) return RG_BAD_ROM;

    const int mapper = (image[6] >> 4) |
                       ((image[12] | image[13] | image[14] | image[15]) == 0 ? (image[7] & 0xf0) : 0);
    if (!mmc_peek(mapper)) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, Header, image + Header, bytes - Header)) return RG_ROM_IO;
    memset(sram, 0, sizeof(sram));
    if (header > Header) memcpy(sram + TRAINER_OFFSET, image + Header, Trainer);
    const bool pal = (image[9] & 1) != 0;
    cartridge = (rominfo_t){
        .rom = image + header, .vrom = chr ? image + header + prg : NULL,
        .sram = sram, .vram = vram, .rom_banks = image[4], .vrom_banks = image[5],
        .sram_banks = sizeof(sram) / SRAM_BANK_LENGTH, .vram_banks = 1,
        .mapper_number = mapper, .flags = image[6] & 15
    };
    save_bytes = cartridge.flags & ROM_FLAG_BATTERY ? sizeof(sram) : 0;
    if (!nes_init(pal ? NES_PAL : NES_NTSC, 24000, false) || !nes_insertcart("", NULL, 0))
        return RG_UNSUPPORTED_ROM;
    const RgVideoMode mode = {
        NES_SCREEN_WIDTH, NES_SCREEN_HEIGHT, pal ? 19997209 : 16639263, 1000
    };
    return host->set_video(host->context, mode) ? RG_BAD_ROM : RG_OK;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **out)
{
    static const uint8_t inputs[] = {
        INP_PAD_UP, INP_PAD_DOWN, INP_PAD_LEFT, INP_PAD_RIGHT,
        INP_PAD_A, INP_PAD_B, INP_PAD_START, INP_PAD_SELECT
    };
    uint8_t pad = 0;
    for (unsigned i = 0; i < sizeof(inputs); ++i)
        if (buttons & (1u << i)) pad |= inputs[i];

    input_update(INP_JOYPAD0, pad);
    nes_renderframe();

    bitmap_t *frame = nes_getptr()->vidbuf;
    uint8_t *indexed = frame->data;
    for (unsigned y = 0; y < NES_SCREEN_HEIGHT; ++y)
        memmove(indexed + y * NES_SCREEN_WIDTH, frame->line[y], NES_SCREEN_WIDTH);
    uint16_t *pixels = nes_rgb565_buffer();
    for (unsigned i = NES_SCREEN_WIDTH * NES_SCREEN_HEIGHT; i > 0; --i)
        pixels[i - 1] = palette[indexed[i - 1]];
    *out = pixels;
    return RG_OK;
}

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind != RG_SRAM || !save_bytes) return 0;
    if (capacity >= save_bytes) memcpy(out, sram, save_bytes);
    return save_bytes;
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t bytes)
{
    if (kind != RG_SRAM || !save_bytes || bytes != save_bytes) return RG_BAD_SAVE;
    memcpy(sram, data, bytes);
    return RG_OK;
}
