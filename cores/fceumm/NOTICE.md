# FCEUmm core

This module is an alternate NES core. Its emulator is FCEUmm (FCE Ultra "mappers modified") from a downstream Game & Watch Retro-Go snapshot at commit `1961864d746f72310111ba79deaa6caeda41ab78`, with the `external/fceumm-go` tree `c4f14139b994f119abe21bf57393f82d0f6ad579`. The source bundle includes the files used by this build. [Game & Watch Retro-Go upstream](https://github.com/sylverb/game-and-watch-retro-go)

## Licenses

- FCEUmm is Copyright (C) 1998 BERO, (C) 2002 Xodnizel, and the contributors listed in [fceumm/Authors](fceumm/Authors). It is distributed under the GNU GPL v2 or later. The license text is in [fceumm/Copying](fceumm/Copying). Every included source file carries a GPL notice, except headers and data tables from the same tree.
- The MD5 implementation in [fceumm/md5.c](fceumm/md5.c) is by Christophe Devine under the GPL.
- The adapter in [adapter.c](adapter.c) and the platform layer in [port/](port/) are distributed under the GNU GPL v2 or later.

## Excluded upstream components

- `boards/fceu-emu2413.c` and `.h`, the YM2413 emulator used by VRC7 audio. The included copy has no license statement. Mapper 85 (VRC7) is not built.
- `filter.c` and `fcoeffs.h`, the audio output filters. Audio sampling is disabled, and the port supplies no-op filters.
- `cheat.c`, `fds.c`, `fds_apu.c`, `nsf.c`, `unif.c`, `vsuni.c`, the input peripherals, the NTSC filter, and the libretro driver. The Famicom Disk System needs a BIOS and is not supported.
- All other mapper boards. The full set does not fit the 512 KiB module window.

## Mappers

0 NROM, 1 MMC1, 2 UNROM, 3 CNROM, 4 MMC3, 5 MMC5, 7 ANROM, 9 MMC2, 10 MMC4, 11 Color Dreams, 13 CPROM, 16 Bandai 24C02, 18 Jaleco SS880006, 19 Namcot 106, 21, 22, 23, and 25 Konami VRC2/VRC4, 24 and 26 Konami VRC6, 32 Irem G-101, 33 Taito TC0190FMC/TC0350FMR, 34 Irem I-IM/BNROM, 48 Taito TCxxx, 64 Tengen RAMBO-1, 65 Irem H3001, 66 MHROM, 67 Sunsoft FZII, 68 Sunsoft mapper 4, 69 Sunsoft-5/FME-7, 70 Bandai discrete, 71 Camerica BF9093, 72 Jaleco JF-17, 73 Konami VRC3, 75 Konami VRC1, 76 Namcot 108 rev. A, 77 Irem LROG017, 78 Irem 74HC161/32, 79 AVE/C&E/TXC, 80 and 207 Taito X1-005, 82 Taito X1-017, 86 Jaleco JF-13, 87 74×139/74 discrete, 88 Namco 3433, 89 Sunsoft-3, 92 Jaleco JF-19, 93 Sunsoft-3R, 94 HVC-UN1ROM, 95 Namcot 108 rev. B, 96 Bandai Oeka Kids, 97 Irem TAM-S1, 118 TKSROM, 119 NES-TQROM, 140 Jaleco JF-11/14, 152, 154, 158 Tengen 800037, 159 Bandai 24C01, 180, 184, 185, 206 Nintendo DE(1)ROM, 210, and 232 BMC Quattro. Names are those in the upstream mapper table.

## Changes

Each modified upstream file carries a dated `OpenOSC-RetroGo` notice at its top.

- `fceu-cart.c`, `fceu-cart.h`: chip 0 PRG and CHR ROM are read through the port's page cache: eight 8 KiB PRG pages and sixty-four 1 KiB CHR pages. A page stays resident while a CPU, PPU, MMC5, or nametable mapping points into it. New CHR pages are looked up before any mapping entry changes.
- `boards/mmc5.c`, `boards/n106.c`, `boards/68.c`, `pputile.h`: CHR pointers that these boards built directly now come from the cache.
- `ines.c`: the ROM stays in the file. Checksums for the game database are computed by streaming the file. Database tables stay in RAM instead of a Game & Watch flash section.
- `ppu.c`: lines are rendered into a two-line buffer and converted to RGB565 as each line completes. The background shift registers are saved in states and cleared at power-on.
- Allocation uses a per-game arena. Diagnostic printing, `rand`, and the C heap are replaced at build time.

## Behavior

- The `.nes` extension is shared with the `nes` module. The loader chooses between them.
- iNES and NES 2.0 headers are supported, with ROMs up to 8 MiB and an optional trainer. Output is 256×240. PAL and Dendy titles, from NES 2.0 headers or the game database, run at 50 Hz.
- Audio sampling is off. The APU length counters, the frame-counter IRQ, DMC DMA and IRQ, and mapper IRQs run as upstream.
- Controls follow the NES pad. Battery-backed RAM is saved when the header declares a battery. Snapshots use the FCEUmm state format for this build.
