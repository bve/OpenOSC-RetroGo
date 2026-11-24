# License notices

## Retro-Go upstream projects

- Retro-Go for ODROID-GO is Copyright (C) 2020 Alex Duchesne. Its upstream README lists GNU GPL v2 and component-specific exceptions. [Retro-Go source and credits](https://github.com/ducalex/retro-go), [Retro-Go GPL v2 text](https://github.com/ducalex/retro-go/blob/master/COPYING)
- Game & Watch Retro-Go ports Retro-Go to Nintendo Game & Watch. This project imports several cores from a downstream snapshot of sylverb's fork at commit `1961864d746f72310111ba79deaa6caeda41ab78`. [Original Game & Watch port by kbeckmann](https://github.com/kbeckmann/game-and-watch-retro-go), [sylverb's Game & Watch Retro-Go fork](https://github.com/sylverb/game-and-watch-retro-go), [Game & Watch Retro-Go GPL v2 text](https://github.com/sylverb/game-and-watch-retro-go/blob/msx_wsv_genesis/LICENSE)
- The Game & Watch port uses `retro-go-stm32` for several emulator cores. The imported snapshot contains tree `8e7a660d514da1a80eb5f87a8a35016d3af32adc`. [retro-go-stm32 source](https://github.com/sylverb/retro-go-stm32), [retro-go-stm32 GPL v2 text](https://github.com/sylverb/retro-go-stm32/blob/main/COPYING)

## Components

- The firmware uses OpenOSC drivers and startup code under GNU GPL v3. The license is in the project root. [OpenOSC](https://github.com/bve/OpenOSC)
- The GB/GBC module uses GnuBoy from the `retro-go-stm32` tree. This port adapts ROM bank access, state serialization, RTC handling, and audio register emulation. The corresponding GPL v2 text is included with the core. [GnuBoy COPYING](../cores/gb/gnuboy/COPYING)
- Nofrendo is Copyright (C) 1998–2000 Matthew Conte and contributors. It is distributed under the GNU Library General Public License v2. [Nofrendo copying terms](../cores/nes/nofrendo/COPYING)
- FCEUmm is Copyright (C) 1998 BERO, (C) 2002 Xodnizel, and contributors. It is distributed under GPL v2 or later. [FCEUmm notices](../cores/fceumm/NOTICE.md), [FCEUmm copying terms](../cores/fceumm/fceumm/Copying)
- PCE-Go comes from the Game & Watch Retro-Go `retro-go-stm32` tree. Its source includes GPL notices and its original credits. [PCE-Go copying terms](../cores/pce/pce-go/COPYING)
- ProSystem is Copyright (C) 2005 Greg Stanton and contributors under GPL v2 or later. [ProSystem license](../cores/a7800/prosystem/License.txt)
- SMS Plus GX retains the upstream notices for the emulator. SMS Plus GX, BareMSX, and Gwenesis use superzazu/z80 at `d64fe10a2274e5e40019b1086bf7d8990cbc5f23`, Copyright (c) 2019 Nicolas Allemand, under the MIT License. [SMS Plus GX notices](../cores/sms/NOTICE.md), [Z80 license](../cores/common/z80/LICENSE)
- Atari 2600 emulation uses Tia at `6ae5bf998fe7a7842294089db17fc86fd73db1f0`. [Tia copying terms](../cores/a2600/tia/COPYING), [Tia source](https://github.com/warmenhoven/tia)
- Amstrad CPC emulation uses chips at `7ef8776f637a6424ac615da64de6b968b06de791`. [chips license](../cores/cpc/vendor/LICENSE), [chips source](https://github.com/floooh/chips)
- LCD Game Emulator by bzhxx comes from the Game & Watch Retro-Go tree `51e3ab3af356e1cb684e6997719e54b385993eeb`. The LCD module uses Igor Pavlov's public-domain LZMA decoder and ChaN's TJpgDec R0.03. [LCD Game Emulator source](https://github.com/bzhxx/LCD-Game-Emulator), [LCD Game Emulator license](../cores/gw/lcd/LICENSE), [LZMA decoder notice](../cores/gw/lzma/LzmaDec.c), [TJpgDec notice](../cores/gw/jpeg/tjpgd.c)
- Gwenesis comes from Game & Watch Retro-Go, with its bundled Musashi 68000 CPU. Source files retain component-specific notices. [Gwenesis license file](../cores/md/gwenesis/LICENSE), [Musashi notice](../cores/md/gwenesis/cpu/m68kcpu.c)
- BareMSX comes from commit `66df161d0356bffd9f73b366e663437f2074b621`. V9938 emulation includes work based on MAME's V9938 implementation by Aaron Giles and Nathan Woods. [BareMSX license](../cores/msx/baremsx/LICENSE), [MAME V9938 notice](../cores/msx/baremsx/MAME-V9938-LICENSE)
- TGB Dual comes from the Game & Watch Retro-Go tree `4d5e46bb5ac46072df3b0c8ca768988a5de84a85`. [TGB Dual copying terms](../cores/tgbdual/tgbdual/COPYING)
- O2EM comes from the Game & Watch Retro-Go tree `4a4f305fe71ef5f500fc02c8fef584c110cbe6cc`. [O2EM license](../cores/videopac/o2em/COPYING)
- The Supervision interface draws on Potator from the Game & Watch Retro-Go tree. The CPU code is Copyright (C) 2022 Troy Schrapel and comes from vrEmu6502 at `aae98cb14386d832cb7357c99626520b6590bc24`. [Potator license](../cores/wsv/Potator-LICENSE), [vrEmu6502 license](../cores/wsv/vremu6502/LICENSE)
- TamaLIB is Copyright (C) 2021 Jean-Christophe Rona and is distributed under GPL v2 or later. [TamaLIB license](../cores/tama/tamalib/LICENSE)
- Arm CMSIS Cortex-M7 is Copyright (c) 2009–2019 Arm Limited under Apache 2.0. STM32H7 HAL is Copyright 2017 STMicroelectronics under BSD 3-Clause. The OpenOSC CMSIS STM32H7B0xx headers retain the 2019 STMicroelectronics notice. [CMSIS license](CMSIS-Apache-2.0.txt), [HAL license](STM32-HAL.txt)
- FatFs R0.16 w/patch 2 is Copyright (C) 2025 ChaN. [FatFs notice](FatFs.txt)
- The original Pixel Lab and core test ROM generators and their output are licensed under CC0-1.0.

Release source archives include the matching firmware and core sources and their build rules.
