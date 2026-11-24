/* Z80 glue for SMS Plus GX in OpenOSC-RetroGo.
 *
 * Builds the unmodified MIT-licensed superzazu/z80 core in cores/common/z80 into this
 * translation unit and adapts it to the SMS Plus GX CPU interface. This glue is
 * distributed under the GNU GPL v2 or later, like SMS Plus GX.
 *
 * Timing follows the upstream SMS Plus GX MAME core: cycles are charged when
 * an instruction starts, a pending interrupt is taken before the next
 * instruction, the IRQ input is level-sensitive, and NMI triggers on a rising
 * edge. */
#include <stdio.h>
#include <string.h>

/* The core only prints diagnostics for an invalid interrupt mode, which this
   glue prevents. Keep stdio formatting out of the module. */
#define fprintf(...) ((void)0)
#define printf(...) ((void)0)

/* Keep the core's public names private to this file. */
#define z80_init superzazu_z80_init
#define z80_step superzazu_z80_step
#define z80_debug_output superzazu_z80_debug_output
#define z80_gen_nmi superzazu_z80_gen_nmi
#define z80_gen_int superzazu_z80_gen_int
#include "../../../common/z80/z80.c"
#undef z80_init
#undef z80_step
#undef z80_debug_output
#undef z80_gen_nmi
#undef z80_gen_int
#undef fprintf
#undef printf

#include "sms_z80.h"

unsigned char *cpu_readmap[64];
unsigned char *cpu_writemap[64];
void (*cpu_writemem16)(int address, int data);
void (*cpu_writeport16)(uint16_t port, uint8_t data);
uint8_t (*cpu_readport16)(uint16_t port);

int z80_cycle_count;

static z80 cpu;
static int (*irq_callback)(int);
static int in_exec;
static uint8_t irq_line, nmi_line;

static uint8_t read_byte(void *userdata, uint16_t address)
{
  (void)userdata;
  return cpu_readmap[address >> 10][address & 0x03FF];
}

static void write_byte(void *userdata, uint16_t address, uint8_t data)
{
  (void)userdata;
  cpu_writemem16(address, data);
}

static uint8_t port_in(z80 *z, uint8_t port)
{
  (void)z;
  return cpu_readport16(port);
}

static void port_out(z80 *z, uint8_t port, uint8_t data)
{
  (void)z;
  cpu_writeport16(port, data);
}

void z80_init(int index, int clock, const void *config, int (*irqcallback)(int))
{
  (void)index;
  (void)clock;
  (void)config;
  superzazu_z80_init(&cpu);
  cpu.read_byte = read_byte;
  cpu.write_byte = write_byte;
  cpu.port_in = port_in;
  cpu.port_out = port_out;

  /* Register values from the upstream SMS Plus GX core. SP replaces the value
     normally set by the BIOS (fixes Shadow Dancer and Ace of Aces). */
  cpu.a = cpu.b = cpu.c = cpu.d = cpu.e = cpu.h = cpu.l = 0;
  set_f(&cpu, 0x40);
  cpu.ix = cpu.iy = 0xFFFF;
  cpu.sp = 0xDFF0;
  irq_callback = irqcallback;
  irq_line = nmi_line = 0;
  in_exec = 0;
}

void z80_reset(void)
{
  cpu.pc = 0;
  cpu.i = 0;
  cpu.r = 0;
  cpu.interrupt_mode = 0;
  cpu.iff1 = cpu.iff2 = 0;
  cpu.iff_delay = 0;
  cpu.halted = 0;
  cpu.nmi_pending = 0;
  cpu.int_pending = 0;
  cpu.mem_ptr = 0;
}

int z80_execute(int cycles)
{
  in_exec = 1;
  cpu.cyc = 0;
  while ((long)cpu.cyc < cycles)
  {
    /* The VDP holds IRQ until its status is read. */
    cpu.int_pending = irq_line;
    if (irq_line && cpu.iff1 && !cpu.iff_delay)
      cpu.int_data = (uint8_t)irq_callback(0);
    process_interrupts(&cpu);
    if ((long)cpu.cyc >= cycles)
      break;
    if (cpu.halted)
      exec_opcode(&cpu, 0x00);
    else
      exec_opcode(&cpu, nextb(&cpu));
  }
  in_exec = 0;
  const int done = (int)cpu.cyc;
  z80_cycle_count += done;
  return done;
}

void z80_set_irq_line(int irqline, int state)
{
  if (irqline == INPUT_LINE_NMI)
  {
    /* mark an NMI pending on the rising edge */
    if (!nmi_line && state != CLEAR_LINE)
      cpu.nmi_pending = 1;
    nmi_line = state != CLEAR_LINE;
  }
  else
  {
    irq_line = state != CLEAR_LINE;
    if (!irq_line)
      cpu.int_pending = 0;
  }
}

void z80_reset_cycle_count(void)
{
  z80_cycle_count = 0;
}

int z80_get_elapsed_cycles(void)
{
  return in_exec ? z80_cycle_count + (int)cpu.cyc : z80_cycle_count;
}

uint16_t z80_get_pc(void)
{
  return cpu.pc;
}

static uint16_t pair(uint8_t high, uint8_t low)
{
  return (uint16_t)(high << 8 | low);
}

void z80_get_state(z80_state_t *s)
{
  memset(s, 0, sizeof(*s));
  s->pc = cpu.pc;
  s->sp = cpu.sp;
  s->ix = cpu.ix;
  s->iy = cpu.iy;
  s->wz = cpu.mem_ptr;
  s->af = pair(cpu.a, get_f(&cpu));
  s->bc = pair(cpu.b, cpu.c);
  s->de = pair(cpu.d, cpu.e);
  s->hl = pair(cpu.h, cpu.l);
  s->af2 = pair(cpu.a_, cpu.f_);
  s->bc2 = pair(cpu.b_, cpu.c_);
  s->de2 = pair(cpu.d_, cpu.e_);
  s->hl2 = pair(cpu.h_, cpu.l_);
  s->i = cpu.i;
  s->r = cpu.r;
  s->im = cpu.interrupt_mode;
  s->iff1 = cpu.iff1;
  s->iff2 = cpu.iff2;
  s->halt = cpu.halted;
  s->iff_delay = cpu.iff_delay;
  s->nmi_pending = cpu.nmi_pending;
  s->irq_line = irq_line;
  s->nmi_line = nmi_line;
}

int z80_state_valid(const z80_state_t *s)
{
  return s->im <= 2 && s->iff1 <= 1 && s->iff2 <= 1 && s->halt <= 1 && s->iff_delay <= 1 &&
         s->nmi_pending <= 1 && s->irq_line <= 1 && s->nmi_line <= 1;
}

void z80_set_state(const z80_state_t *s)
{
  cpu.pc = s->pc;
  cpu.sp = s->sp;
  cpu.ix = s->ix;
  cpu.iy = s->iy;
  cpu.mem_ptr = s->wz;
  cpu.a = s->af >> 8;
  set_f(&cpu, s->af & 0xFF);
  cpu.b = s->bc >> 8;
  cpu.c = s->bc & 0xFF;
  cpu.d = s->de >> 8;
  cpu.e = s->de & 0xFF;
  cpu.h = s->hl >> 8;
  cpu.l = s->hl & 0xFF;
  cpu.a_ = s->af2 >> 8;
  cpu.f_ = s->af2 & 0xFF;
  cpu.b_ = s->bc2 >> 8;
  cpu.c_ = s->bc2 & 0xFF;
  cpu.d_ = s->de2 >> 8;
  cpu.e_ = s->de2 & 0xFF;
  cpu.h_ = s->hl2 >> 8;
  cpu.l_ = s->hl2 & 0xFF;
  cpu.i = s->i;
  cpu.r = s->r;
  cpu.interrupt_mode = s->im;
  cpu.iff1 = s->iff1;
  cpu.iff2 = s->iff2;
  cpu.halted = s->halt;
  cpu.iff_delay = s->iff_delay;
  cpu.nmi_pending = s->nmi_pending;
  irq_line = s->irq_line;
  nmi_line = s->nmi_line;
  cpu.int_pending = irq_line;
}
