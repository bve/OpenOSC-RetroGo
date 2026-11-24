#include "retrogo/core.h"
#include "pce-go/gfx.h"
#include "pce-go/pce.h"

enum {
    BankBytes = 8192, CacheSlots = 12, HeaderBytes = 512,
    OutputWidth = 256, OutputHeight = 224
};

static uint8_t indexed[XBUF_WIDTH * PCE_MAX_HEIGHT];
static uint16_t pixels[OutputWidth * OutputHeight];
static struct {
    uint8_t bytes[BankBytes];
    unsigned bank;
    uint32_t age;
} cache[CacheSlots];
static uint32_t age, rom_offset;
static int reversed, rom_error;
static void *rom_context;
static int (*read_rom)(void *, uint32_t, void *, uint32_t);

uint8_t *osd_gfx_framebuffer(void)
{
    return indexed + 32;
}

static uint8_t reverse_bits(uint8_t value)
{
    value = ((value & 0x55) << 1) | ((value & 0xaa) >> 1);
    value = ((value & 0x33) << 2) | ((value & 0xcc) >> 2);
    return (value << 4) | (value >> 4);
}

static uint8_t *cached_bank(unsigned bank, unsigned page)
{
    unsigned victim = CacheSlots;
    uint32_t oldest = 0;
    for (unsigned i = 0; i < CacheSlots; ++i) {
        if (cache[i].age && cache[i].bank == bank) {
            cache[i].age = ++age;
            return cache[i].bytes;
        }
        if (!cache[i].age) {
            victim = i;
            break;
        }
        unsigned pinned = 0;
        for (unsigned j = 0; j < 8; ++j)
            if (j != page && PageR[j] == cache[i].bytes)
                pinned = 1;
        const uint32_t idle = age - cache[i].age;
        if (!pinned && (victim == CacheSlots || idle > oldest)) {
            oldest = idle;
            victim = i;
        }
    }
    if (rom_error || read_rom(rom_context, rom_offset + bank * BankBytes,
                              cache[victim].bytes, BankBytes)) {
        rom_error = 1;
        memset(cache[victim].bytes, 0xff, BankBytes);
    } else if (reversed) {
        for (unsigned i = 0; i < BankBytes; ++i)
            cache[victim].bytes[i] = reverse_bits(cache[victim].bytes[i]);
    }
    cache[victim].bank = bank;
    cache[victim].age = ++age;
    return cache[victim].bytes;
}

void pce_bank_set(uint8_t page, uint8_t bank)
{
    PCE.MMR[page] = bank;
    PageR[page] = PCE.NULLRAM;
    PageW[page] = NULL;
    if (bank == 0xff) {
        PageR[page] = PageW[page] = PCE.IOAREA;
    } else if (bank >= 0xf8 && bank <= 0xfb) {
        PageR[page] = PageW[page] = PCE.RAM;
    } else if (bank < 0x80) {
        unsigned mapped = bank;
        if (PCE.ROM_SIZE == 320) {
            if (!bank) PageW[page] = PCE.NULLRAM;
            if (bank >= 0x40) mapped += PCE.SF2 * 64;
        } else if (PCE.ROM_SIZE == 48) {
            static const uint8_t bases[] = { 0, 16, 0, 32, 32, 16, 32, 32 };
            mapped = bases[bank >> 4] + (bank & 15);
        } else {
            mapped %= PCE.ROM_SIZE;
        }
        PageR[page] = cached_bank(mapped, page);
    }
}

enum RgResult rg_open(const RgHost *host, uint32_t size)
{
    if (!host || !host->read_rom) return RG_BAD_ROM;
    rom_offset = size % BankBytes;
    const unsigned banks = size / BankBytes;
    if (!banks || (rom_offset != 0 && rom_offset != HeaderBytes)) return RG_BAD_ROM;
    if (banks > 128 && banks != 320) return RG_UNSUPPORTED_ROM;

    rom_context = host->context;
    read_rom = host->read_rom;
    uint8_t reset_high;
    if (read_rom(rom_context, rom_offset + BankBytes - 1, &reset_high, 1))
        return RG_ROM_IO;

    reversed = reset_high < 0xe0 && reverse_bits(reset_high) >= 0xe0;
    rom_error = 0;
    memset(&PCE, 0, sizeof(PCE));
    memset(&CPU_PCE, 0, sizeof(CPU_PCE));
    memset(PageR, 0, sizeof(PageR));
    for (unsigned i = 0; i < CacheSlots; ++i) cache[i].age = 0;
    age = 0;
    PCE.ROM_SIZE = banks;
    gfx_reset(true);
    pce_reset(true);
    return rom_error ? RG_ROM_IO : RG_OK;
}

static uint16_t rgb565(unsigned index)
{
    const unsigned red = (index >> 2) & 7;
    const unsigned green = index >> 5;
    const unsigned blue = index & 3;
    return ((red * 31 / 7) << 11) | ((green * 63 / 7) << 5) | (blue * 31 / 3);
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **frame)
{
    static const uint8_t input[] = {
        JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_A, JOY_B, JOY_RUN, JOY_SELECT
    };
    uint8_t pad = 0;
    for (unsigned i = 0; i < sizeof(input); ++i)
        if (buttons & (1u << i)) pad |= input[i];
    PCE.Joypad.regs[0] = pad;

    memset(indexed, PCE.Palette[0], sizeof(indexed));
    for (PCE.Scanline = 0; PCE.Scanline < 263; ++PCE.Scanline) gfx_run();
    PCE.Timer.cycles_counter -= Cycles;
    PCE.MaxCycles -= Cycles;
    Cycles = 0;
    if (rom_error) return RG_ROM_IO;

    const unsigned width = IO_VDC_SCREEN_WIDTH < PCE_MAX_WIDTH ? IO_VDC_SCREEN_WIDTH : PCE_MAX_WIDTH;
    const unsigned start = IO_VDC_MINLINE > 14 ? IO_VDC_MINLINE : 14;
    const unsigned end = IO_VDC_MAXLINE < 255 ? IO_VDC_MAXLINE : 255;
    const unsigned height = end >= start ? end - start + 1 : 1;
    uint16_t palette[256];
    for (unsigned i = 0; i < 256; ++i) palette[i] = rgb565(i);
    for (unsigned y = 0; y < OutputHeight; ++y) {
        const uint8_t *line = osd_gfx_framebuffer() + y * height / OutputHeight * XBUF_WIDTH;
        for (unsigned x = 0; x < OutputWidth; ++x)
            pixels[y * OutputWidth + x] = palette[line[x * width / OutputWidth]];
    }
    *frame = pixels;
    return RG_OK;
}

struct State {
    uint32_t version;
    h6280_t cpu;
    PCE_t console;
};

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind != RG_STATE) return 0;
    if (capacity >= sizeof(struct State)) {
        struct State *state = out;
        state->version = 1;
        state->cpu = CPU_PCE;
        state->console = PCE;
    }
    return sizeof(struct State);
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    if (kind != RG_STATE || size != sizeof(struct State)) return RG_BAD_SAVE;
    const struct State *state = data;
    if (state->version != 1 || state->console.ROM_SIZE != PCE.ROM_SIZE ||
        state->console.SF2 > 3 || state->console.VDC.reg > 31 || state->console.PSG.ch > 5 ||
        state->console.VCE.reg.W > 511 || state->console.Joypad.counter > 7 ||
        state->console.Timer.cycles_per_line > 455)
        return RG_BAD_SAVE;
    PCE = state->console;
    CPU_PCE = state->cpu;
    gfx_reset(true);
    for (unsigned i = 0; i < 8; ++i) pce_bank_set(i, PCE.MMR[i]);
    return rom_error ? RG_ROM_IO : RG_OK;
}
