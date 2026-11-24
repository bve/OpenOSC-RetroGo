#!/usr/bin/env python3
"""Adapt OpenOSC's clock profiles and SPI source for the Retro-Go runtime."""
import argparse
from pathlib import Path
import re


def profiles(text):
    def add_modes(match):
        start, body, end = match.groups()
        modes = body.rstrip().rstrip(",")
        for mhz in (340, 360):
            if not re.search(rf"overclockedRunMode\(\s*{mhz}\s*,|\b{mhz}000000\b", body):
                modes += f",\n    overclockedRunMode({mhz}, 4)"
        return start + modes + "\n" + end

    text, count = re.subn(r"(\bRunMode\s+RunModes\[\]\s*=\s*\{)(.*?)(\};)",
                          add_modes, text, flags=re.DOTALL)
    if count != 1:
        raise ValueError("Cannot find the OpenOSC clock profile table")
    return text


def spi(text):
    for before, after in (("RCC_SPI45CLKSOURCE_PCLK2", "RCC_SPI45CLKSOURCE_HSI"),
                          ("SPI_BAUDRATEPRESCALER_16", "SPI_BAUDRATEPRESCALER_8")):
        if text.count(before) != 1:
            raise ValueError(f"Cannot find the OpenOSC SPI setting {before}")
        text = text.replace(before, after)
    return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kind", choices=("profiles", "spi"))
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    transform = profiles if args.kind == "profiles" else spi
    output = transform(args.source.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)
