# OpenOSC-RetroGo

My nephew loves playing with my scopemeter, so I put together this build to give him a few retro games to try. It's just a little side project for fun.

The build combines OpenOSC and Retro-Go for the ZT703S and ZT706S. It installs through stock OpenOSC Recovery and loads emulator cores from the scopemeter's FAT volume.

| At power-on | Opens |
| --- | --- |
| No key held | OpenOSC |
| **F4** | Retro-Go |
| **F2** | OpenOSC Recovery. F2 also takes priority if both keys are held. |

## Install

Use the `.osc` file for your model (`openosc-retro-go-zt703s.osc` or `openosc-retro-go-zt706s.osc`) and the `.rgc` modules for the systems you want to play. Both are available in release downloads and local builds.

1. Boot with **F2**. Copy the matching `.osc` to `/openosc/UPDATE` on the scopemeter's USB drive and eject it.
2. Choose **Update Firmware** in Recovery, check the target model, and confirm. The package updates both OpenOSC and Retro-Go.
3. Press **F4** to return to the Recovery menu and reconnect the drive. Copy the `.rgc` modules to `/openosc/games/cores`, ROMs to `/openosc/games/roms`, and any required BIOS files to `/openosc/games/bios`. You can create the directories on your computer. Retro-Go also creates them on its first launch.
4. Eject the drive and restart with **F4** held until the library appears. Release it, select a ROM with the directional keys, and press **F1** to play.

If the library finds multiple emulators for a file, use **Up/Down** and **F1** to choose one.

## Supported cores

| Module | Systems | Game images |
| --- | --- | --- |
| `a2600.rgc` | Atari 2600 | `.a26`, `.a2600` |
| `a7800.rgc` | Atari 7800 | `.a78`, `.a7800` |
| `cpc.rgc` | Amstrad CPC6128 | `.dsk` |
| `fceumm.rgc` | NES (FCEUmm) | `.nes` |
| `gb.rgc` | Game Boy and Game Boy Color (GnuBoy) | `.gb`, `.gbc` |
| `gw.rgc` | Game & Watch LCD | `.gw` |
| `md.rgc` | Mega Drive and Genesis | `.md`, `.gen` |
| `msx.rgc` | MSX, MSX2, and MSX2+ | `.msx`, `.mx2`, `.mxp`, `.dsk` |
| `nes.rgc` | NES (Nofrendo) | `.nes` |
| `pce.rgc` | PC Engine and TurboGrafx-16 | `.pce` |
| `sms.rgc` | Master System, Game Gear, SG-1000, and ColecoVision | `.sms`, `.gg`, `.sg`, `.col` |
| `tama.rgc` | Tamagotchi P1 | `.tama` |
| `tgbdual.rgc` | Game Boy and Game Boy Color (TGB Dual) | `.gb`, `.gbc` |
| `videopac.rgc` | Videopac and Odyssey² | `.o2`, `.videopac` |
| `wsv.rgc` | Watara Supervision | `.wsv`, `.sv` |

Place any required BIOS files under `/openosc/games/bios`.

| System | Required files |
| --- | --- |
| Amstrad CPC6128 | `cpc6128-os.rom`, `cpc6128-basic.rom`, `amsdos.rom` (16 KiB each) |
| ColecoVision | `colecovision.rom` (8 KiB) |
| MSX | `msx.rom` (32 KiB) |
| MSX2 | `msx2.rom` (32 KiB), `msx2-ext.rom` (16 KiB), and `msx-disk.rom` (16 KiB) for disks |
| MSX2+ | `msx2plus.rom` (32 KiB) and `msx2plus-ext.rom` (16 KiB) |
| Videopac and Odyssey² | `odyssey2.rom` (1 KiB) and `odyssey2-vdc.bin` (512 bytes) |
| Philips Videopac+ | A compatible G7400 BIOS as `odyssey2.rom`, plus `odyssey2-vdc.bin` and `videopac-vpp.bin` (2.5 KiB) |

## Play and save

The directional pad handles movement by default. F1/F2 are Start/Select, and F3/F4 are A/B. On Mega Drive and Genesis, F1/F2 are C/Start. Hold **MENU** for half a second to pause. In menus, **MENU** or F1 confirms and F2 goes back or cancels.

Choose **Controls / Button mapping** from the pause menu or press F3 in the library to edit the selected core's bindings. Select an action, press **MENU**, then press the physical key you want to assign. **AUTO**, **HOLD**, and **MODE** can be assigned to any action supported by the core. Press **MENU** to confirm the new binding or F2 to cancel at the confirmation screen. On a conflict the two actions exchange their bindings. If the selected action was unbound, the other action becomes unbound. Use F3 to clear a binding and F4 to restore the defaults.

Press **F2** in the library or select **Settings** in the pause menu to adjust the display or CPU clock. Under **Brightness**, use Left and Right to preview levels from 10% to 100% and **MENU** to save your choice. The menu header shows the estimated battery charge and voltage.

Under **CPU clock**, choose 200, 280, 340, or 360 MHz and press **MENU** to apply the speed until the next restart. The 340 and 360 MHz settings are experimental overclocks. Retro-Go starts at 200 MHz after a restart.

Cores with battery-backed SRAM load it when you open a game. Use **Save cartridge** to save it on demand. Retro-Go also saves it when exiting a game and every 90 seconds during play. **Save state** and **Load state** handle emulator snapshots on cores that provide them. Saves and mappings live under `/openosc/games/saves` and `/openosc/games/controls`. Choose **Back to OpenOSC** in the pause menu or press F4 in the library to restart into OpenOSC.

Hold the power button for two seconds to shut down from a game or menu. Retro-Go saves cartridge data and flushes storage before switching off.

Audio output is disabled.

## Build locally

Use Linux with Git, GNU Make, Python 3.9 or later, PlatformIO, and a checkout of [OpenOSC](https://github.com/bve/OpenOSC).

```sh
make OPENOSC=../OpenOSC
```

The build compares the local OpenOSC `main` against `origin/main`. It selects local `main` if it contains the remote tip and selects the remote tip otherwise. To pin the source, set `OPENOSC_REF` to a local tag or commit. PlatformIO installs the ARM toolchain selected by OpenOSC. Set `ARM_PREFIX` to use another installation.

The two combined packages, all built core modules, build provenance, and checksums go to `build/combined/dist`. Use `make PROFILE=1` for an instrumented build under `build/profile/combined`.

## Automated releases

GitHub Actions checks for new stable OpenOSC releases every 10 minutes and when the release workflow or `VERSION` changes on `main`. It builds both board packages, verifies the release checksums, and publishes the packages with all core modules and source code. [Release workflow](.github/workflows/release.yml)

## Licenses

See the [license and component notices](LICENSES/README.md).
