/* Supervision machine interface adapted from Game & Watch Retro-Go's Potator.
 * GPL-2.0-or-later. The 65C02 implementation is vrEmu6502 under MIT. */
#include "retrogo/core.h"
#include "vremu6502/vrEmu6502.h"
#include <string.h>

enum { BankBytes = 0x4000, FrameCycles = 65536, LcdWidth = 160, LcdHeight = 160 };
enum {
    XSize = 0x00, Horiz = 0x02, Vert = 0x03, DmaSource = 0x08, DmaTarget = 0x0a,
    DmaLength = 0x0c, DmaRequest = 0x0d, Input = 0x20, Port = 0x21, Direction = 0x22,
    Timer = 0x23, TimerAck = 0x24, DmaAck = 0x25, Bank = 0x26, IrqStatus = 0x27,
    AudioDmaLength = 0x1a, AudioDmaRate = 0x1b, AudioDmaEnable = 0x1c
};

struct Console {
    uint8_t ram[0x2000], video[0x2000], regs[0x2000];
    uint32_t timer_cycles, audio_dma_cycles;
    uint8_t timer_pending, dma_pending, pad;
};

static struct Console machine;
static VrEmu6502 *cpu;
static uint8_t fixed[BankBytes];
static struct {
    uint8_t data[BankBytes];
    unsigned index;
} cache[4];
static unsigned cache_insert, banks;
static const uint8_t *lower;
static uint16_t pixels[LcdWidth * LcdHeight];
static void *rom_context;
static int (*read_rom)(void *, uint32_t, void *, uint32_t);
static int rom_error;

static void update_bank(void)
{
    unsigned index = machine.regs[Bank] >> 5;
    if (banks > 8)
        index = (index & 1) | ((machine.regs[Port] & 15) << 1);
    index %= banks;
    if (index == banks - 1) {
        lower = fixed;
        return;
    }

    for (unsigned i = 0; i < sizeof(cache) / sizeof(*cache); ++i) {
        if (cache[i].index == index) {
            lower = cache[i].data;
            return;
        }
    }
    const unsigned i = cache_insert++ % (sizeof(cache) / sizeof(*cache));
    if (rom_error || read_rom(rom_context, index * BankBytes, cache[i].data, BankBytes)) {
        rom_error = 1;
        memset(cache[i].data, 0xff, BankBytes);
    }
    cache[i].index = index;
    lower = cache[i].data;
}

static void update_irq(void)
{
    const uint8_t mask = machine.regs[Bank];
    *vrEmu6502Int(cpu) = ((mask & 2) && machine.timer_pending) ||
                       ((mask & 4) && machine.dma_pending) ? IntRequested : IntCleared;
}

static uint8_t bus_read(uint16_t address, bool debug)
{
    if (address >= 0xc000) return fixed[address & 0x3fff];
    if (address >= 0x8000) return lower[address & 0x3fff];
    if (address >= 0x6000) return address >> 8;
    if (address >= 0x4000) return machine.video[address & 0x1fff];
    if (address < 0x2000) return machine.ram[address];

    const unsigned reg = address & 0x1fff;
    const uint8_t value = machine.regs[reg];
    switch (reg) {
    case Input: return ~machine.pad;
    case Port: return value | (machine.regs[Direction] & 15);
    case TimerAck:
        if (!debug) {
            machine.timer_pending = 0;
            update_irq();
        }
        break;
    case DmaAck:
        if (!debug) {
            machine.dma_pending = 0;
            update_irq();
        }
        break;
    case IrqStatus:
        return (value & ~3) | machine.timer_pending | (machine.dma_pending << 1);
    }
    return value;
}

static void bus_write(uint16_t address, uint8_t value);

static void transfer(void)
{
    const uint16_t cpu_addr = machine.regs[DmaSource] | (machine.regs[DmaSource + 1] << 8);
    const unsigned lcd_addr = machine.regs[DmaTarget] | (machine.regs[DmaTarget + 1] << 8);
    const unsigned length = machine.regs[DmaLength] ? machine.regs[DmaLength] * 16 : 4096;
    const int to_video = lcd_addr & 0x4000;
    for (unsigned i = 0; i < length; ++i) {
        uint8_t *pixel = machine.video + ((lcd_addr + i) & 0x1fff);
        const uint16_t source = cpu_addr + i;
        if (to_video)
            *pixel = bus_read(source, false);
        else if ((source & 0xefff) != 0x200d)
            bus_write(source, *pixel);
    }
}

static void bus_write(uint16_t address, uint8_t value)
{
    if (address < 0x2000) {
        machine.ram[address] = value;
    } else if (address < 0x4000) {
        const unsigned reg = address & 0x1fff;
        machine.regs[reg] = value;
        switch (reg) {
        case Port:
            if (banks > 8 && !machine.regs[Direction]) update_bank();
            break;
        case Bank:
            update_bank();
            update_irq();
            break;
        case Timer:
            machine.timer_cycles = (value ? value : 256u) << ((machine.regs[Bank] & 16) ? 14 : 8);
            break;
        case AudioDmaEnable: {
            const unsigned length = machine.regs[AudioDmaLength];
            const unsigned shift = machine.regs[AudioDmaRate] & 3;
            machine.audio_dma_cycles = value & 128 ? (length ? length : 256u) << (13 + shift) : 0;
            break;
        }
        case DmaRequest:
            if (value & 128) transfer();
            break;
        }
    } else if (address < 0x6000) {
        machine.video[address & 0x1fff] = value;
    }
}

enum RgResult rg_open(const RgHost *host, uint32_t bytes)
{
    if (!host || !host->read_rom || bytes < BankBytes || bytes % BankBytes) return RG_BAD_ROM;
    if (bytes > 32 * BankBytes) return RG_UNSUPPORTED_ROM;
    banks = bytes / BankBytes;
    rom_context = host->context;
    read_rom = host->read_rom;
    if (read_rom(rom_context, bytes - BankBytes, fixed, BankBytes)) return RG_ROM_IO;
    memset(&machine, 0, sizeof(machine));
    for (unsigned i = 0; i < sizeof(cache) / sizeof(*cache); ++i) cache[i].index = 32;
    cache_insert = rom_error = 0;
    update_bank();
    cpu = vrEmu6502New(CPU_65C02, bus_read, bus_write);
    return rom_error ? RG_ROM_IO : RG_OK;
}

static void render(void)
{
    static const uint16_t grayscale[] = { 0xffff, 0xad55, 0x52aa, 0x0000 };
    const unsigned x = machine.regs[Horiz];
    const unsigned y = machine.regs[Vert];
    const unsigned size = machine.regs[XSize] < LcdWidth ? machine.regs[XSize] : LcdWidth;
    for (unsigned row = 0; row < LcdHeight; ++row) {
        const unsigned line = ((y + row) * 0x30 + x / 4) % 0x1fe0;
        for (unsigned col = 0; col < size; ++col) {
            const unsigned dot = (x & 3) + col;
            const uint8_t value = machine.video[(line + dot / 4) & 0x1fff];
            pixels[row * LcdWidth + col] = grayscale[(value >> ((dot & 3) * 2)) & 3];
        }
        for (unsigned col = size; col < LcdWidth; ++col)
            pixels[row * LcdWidth + col] = grayscale[0];
    }
}

enum RgResult rg_run(uint32_t buttons, const uint16_t **frame)
{
    static const uint8_t map[] = { 8, 4, 2, 1, 32, 16, 128, 64 };
    machine.pad = 0;
    for (unsigned i = 0; i < sizeof(map); ++i)
        if (buttons & (1u << i)) machine.pad |= map[i];

    for (unsigned cycle = 0; cycle < FrameCycles && !rom_error;) {
        const unsigned ticks = vrEmu6502Advance(cpu, FrameCycles - cycle);
        cycle += ticks;
        if (machine.timer_cycles && ticks >= machine.timer_cycles) {
            machine.timer_cycles = 0;
            machine.timer_pending = 1;
            update_irq();
        } else if (machine.timer_cycles) {
            machine.timer_cycles -= ticks;
        }
        if (machine.audio_dma_cycles && ticks >= machine.audio_dma_cycles) {
            machine.audio_dma_cycles = 0;
            machine.dma_pending = 1;
            update_irq();
        } else if (machine.audio_dma_cycles) {
            machine.audio_dma_cycles -= ticks;
        }
    }
    if (rom_error) return RG_ROM_IO;
    if (machine.regs[Bank] & 1) *vrEmu6502Nmi(cpu) = IntRequested;
    render();
    *frame = pixels;
    return RG_OK;
}

struct State {
    uint32_t version;
    VrEmu6502State cpu;
    struct Console console;
};

uint32_t rg_save(enum RgSaveKind kind, void *out, uint32_t capacity)
{
    if (kind != RG_STATE) return 0;
    if (capacity >= sizeof(struct State)) {
        struct State *s = out;
        memset(s, 0, sizeof(*s));
        s->version = 1;
        s->cpu = vrEmu6502GetState(cpu);
        s->console = machine;
    }
    return sizeof(struct State);
}

enum RgResult rg_load(enum RgSaveKind kind, const void *data, uint32_t size)
{
    if (kind != RG_STATE || size != sizeof(struct State)) return RG_BAD_SAVE;
    const struct State *s = data;
    if (s->version != 1 || s->console.timer_pending > 1 || s->console.dma_pending > 1 ||
        !vrEmu6502SetState(cpu, &s->cpu))
        return RG_BAD_SAVE;
    machine = s->console;
    update_bank();
    update_irq();
    return rom_error ? RG_ROM_IO : RG_OK;
}
