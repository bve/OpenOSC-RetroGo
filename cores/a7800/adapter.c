#include "retrogo/core.h"
#include "prosystem/Cartridge.h"
#include "prosystem/Database.h"
#include "prosystem/Maria.h"
#include "prosystem/Memory.h"
#include "prosystem/Palette.h"
#include "prosystem/ProSystem.h"
#include "prosystem/Region.h"

static uint8_t cartridge[147584];
static uint16_t palette[256];
static unsigned height, top, source_height;

enum RgResult rg_open(const RgHost *host, uint32_t size)
{
    if (!host || !host->read_rom || !host->set_video || size < 1024) return RG_BAD_ROM;
    if (size > sizeof(cartridge)) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, 0, cartridge, size)) return RG_ROM_IO;
    if (!cartridge_Load(cartridge, size)) return RG_BAD_ROM;
    database_Load(cartridge_digest);

    const unsigned payload = size - (memcmp(cartridge + 1, "ATARI7800", 9) == 0 ? 128 : 0);
    unsigned minimum = 0, maximum = 147456;
    switch (cartridge_type) {
    case CARTRIDGE_TYPE_NORMAL: maximum = 49152; break;
    case CARTRIDGE_TYPE_SUPERCART:
    case CARTRIDGE_TYPE_SUPERCART_RAM:
    case CARTRIDGE_TYPE_SUPERCART_ROM: minimum = 65536; break;
    case CARTRIDGE_TYPE_SUPERCART_LARGE: minimum = 147456; break;
    case CARTRIDGE_TYPE_ABSOLUTE: minimum = 65536; break;
    case CARTRIDGE_TYPE_ACTIVISION: minimum = 131072; break;
    default: return RG_UNSUPPORTED_ROM;
    }
    if (payload < minimum || payload > maximum ||
        (cartridge_type != CARTRIDGE_TYPE_NORMAL && payload % 16384))
        return RG_UNSUPPORTED_ROM;

    prosystem_Reset();
    source_height = maria_visibleArea.bottom - maria_visibleArea.top;
    height = source_height < MariaHeight ? source_height : MariaHeight;
    top = maria_visibleArea.top - maria_displayArea.top;
    const RgVideoMode mode = { MariaWidth, height, 1000000, prosystem_frequency };
    if (host->set_video(host->context, mode)) return RG_BAD_ROM;
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t *c = palette_data + i * 3;
        palette[i] = ((c[0] & 0xf8) << 8) | ((c[1] & 0xfc) << 3) | (c[2] >> 3);
    }
    return RG_OK;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **frame)
{
    uint8_t keys[17] = {0};
    static const uint8_t map[] = { 3, 2, 1, 0, 5, 4, 14, 13 };
    for (unsigned i = 0; i < sizeof(map); ++i)
        keys[map[i]] = (buttons >> i) & 1;

    maria_Clear();
    prosystem_ExecuteFrame(keys);
    /* Compact visible lines in place before expanding indexed pixels to RGB565. */
    for (unsigned y = 0; y < height; ++y)
        memmove(maria_surface + MariaWidth * y,
                maria_surface + MariaWidth * (top + y * source_height / height), MariaWidth);
    for (unsigned i = MariaWidth * height; i > 0; --i)
        maria_output.rgb565[i - 1] = palette[maria_surface[i - 1]];
    *frame = maria_output.rgb565;
    return RG_OK;
}
