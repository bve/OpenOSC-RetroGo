/* OpenOSC-RetroGo port of FCEUmm, an alternate NES core. Distributed under
 * the GNU GPL v2 or later, like FCEUmm. */
#include "retrogo/core.h"
#include "port/port.h"
#include <string.h>
#include "fceu-types.h"
#include "fceu.h"
#include "fceu-cart.h"
#include "fceu-endian.h"
#include "fceu-state.h"
#include "driver.h"
#include "git.h"

enum { StateHeaderBytes = 12, MaxSaveBytes = RG_SNAPSHOT_CAPACITY };

extern CartInfo iNESCart;
extern FCEUGI *GameInfo;
extern uint8 PAL;
void FCEUSS_Save_Fs(const char *path);
void FCEUSS_Load_Fs(const char *path);

static uint8_t header[PortHeaderBytes];
static uint32_t joypad;
static int initialized;

static enum RgResult select_video(void)
{
    /* PAL and Dendy both run 50 Hz frames (19997.2 us). */
    const int slow = PAL || dendy;
    const RgVideoMode mode = {
        PortWidth, PortHeight,
        slow ? 88227687u : 5241368u,
        slow ? 4412u : 315u,
    };
    /* The manifest mode is NTSC; an ABI 1 host can only run that. */
    if (port_host.set_video) return port_host.set_video(port_host.context, mode) ? RG_UNSUPPORTED_ROM : RG_OK;
    return slow ? RG_UNSUPPORTED_ROM : RG_OK;
}

enum RgResult rg_open(const RgHost *services, uint32_t bytes)
{
    if (!services || !services->read_rom || bytes < 16) return RG_BAD_ROM;
    port_reset((const RgHostV3 *)services);
    port_host = *services;
    port_rom_bytes = bytes;
    memset(header, 0, sizeof(header));
    const uint32_t head = bytes < sizeof(header) ? bytes : sizeof(header);
    if (port_host.read_rom(port_host.context, 0, header, head)) return RG_ROM_IO;
    if (memcmp(header, "NES\x1a", 4)) return RG_BAD_ROM;
    if ((header[6] & 4) && bytes < 16 + 512) return RG_BAD_ROM;

    if (!initialized) {
        if (!FCEUI_Initialize()) return RG_UNSUPPORTED_ROM;
        initialized = 1;
    }
    /* The previous game's memory belongs to the reset arena; do not close it. */
    GameInfo = NULL;
    joypad = 0;
    if (!FCEUI_LoadGame("", header, bytes, NULL))
        return port_rom_error ? RG_ROM_IO : RG_UNSUPPORTED_ROM;
    if (port_rom_error) return RG_ROM_IO;
    FCEUI_SetInput(0, SI_GAMEPAD, &joypad, 0);
    FCEUI_SetInput(1, SI_NONE, NULL, 0);
    return select_video();
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **out)
{
    static const uint8_t bits[] = {JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_A, JOY_B, JOY_START, JOY_SELECT};
    uint32_t pad = 0;
    for (unsigned i = 0; i < sizeof(bits); ++i)
        if (buttons & (1u << i)) pad |= bits[i];
    joypad = pad;
    uint8 *gfx;
    int32 *sound, samples;
    FCEUI_Emulate(&gfx, &sound, &samples, 0);
    *out = port_frame;
    return port_rom_error ? RG_ROM_IO : RG_OK;
}

static uint32_t battery_bytes(void)
{
    if (!iNESCart.battery) return 0;
    uint32_t total = 0;
    for (int i = 0; i < 4; ++i)
        if (iNESCart.SaveGame[i]) total += iNESCart.SaveGameLen[i];
    return total;
}

static uint32_t state_bytes(void)
{
    port_stream(NULL, 0, 1);
    FCEUSS_Save_Fs("");
    return port_stream_length();
}

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind == RG_SRAM) {
        const uint32_t total = battery_bytes();
        if (!total || capacity < total) return total;
        uint8_t *p = out;
        for (int i = 0; i < 4; ++i) {
            if (!iNESCart.SaveGame[i]) continue;
            memcpy(p, iNESCart.SaveGame[i], iNESCart.SaveGameLen[i]);
            p += iNESCart.SaveGameLen[i];
        }
        return total;
    }
    if (kind != RG_STATE) return 0;
    const uint32_t total = state_bytes();
    if (total > MaxSaveBytes || capacity < total) return total;
    port_stream(out, capacity, 1);
    FCEUSS_Save_Fs("");
    return port_stream_failed() ? 0 : port_stream_length();
}

/* A state for this game has the same chunk layout and length as a fresh
 * save. Check the header and framing before anything is restored. */
static int valid_state(const uint8_t *data, uint32_t size)
{
    if (size < StateHeaderBytes || size != state_bytes() || memcmp(data, "FCS\xff", 4) ||
        FCEU_de32lsb(data + 4) != FCEU_VERSION_NUMERIC)
        return 0;
    for (uint32_t at = StateHeaderBytes; at < size;) {
        if (size - at < 5) return 0;
        const uint32_t chunk = FCEU_de32lsb(data + at + 1);
        at += 5;
        if (chunk > size - at) return 0;
        for (uint32_t entry = at, end = at + chunk; entry < end;) {
            if (end - entry < 8) return 0;
            const uint32_t length = FCEU_de32lsb(data + entry + 4);
            entry += 8;
            if (length > end - entry) return 0;
            entry += length;
        }
        at += chunk;
    }
    return 1;
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    if (kind == RG_SRAM) {
        if (!size || size != battery_bytes()) return RG_BAD_SAVE;
        const uint8_t *p = data;
        for (int i = 0; i < 4; ++i) {
            if (!iNESCart.SaveGame[i]) continue;
            memcpy(iNESCart.SaveGame[i], p, iNESCart.SaveGameLen[i]);
            p += iNESCart.SaveGameLen[i];
        }
        return port_rom_error ? RG_ROM_IO : RG_OK;
    }
    if (kind != RG_STATE || !valid_state(data, size)) return RG_BAD_SAVE;
    port_stream((uint8_t *)data, size, 0);
    FCEUSS_Load_Fs("");
    if (port_stream_failed()) return RG_BAD_SAVE;
    return port_rom_error ? RG_ROM_IO : RG_OK;
}
