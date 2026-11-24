# SMS Plus GX core

This module runs Master System, Game Gear, SG-1000, and ColecoVision ROMs. Its emulator is SMS Plus GX from a downstream Game & Watch Retro-Go snapshot at commit `1961864d746f72310111ba79deaa6caeda41ab78`, with the `retro-go-stm32` tree `8e7a660d514da1a80eb5f87a8a35016d3af32adc`, from `retro-go-stm32/smsplusgx-go/components/smsplus`. [Game & Watch Retro-Go upstream](https://github.com/sylverb/game-and-watch-retro-go), [retro-go-stm32 upstream](https://github.com/sylverb/retro-go-stm32)

## Licenses

- SMS Plus GX is Copyright (C) 1998-2007 Charles MacDonald, with additional code by Eke-Eke. It is distributed under the GNU GPL v2 or later. The license text is in [smsplus/LICENSE](smsplus/LICENSE).
- The Z80 CPU in [z80/](../common/z80/) is [superzazu/z80](https://github.com/superzazu/z80) at commit `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, Copyright (c) 2019 Nicolas Allemand, under the MIT License. The files `z80.c`, `z80.h`, and `LICENSE` are unmodified. The license text is in [z80/LICENSE](../common/z80/LICENSE).
- The adapter in [adapter.c](adapter.c) and the CPU glue in [smsplus/cpu/sms_z80.c](smsplus/cpu/sms_z80.c) and [smsplus/cpu/sms_z80.h](smsplus/cpu/sms_z80.h) are distributed under the GNU GPL v2 or later.

## Excluded upstream files

- `coleco_bios.h`, the embedded ColecoVision BIOS. Supply it as `/openosc/games/bios/colecovision.rom` (8192 bytes).
- `sound/sn76489.*`, `sound/emu2413.*`, `sound/ym2413.*`, `sound/fmintf.*`, and the tone tables. The host has no audio output.
- `state.c`, `state.h`, and the `load_rom` function of `loadrom.c`. The adapter implements ROM access and snapshots.
- `cpu/z80.c`, `cpu/z80.h`, `cpu/cpuintrf.h`, and `cpu/osd_cpu.h`, the MAME-derived Z80 core and its headers. The superzazu core replaces them.
- `PORTING` and the CMake files.

## Changes

Modified files carry a notice in their headers.

- Cartridge ROM is read through a cache of fourteen 16 KiB pages. Pages that the Z80 memory map references stay resident. ROMs can be up to 4 MiB, with an optional 512-byte copier header.
- The glue builds the superzazu core into one translation unit and keeps the upstream CPU interface. Cycles are charged when an instruction starts, a pending interrupt is taken before the next instruction, IRQ is level-sensitive, and NMI triggers on a rising edge. The core passes ZEXDOC and ZEXALL through the glue. Its register and cycle trace matched the replaced core for the first 100 million ZEXALL instructions.
- The 256 KiB bitplane table, the 64 KiB sprite priority table, and the 32 KiB TMS multicolor table are computed on demand.
- Lines are converted to RGB565 as they are rendered, so palette changes take effect per line.
- The SMS background renderer has a margin for the clipped first column of a scrolled line, and it uses unaligned-safe line buffer access.
- `z80_read_unmapped()` indexes the 1 KiB memory map correctly and reads the program counter through the glue.
- The SMS BIOS slot is not emulated. A game that disables the cartridge reads unmapped memory.
- The sound interface discards PSG and FM writes. These chips raise no interrupts and have no CPU-readable registers. The Mark III FM detection latch keeps its upstream behavior with FM disabled.

## Behavior

- File extensions select the system: `.sms`, `.gg`, `.sg`, and `.col`. The header and the upstream game database refine SMS and Game Gear settings, including mapper, region, PAL timing, and Game Gear SMS mode.
- Output is 256×192 for SMS, SG-1000, and ColecoVision, and 160×144 for Game Gear. Codemasters SMS titles use 256×224. Other 224- and 240-line modes are centered and cropped. PAL titles run at 50 Hz.
- Button 1 is B, and button 2 is A. Start is Pause on SMS and SG-1000, and Start on Game Gear. On ColecoVision, Start with no direction, Up, Right, Down, Left, A, or B enters keypad 1, 2, 3, 4, 5, 6, or 0. Select with the same keys enters #, \*, 9, 8, 7, 6, or 5.
- Cartridge SRAM is 32 KiB and is saved after a game maps it. Snapshots are about 58 KiB.
- Paddle, Light Phaser, and Sports Pad titles are detected but have no analog input.
