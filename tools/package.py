#!/usr/bin/env python3
"""Assemble the combined firmware image and package a loadable Retro-Go core."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zlib

CORE_HEADER = struct.Struct("<8s17I4H16s32s12B")
CORE_BASE = 0x24080000
CORE_CAPACITY = 512 * 1024
HEADER_BYTES = 1024
SLOT_BASE = 0x081A0000
SLOT_VECTOR = SLOT_BASE + HEADER_BYTES
SLOT_PAYLOAD = 383 * 1024


def symbols(nm, elf):
    return {fields[2]: int(fields[0], 16)
            for line in subprocess.check_output([nm, "-n", elf], text=True).splitlines()
            if len(fields := line.split()) == 3 and fields[1] != "U"}


def core(elf, binary, manifest, nm):
    syms = symbols(nm, elf)
    metadata = json.loads(manifest.read_text())
    text_size = syms["__text_end"] - CORE_BASE
    bss_offset = syms["__bss_start"] - CORE_BASE
    bss_size = syms["__bss_end"] - syms["__bss_start"]
    entries = [((syms[name] - CORE_BASE) | 1) if name in syms else 0
               for name in ("rg_open", "rg_run", "rg_save", "rg_load")]
    payload = binary.read_bytes()
    if (not 0 < text_size <= bss_offset or len(payload) != bss_offset
            or bss_offset % 32 or bss_offset + bss_size > CORE_CAPACITY
            or not all(entries[:2])
            or any(offset and offset >= text_size for offset in entries)):
        raise ValueError("core section or entry outside the ABI memory window")
    extensions = b"".join(s.encode().ljust(8, b"\0") for s in metadata["extensions"])
    header = bytearray(CORE_HEADER.pack(
        b"RGCORE1", 1, metadata.get("abi", 1), CORE_HEADER.size, CORE_BASE,
        text_size, bss_offset - text_size, bss_offset, bss_size,
        zlib.crc32(payload), 0, *entries, *metadata["period_us"],
        metadata["max_rom_size"], metadata["width"], metadata["height"],
        metadata["action_mask"], 0, metadata["id"].encode(), extensions, *metadata["defaults"]))
    struct.pack_into("<I", header, 44, zlib.crc32(header))
    return bytes(header) + payload


def check_vectors(payload, origin, name):
    if len(payload) < 8:
        raise ValueError(f"{name} has no complete vector table")
    sp, pc = struct.unpack_from("<II", payload)
    if sp & 7 or not 0x20000000 < sp <= 0x2001FF00:
        raise ValueError(f"{name} initial stack must be aligned in DTCM")
    if not pc & 1 or not origin + 8 <= pc & ~1 < origin + len(payload):
        raise ValueError(f"{name} reset vector outside linked payload")


def retrogo_image(payload, version, board_id):
    encoded = version.encode("ascii")
    if not encoded or len(encoded) > 31 or not all(32 <= b <= 126 for b in encoded):
        raise ValueError("version must contain 1..31 printable ASCII characters")
    if len(payload) > SLOT_PAYLOAD:
        raise ValueError("Retro-Go payload does not fit 383 KiB slot")
    check_vectors(payload, SLOT_VECTOR, "Retro-Go")
    header = bytearray(HEADER_BYTES)
    struct.pack_into("<8s6I32s", header, 0, b"OPENOSC1", 1, HEADER_BYTES, board_id,
                     SLOT_VECTOR, len(payload), zlib.crc32(payload), encoded)
    struct.pack_into("<I", header, 64, zlib.crc32(header))
    return bytes(header) + payload


def combined_image(app, retrogo, app_version, game_version, board, upstream):
    prefix_size = SLOT_BASE - upstream.LOAD_ADDRESS
    if (upstream.HEADER_BYTES != HEADER_BYTES or upstream.LOAD_ADDRESS != 0x08040400 or
            board.flash_end != SLOT_VECTOR + SLOT_PAYLOAD):
        raise ValueError("unsupported OpenOSC Flash layout")
    if len(app) > prefix_size:
        raise ValueError("OpenOSC overlaps the Retro-Go header")
    program_size = len(app) - upstream.TRAILER.size
    if upstream.read_trailer(app) != (board.image_board_id, program_size):
        raise ValueError("missing or incorrect OpenOSC build marker")
    program = app[:program_size]
    if zlib.crc32(program) != upstream.TRAILER.unpack_from(app, program_size)[4]:
        raise ValueError("OpenOSC program CRC does not match its build marker")
    check_vectors(program, upstream.LOAD_ADDRESS, "OpenOSC")
    embedded = retrogo_image(retrogo, game_version, board.image_board_id)
    payload = app.ljust(prefix_size, b"\xff") + embedded
    return upstream.package(payload, f"{app_version}+RG{game_version}", board)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("binary", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()
    package = core(args.elf, args.binary, args.manifest, args.nm)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)
    print(f"{args.output}: {len(package)} bytes")


if __name__ == "__main__":
    main()
