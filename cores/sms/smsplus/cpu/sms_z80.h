/* Z80 CPU interface used by SMS Plus GX in OpenOSC-RetroGo.
 *
 * This file replaces the MAME-derived z80.h, cpuintrf.h, and osd_cpu.h. The
 * CPU is the MIT-licensed superzazu/z80 core in ../../z80. This interface is
 * distributed under the GNU GPL v2 or later, like SMS Plus GX. */
#ifndef SMS_Z80_H_
#define SMS_Z80_H_

#include <stdint.h>

/* Interrupt line states and inputs */
enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };
enum { INPUT_LINE_IRQ0 = 0, INPUT_LINE_NMI = 32 };

/* Serializable CPU state. Register pairs hold the high byte in bits 15-8. */
typedef struct
{
  uint16_t pc, sp, ix, iy, wz;
  uint16_t af, bc, de, hl;
  uint16_t af2, bc2, de2, hl2;
  uint8_t i, r, im, iff1;
  uint8_t iff2, halt, iff_delay, nmi_pending;
  uint8_t irq_line, nmi_line, reserved[2];
} z80_state_t;

/* Running total of cycles executed in the current frame */
extern int z80_cycle_count;

void z80_init(int index, int clock, const void *config, int (*irqcallback)(int));
void z80_reset(void);
int z80_execute(int cycles);
void z80_set_irq_line(int irqline, int state);
void z80_reset_cycle_count(void);
int z80_get_elapsed_cycles(void);
uint16_t z80_get_pc(void);
void z80_get_state(z80_state_t *state);
int z80_state_valid(const z80_state_t *state);
void z80_set_state(const z80_state_t *state); /* Requires a valid state */

/* 1K memory pages and I/O handlers supplied by the system emulation */
extern unsigned char *cpu_readmap[64];
extern unsigned char *cpu_writemap[64];
extern void (*cpu_writemem16)(int address, int data);
extern void (*cpu_writeport16)(uint16_t port, uint8_t data);
extern uint8_t (*cpu_readport16)(uint16_t port);

#endif /* SMS_Z80_H_ */
