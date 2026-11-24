#include "retrogo/core.h"
#include "tgbdual/gb.h"
#include "tgbdual/storage.hpp"
#include <cstring>

namespace {
enum : unsigned { BankBytes = 16384, BankSlots = 8, Lines = 154 };
static byte fixed[BankBytes];
static struct {
    byte bytes[BankBytes];
    unsigned bank;
} cache[BankSlots];
static unsigned insert, banks;
static bool romError, rtc;
static RgHost host;
static gb *console;

class Display final : public renderer {
public:
    void reset() override {}
    void refresh() override {}
    void render_screen(byte *, int, int, int) override {}
    int check_pad() override { return pad_; }
    word map_color(word color) override {
        const unsigned green = (color >> 5) & 31;
        return ((color & 31) << 11) | ((green * 2 + (green >> 4)) << 5) | ((color >> 10) & 31);
    }
    byte get_time(int field) override {
        const uint32_t clock = seconds();
        switch (field) {
        case 8: return clock % 60;
        case 9: return clock / 60 % 60;
        case 10: return clock / 3600 % 24;
        case 11: return clock / 86400 & 255;
        case 12: return clock / 86400 >> 8;
        default: return 0;
        }
    }
    void set_time(int field, byte value) override {
        uint32_t clock = seconds();
        switch (field) {
        case 8: clock = clock / 60 * 60 + value % 60; break;
        case 9: clock = clock / 3600 * 3600 + (value % 60) * 60 + clock % 60; break;
        case 10: clock = clock / 86400 * 86400 + (value % 24) * 3600 + clock % 3600; break;
        case 11: clock = (clock / 86400 & 256) * 86400 + value * 86400 + clock % 86400; break;
        case 12: clock = (clock / 86400 & 255) * 86400 + (value & 1) * (256 * 86400) +
                          clock % 86400; break;
        default: return;
        }
        setSeconds(clock);
    }
    word get_sensor(bool) override { return 0x800; }
    void set_bibrate(bool) override {}
    void setButtons(uint32_t actions) {
        static constexpr uint8_t code[] = { 32, 16, 64, 128, 1, 2, 8, 4 };
        pad_ = 0;
        for (unsigned i = 0; i < sizeof(code); ++i)
            if (actions & (1u << i)) pad_ |= code[i];
    }
    void setSeconds(uint32_t value) {
        offset_ = value - host.seconds(host.context);
    }
    uint32_t seconds() const {
        return (host.seconds(host.context) + offset_) % (86400 * 512);
    }

private:
    uint32_t offset_ = 0;
    uint8_t pad_ = 0;
};

static Display *display;

uint32_t batteryBytes() {
    return console->get_rom()->has_battery() ? console->get_rom()->get_sram_size() : 0;
}
}

byte *tgb_read_bank(unsigned bank) {
    bank %= banks;
    if (bank == 0) return fixed;
    for (auto &slot : cache)
        if (slot.bank == bank) return slot.bytes;
    auto &slot = cache[insert++ % BankSlots];
    if (romError || host.read_rom(host.context, bank * BankBytes, slot.bytes, BankBytes)) {
        romError = true;
        std::memset(slot.bytes, 0xff, BankBytes);
    }
    slot.bank = bank;
    return slot.bytes;
}

extern "C" void __cxa_pure_virtual() { __builtin_trap(); }

extern "C" RgResult rg_open(const RgHost *services, uint32_t bytes) {
    if (!services || !services->read_rom || !services->seconds || bytes < 2 * BankBytes)
        return RG_BAD_ROM;
    host = {services->context, services->read_rom, services->seconds, nullptr, nullptr, nullptr};
    if (host.read_rom(host.context, 0, fixed, BankBytes)) return RG_ROM_IO;
    uint8_t checksum = 0;
    for (unsigned i = 0x134; i < 0x14d; ++i) checksum -= fixed[i] + 1;
    if (checksum != fixed[0x14d]) return RG_BAD_ROM;
    if (fixed[0x148] > 8 || fixed[0x149] > 5) return RG_UNSUPPORTED_ROM;
    banks = 2u << fixed[0x148];
    if (bytes != banks * BankBytes) return RG_BAD_ROM;
    switch (fixed[0x147]) {
    case 0: case 1: case 2: case 3: case 5: case 6: case 8: case 9:
    case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: break;
    default: return RG_UNSUPPORTED_ROM;
    }

    rtc = fixed[0x147] == 0x0f || fixed[0x147] == 0x10;
    insert = 0;
    romError = false;
    for (auto &slot : cache) slot.bank = 0;
    display = make_object<Display>();
    display->setSeconds(0);
    console = make_object<gb>(display, true, false);
    if (!console->load_rom(fixed, bytes, nullptr, 0, true)) return RG_BAD_ROM;
    return romError ? RG_ROM_IO : RG_OK;
}

extern "C" RgResult rg_run(uint32_t actions, const uint16_t **pixels) {
    display->setButtons(actions);
    for (unsigned i = 0; i < Lines && !romError; ++i) console->run();
    *pixels = console->frame();
    return romError ? RG_ROM_IO : RG_OK;
}

extern "C" uint32_t rg_save(RgSaveKind kind, void *out, uint32_t capacity) {
    if (kind != RG_SRAM) return 0;
    const uint32_t ram = batteryBytes(), size = ram + (rtc ? sizeof(uint32_t) : 0);
    if (size && capacity >= size) {
        std::memcpy(out, console->get_rom()->get_sram(), ram);
        if (rtc) {
            const uint32_t seconds = display->seconds();
            std::memcpy(static_cast<uint8_t *>(out) + ram, &seconds, sizeof(seconds));
        }
    }
    return size;
}

extern "C" RgResult rg_load(RgSaveKind kind, const void *data, uint32_t bytes) {
    const uint32_t ram = batteryBytes(), size = ram + (rtc ? sizeof(uint32_t) : 0);
    if (kind != RG_SRAM || !size || bytes != size) return RG_BAD_SAVE;
    if (rtc) {
        uint32_t seconds;
        std::memcpy(&seconds, static_cast<const uint8_t *>(data) + ram, sizeof(seconds));
        if (seconds >= 86400 * 512) return RG_BAD_SAVE;
        display->setSeconds(seconds);
    }
    std::memcpy(console->get_rom()->get_sram(), data, ram);
    return RG_OK;
}
