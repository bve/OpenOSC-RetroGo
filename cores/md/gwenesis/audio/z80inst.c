#include "z80inst.h"
#include "gwenesis_bus.h"
#include "gwenesis_vdp.h"
#include "m68k.h"
#include "ym2612.h"
#include "z80.h"

static z80 cpu;
static uint8_t *ram;
static unsigned bank;
static int clock;
static bool request, reset, irq, executing;

static uint8_t read_byte(void *context, uint16_t address)
{
    (void)context;
    if (address < 0x4000) return ram[address & 0x1fff];
    if (address < 0x6000) return YM2612Read(clock);
    if (address >= 0x8000) return m68k_read_memory_8((bank << 15) | (address & 0x7fff));
    if ((address & 0xffe0) == 0x7f00) return gwenesis_vdp_read_memory_8(address);
    return 0xff;
}

static void write_byte(void *context, uint16_t address, uint8_t value)
{
    (void)context;
    if (address < 0x4000) ram[address & 0x1fff] = value;
    else if (address < 0x6000) YM2612Write(address & 3, value, clock);
    else if (address < 0x6100) bank = (bank >> 1) | ((value & 1) << 8);
    else if (address >= 0x8000) m68k_write_memory_8((bank << 15) | (address & 0x7fff), value);
}

static uint8_t port_in(z80 *unused, uint8_t port)
{
    (void)unused;
    (void)port;
    return 0xff;
}

static void port_out(z80 *unused, uint8_t port, uint8_t value)
{
    (void)unused;
    (void)port;
    (void)value;
}

void z80_set_memory(unsigned char *memory) { ram = memory; }

void z80_pulse_reset(void)
{
    z80_init(&cpu);
    cpu.read_byte = read_byte;
    cpu.write_byte = write_byte;
    cpu.port_in = port_in;
    cpu.port_out = port_out;
    reset = true;
    irq = false;
    bank = 0;
}

void z80_start(void)
{
    clock = 0;
    request = executing = false;
    z80_pulse_reset();
}

void z80_run(int target)
{
    if (executing || target <= clock) return;
    if (request || reset) {
        clock = target;
        return;
    }
    executing = true;
    while (clock < target) {
        const unsigned long previous = cpu.cyc;
        cpu.int_pending = irq;
        cpu.int_data = 0xff;
        z80_step(&cpu);
        clock += (cpu.cyc - previous) * 15;
    }
    executing = false;
}

void md_end_z80_frame(int cycle) { clock -= cycle; }
void z80_irq_line(unsigned value) { irq = value != 0; }

void z80_write_ctrl(unsigned address, unsigned value)
{
    z80_run(m68k_cycles_master());
    switch (address & 0xff00) {
    case 0x1100: request = value & 1; break;
    case 0x1200:
        if (!(value & 1) && !reset) z80_pulse_reset();
        reset = !(value & 1);
        break;
    }
}

unsigned z80_read_ctrl(unsigned address)
{
    z80_run(m68k_cycles_master());
    if ((address & 0xff00) == 0x1100) return request || reset ? 0 : 1;
    return 0xff;
}
