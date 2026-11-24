/* Loadable port of O2EM under the accompanying Artistic License 2.0. */
#include "retrogo/core.h"
#include "o2em/audio.h"
#include "o2em/cpu.h"
#include "o2em/keys.h"
#include "o2em/o2em_config.h"
#include "o2em/vdc.h"
#include "o2em/vmachine.h"
#include "o2em/voice.h"
#include <string.h>

enum { BiosBytes = 1024, CartBytes = 24576 };
static uint8_t cartridge[CartBytes];
static uint32_t previous;
static unsigned digit;
uint8_t cset[512], vpp_cset[2][1280];
int joystick_data[2][5];
int RLOOP, sound_IRQ;

extern uint8_t key[512];
const uint16_t *o2_framebuffer(void);
void o2_begin_frame(void);
void o2_number(unsigned number);

static uint32_t crc32(const uint8_t *data, unsigned bytes)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < bytes; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0);
    }
    return ~crc;
}

void update_joy(void) {}
void update_voice(void) {}
void reset_voice(void) {}
void set_voice_bank(int bank) { (void)bank; }
void trigger_voice(int address) { (void)address; }
int get_voice_status(void) { return 0; }

void update_audio(void)
{
    if (!(VDCwrite[0xa0] & 4) || sound_IRQ) return;
    const unsigned first = tweakedaudio ? 0 : MAXLINES - 1;
    const unsigned last = tweakedaudio ? SOUND_BUFFER_LEN / 3 : MAXLINES;
    for (unsigned i = first; i < last; ++i) {
        if (AudioVector[i] & 0x80) {
            sound_IRQ = 1;
            ext_IRQ();
            return;
        }
    }
}

static enum RgResult load_cart(unsigned bytes)
{
    app_data.crc = crc32(cartridge, bytes);
    app_data.exrom = app_data.crc == 0xafb23f89 || app_data.crc == 0x3bfef56b ||
                     app_data.crc == 0x9b5e9356;
    const unsigned block = bytes % 3072 == 0 ? 3072 : 2048;
    const unsigned count = bytes / block;
    if (bytes % block || (count != 1 && count != 2 && count != 4 && count != 8))
        return RG_UNSUPPORTED_ROM;
    app_data.three_k = block == 3072;
    if (app_data.exrom && bytes == 4096) {
        memcpy(extROM, cartridge, 1024);
        memcpy(rom_table[0] + BiosBytes, cartridge + 1024, 3072);
        app_data.bank = 1;
    } else {
        for (unsigned i = 0; i < count; ++i) {
            uint8_t *dest = rom_table[count - i - 1] + BiosBytes;
            memcpy(dest, cartridge + i * block, block);
            if (block == 2048) memcpy(dest + 2048, dest + 1024, 1024);
        }
        app_data.bank = count == 1 ? 1 : count == 2 ? 2 : count == 4 ? 3 : 4;
    }
    app_data.openb = memcmp(cartridge + 12, "OPNB", 4) == 0;
    return RG_OK;
}

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || !host->set_video || bytes < 2048) return RG_BAD_ROM;
    if (bytes > sizeof(cartridge)) return RG_UNSUPPORTED_ROM;
    if (host->read_rom(host->context, 0, cartridge, bytes)) return RG_ROM_IO;
    if (!host->read_bios) return RG_MISSING_BIOS;

    memset(&app_data, 0, sizeof(app_data));
    memset(rom_table, 0, sizeof(rom_table));
    enum RgResult result = host->read_bios(host->context, "odyssey2.rom", rom_table[0], BiosBytes);
    if (result != RG_OK) return result;
    result = host->read_bios(host->context, "odyssey2-vdc.bin", cset, sizeof(cset));
    if (result != RG_OK) return result;
    for (unsigned bank = 1; bank < 8; ++bank) memcpy(rom_table[bank], rom_table[0], BiosBytes);

    switch (crc32(rom_table[0], BiosBytes)) {
    case 0x8016a315: app_data.bios = ROM_O2; break;
    case 0xa318e8d6: app_data.bios = ROM_C52; app_data.euro = 1; break;
    case 0xe20a9f41: app_data.bios = ROM_G7400; app_data.euro = app_data.vpp = 1; break;
    case 0x11647ca5: app_data.bios = ROM_JOPAC; app_data.euro = app_data.vpp = 1; break;
    default: app_data.bios = ROM_UNKNOWN; break;
    }
    if (app_data.vpp) {
        result = host->read_bios(host->context, "videopac-vpp.bin", vpp_cset, sizeof(vpp_cset));
        if (result != RG_OK) return result;
    }
    result = load_cart(bytes);
    if (result != RG_OK) return result;
    app_data.speed = 100;
    app_data.breakpoint = -1;
    app_data.stick[0] = app_data.stick[1] = 1;
    sound_IRQ = 0;
    memset(joystick_data, 0, sizeof(joystick_data));
    previous = 0;
    digit = 0;

    init_display();
    init_cpu();
    init_system();
    const RgVideoMode mode = { WNDW, WNDH, 1000000, evblclk == EVBLCLK_PAL ? 50 : 60 };
    return host->set_video(host->context, mode) ? RG_BAD_ROM : RG_OK;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **pixels)
{
    const uint32_t pressed = buttons & ~previous;
    previous = buttons;
    if (pressed & (1u << RG_SELECT)) digit = (digit + 1) % 10;
    memset(key + RETROK_0, 0, 10);
    key[RETROK_0 + digit] = (buttons >> RG_START) & 1;
    key[RETROK_RETURN] = (buttons >> RG_B) & 1;
    const unsigned control = (buttons >> RG_X) & 1;
    for (unsigned player = 0; player < 2; ++player)
        for (unsigned i = 0; i <= RG_A; ++i)
            joystick_data[player][i] = player == control && ((buttons >> i) & 1);

    o2_begin_frame();
    RLOOP = 1;
    cpu_exec();
    if (buttons & (1u << RG_SELECT)) o2_number(digit);
    *pixels = o2_framebuffer();
    return RG_OK;
}
