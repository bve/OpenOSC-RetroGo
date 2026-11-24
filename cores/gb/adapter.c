#include "retrogo/core.h"
#include "gnuboy/cpu.h"
#include "gnuboy/hw.h"
#include "gnuboy/lcd.h"
#include "gnuboy/loader.h"
#include "gnuboy/mem.h"
#include "gnuboy/regs.h"
#include "gnuboy/rtc.h"
#include "gnuboy/sound.h"

enum { BankBytes = 16384, CacheSlots = 8, FrameCycles = 35112, StateVersion = 1 };
static RgHost host;
static byte fixed_bank[BankBytes];
static struct {
    unsigned bank, age;
    byte bytes[BankBytes];
} cache[CacheSlots];
static unsigned age;
static int rom_error;
static uint16_t pixels[GB_WIDTH * GB_HEIGHT];
static byte cart[128 * 1024];
static uint32_t phase;
extern int lcd_latched_wy;

const byte *rom_loadbank(unsigned bank, unsigned pinned)
{
    if (!bank) return fixed_bank;
    unsigned victim = 0, oldest = 0;
    for (unsigned i = 0; i < CacheSlots; ++i) {
        if (cache[i].bank == bank) {
            cache[i].age = ++age;
            return cache[i].bytes;
        }
        if (!cache[i].bank) {
            victim = i;
            break;
        }
        const unsigned elapsed = age - cache[i].age;
        if (cache[i].bank != pinned && elapsed >= oldest) {
            victim = i;
            oldest = elapsed;
        }
    }
    if (rom_error || host.read_rom(host.context, bank * BankBytes, cache[victim].bytes, BankBytes)) {
        rom_error = 1;
        memset(cache[victim].bytes, 0xff, BankBytes);
    }
    cache[victim].bank = bank;
    cache[victim].age = ++age;
    return cache[victim].bytes;
}

uint32_t cart_seconds(void)
{
    return host.seconds(host.context);
}

static int cartridge_type(byte code)
{
    switch (code) {
    case 0: case 8: case 9: return MBC_NONE;
    case 1: case 2: case 3: return MBC_MBC1;
    case 5: case 6: return MBC_MBC2;
    case 0x0f: case 0x10: case 0x11: case 0x12: case 0x13: return MBC_MBC3;
    case 0x19: case 0x1a: case 0x1b: return MBC_MBC5;
    case 0x1c: case 0x1d: case 0x1e: return MBC_RUMBLE;
    default: return -1;
    }
}

enum RgResult rg_open(const RgHost *services, uint32_t bytes)
{
    if (!services || !services->read_rom || !services->seconds || bytes < 2 * BankBytes)
        return RG_BAD_ROM;
    host = (RgHost){ .context = services->context, .read_rom = services->read_rom,
                     .seconds = services->seconds };
    if (host.read_rom(host.context, 0, fixed_bank, BankBytes)) return RG_ROM_IO;
    const byte *h = fixed_bank;
    byte check = 0;
    for (unsigned i = 0x134; i < 0x14d; ++i) check -= h[i] + 1;
    if (check != h[0x14d]) return RG_BAD_ROM;

    const byte type = h[0x147], size = h[0x148], ram_size = h[0x149];
    const int mapper = cartridge_type(type);
    static const unsigned ram_bytes[] = { 0, 2048, 8192, 32768, 131072, 65536 };
    const unsigned banks = size <= 8 ? 2u << size :
                           size >= 0x52 && size <= 0x54 ? 64u + (8u << (size - 0x52)) : 0;
    if (mapper < 0 || !banks || ram_size >= sizeof(ram_bytes) / sizeof(ram_bytes[0]) ||
        (mapper == MBC_NONE && banks != 2) || (mapper == MBC_MBC2 && banks > 16) ||
        ((mapper == MBC_MBC1 || mapper == MBC_MBC3) && banks > 128))
        return RG_UNSUPPORTED_ROM;
    if (bytes != banks * BankBytes) return RG_BAD_ROM;

    memset(&mbc, 0, sizeof(mbc));
    memset(cache, 0, sizeof(cache));
    memset(&rtc, 0, sizeof(rtc));
    memset(ram.ibank, 0, sizeof(ram.ibank));
    memset(pixels, 0, sizeof(pixels));
    age = phase = rom_error = 0;
    hw.cgb = h[0x143] == 0x80 || h[0x143] == 0xc0;
    hw.gba = hw.hdma = 0;
    mbc.type = mapper;
    mbc.romsize = banks;
    mbc.batt = type == 3 || type == 6 || type == 9 || type == 0xf || type == 0x10 ||
               type == 0x13 || type == 0x1b || type == 0x1e;
    mbc.rtc = type == 0xf || type == 0x10;
    rtc.epoch = cart_seconds();
    ram.cart_bytes = mapper == MBC_MBC2 ? 512 : ram_bytes[ram_size];
    ram.sbank = (byte (*)[8192])cart;
    memset(cart, 0xff, ram.cart_bytes);
    memcpy(rom.name, h + 0x134, 16);
    rom.name[16] = 0;
    rom.checksum = h[0x14e] | (h[0x14f] << 8);

    hw_reset();
    lcd_reset();
    cpu_reset();
    mbc_reset();
    sound_reset();
    pal_set_dmg(0);
    fb = (struct fb){ .ptr = (byte *)pixels, .w = GB_WIDTH, .h = GB_HEIGHT,
                      .pitch = GB_WIDTH * sizeof(uint16_t), .format = GB_PIXEL_565_LE, .enabled = 1 };
    return rom_error ? RG_ROM_IO : RG_OK;
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **frame)
{
    static const byte translate[] = {
        PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_START, PAD_SELECT
    };
    byte pad = 0;
    for (unsigned i = 0; i < sizeof(translate); ++i)
        if (buttons & (1u << i)) pad |= translate[i];
    if (pad != hw.pad) {
        hw.pad = pad;
        pad_refresh();
    }
    const int requested = FrameCycles - phase;
    const int cycles = cpu_emulate(requested);
    if (rom_error) return RG_ROM_IO;
    if (cycles < 0) return RG_BAD_ROM;
    phase = cycles - requested;
    sound_clock();
    if (!(R_LCDC & 128)) memset(pixels, 0xff, sizeof(pixels));
    *frame = pixels;
    return RG_OK;
}

/* Native pointers, cache contents, and host services are derived on restore. */
struct State {
    uint32_t version, phase;
    struct cpu cpu;
    struct hw hw;
    struct snd snd;
    struct rtc rtc;
    int model, rom_bank, ram_bank, ram_enabled, latched_wy;
    struct lcd lcd;
    byte hi[sizeof(ram.hi)], wram[sizeof(ram.ibank)];
};

static int valid_rtc(const struct rtc *value)
{
    return value->s < 60 && value->m < 60 && value->h < 24 && value->d < 512;
}

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind == RG_SRAM && (!mbc.batt || (!ram.cart_bytes && !mbc.rtc))) return 0;
    const uint32_t prefix = kind == RG_STATE ? sizeof(struct State) : sizeof(struct rtc);
    const uint32_t bytes = prefix + ram.cart_bytes;
    if (bytes > capacity) return bytes;
    if (mbc.rtc) rtc_update();
    if (kind == RG_SRAM) {
        memcpy(out, &rtc, sizeof(rtc));
    } else {
        struct State *s = out;
        memset(s, 0, sizeof(*s));
        s->version = StateVersion;
        s->phase = phase;
        s->cpu = cpu;
        s->hw = hw;
        s->snd = snd;
        s->rtc = rtc;
        s->model = mbc.model;
        s->rom_bank = mbc.rombank;
        s->ram_bank = mbc.rambank;
        s->ram_enabled = mbc.enableram;
        s->latched_wy = lcd_latched_wy;
        s->lcd = lcd;
        memcpy(s->hi, ram.hi, sizeof(ram.hi));
        memcpy(s->wram, ram.ibank, sizeof(ram.ibank));
    }
    memcpy((byte *)out + prefix, cart, ram.cart_bytes);
    return bytes;
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    const unsigned prefix = kind == RG_STATE ? sizeof(struct State) : sizeof(struct rtc);
    if (size != prefix + ram.cart_bytes) return RG_BAD_SAVE;
    if (kind == RG_SRAM) {
        if (!mbc.batt || !valid_rtc(data)) return RG_BAD_SAVE;
        rtc = *(const struct rtc *)data;
    } else {
        const struct State *s = data;
        if (s->version != StateVersion || s->phase >= 32 || s->hw.cgb != hw.cgb ||
            s->rom_bank < 0 || s->rom_bank > 511 || s->ram_bank < 0 || s->ram_bank > 15 ||
            (unsigned)s->model > 1 || (unsigned)s->ram_enabled > 1 || (unsigned)s->cpu.speed > 1 ||
            s->lcd.cycles < -128 || s->lcd.cycles > 228 || s->hi[RI_LY] > 153 ||
            (unsigned)s->latched_wy > 255 || s->snd.cycles >= 4096 || !valid_rtc(&s->rtc))
            return RG_BAD_SAVE;
        phase = s->phase;
        cpu = s->cpu;
        hw = s->hw;
        snd = s->snd;
        rtc = s->rtc;
        lcd_latched_wy = s->latched_wy;
        lcd = s->lcd;
        mbc.model = s->model;
        mbc.rombank = s->rom_bank;
        mbc.rambank = s->ram_bank;
        mbc.enableram = s->ram_enabled;
        memcpy(ram.hi, s->hi, sizeof(ram.hi));
        memcpy(ram.ibank, s->wram, sizeof(ram.ibank));
        pal_dirty();
        mem_updatemap();
    }
    rtc.epoch = cart_seconds();
    memcpy(cart, (const byte *)data + prefix, ram.cart_bytes);
    return rom_error ? RG_ROM_IO : RG_OK;
}
