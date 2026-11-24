#pragma once
void z80_write_ctrl(unsigned address, unsigned value);
unsigned z80_read_ctrl(unsigned address);
void z80_start(void);
void z80_pulse_reset(void);
void z80_run(int target);
void z80_set_memory(unsigned char *buffer);
void z80_irq_line(unsigned value);
void md_end_z80_frame(int master_cycle);
