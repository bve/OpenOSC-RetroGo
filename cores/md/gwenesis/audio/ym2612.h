#pragma once
void YM2612Init(void);
void YM2612ResetChip(void);
void YM2612Write(unsigned reg, unsigned value, int master_cycle);
unsigned YM2612Read(int master_cycle);
void md_end_audio_frame(int master_cycle);
