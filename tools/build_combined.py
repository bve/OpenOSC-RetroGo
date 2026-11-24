#!/usr/bin/env python3
"""Build OpenOSC and Retro-Go as one standard OpenOSC application image."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from package import combined_image, SLOT_BASE

ROOT = Path(__file__).resolve().parents[1]
MODELS = {"zt703s": "firmware", "zt706s": "zt706s_firmware"}


def git(path, *args):
    return subprocess.check_output(["git", "-C", path, *args], text=True).strip()


class SourceCheckout:
    def __init__(self, source, destination):
        self.source = source
        self.destination = destination
        marker = destination / ".retrogo-combined-checkout"
        if not marker.is_file():
            if destination.exists() and any(destination.iterdir()):
                raise ValueError(f"build checkout directory is already in use: {destination}")
            destination.mkdir(parents=True, exist_ok=True)
            git(destination, "init", "--quiet")
            marker.touch()

    def fetch(self, source, ref, name):
        target = f"refs/retrogo/{name}"
        git(self.destination, "fetch", "--quiet", "--no-tags", "--no-write-fetch-head", "--update-shallow",
            source, f"+{ref}:{target}")
        return git(self.destination, "rev-parse", target)

    def select(self, ref):
        if ref:
            commit = git(self.source, "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}")
            return self.fetch(self.source, commit, "pinned")

        local = self.fetch(self.source, "refs/heads/main", "local")
        if "origin" not in git(self.source, "remote").splitlines():
            return local
        remote = git(self.source, "remote", "get-url", "origin")
        if ":" not in remote and not Path(remote).is_absolute():
            remote = str(self.source / remote)
        upstream = self.fetch(remote, "refs/heads/main", "upstream")
        ancestry = subprocess.run(["git", "-C", self.destination, "merge-base", "--is-ancestor",
                                   upstream, local], check=False)
        if ancestry.returncode not in (0, 1):
            raise subprocess.CalledProcessError(ancestry.returncode, ancestry.args)
        return local if ancestry.returncode == 0 else upstream

    def prepare(self, commit):
        branch = subprocess.run(["git", "-C", self.destination, "rev-parse", "--verify", "HEAD"],
                                text=True, capture_output=True)
        if branch.returncode != 0 or branch.stdout.strip() != commit:
            git(self.destination, "checkout", "--detach", "--force", "--quiet", commit)
        self.overlay(commit)

    def overlay(self, commit):
        def replace(path, anchor, replacement, prefix=""):
            command = ["git", "-C", self.destination, "show", f"{commit}:{path}"]
            stock = subprocess.check_output(command, text=True)
            if stock.count(anchor) != 1:
                raise ValueError(f"OpenOSC {commit[:12]} changed the integration point in {path}")
            result = prefix + stock.replace(anchor, replacement, 1)
            if (self.destination / path).read_text() != result:
                (self.destination / path).write_text(result)

        entry = "    if (!Board::initialize(bootFrame)) Watchdog::fatal(boot_diagnostics.error);\n"
        replace("src/application/main.cpp", entry, entry + "    tryRetroGo();\n",
                '#include "retro_go_image.hpp"\n')

        limit = "LENGTH = __openosc_flash_end - 0x08040400"
        replace("ld/application.ld", limit, f"LENGTH = 0x{SLOT_BASE:08X} - 0x08040400")
        crc_entry = "    +<update/crc32.cpp>\n"
        replace("platformio.ini", crc_entry, crc_entry + "    +<update/image.cpp>\n")
        for source in sorted((ROOT / "integration/openosc").glob("retro_go_*")):
            target = self.destination / "src/application" / source.name
            data = source.read_bytes()
            if not target.exists() or target.read_bytes() != data:
                target.write_bytes(data)


def run(args, env=None):
    subprocess.run(args, cwd=ROOT, env=env, check=True)


def native_install_check(source, dist, build, runtime, host_cxx, board_config):
    for model in MODELS:
        board = board_config.load(model)
        binary = build / f"check-{model}"
        flags = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", f"-DOPENOSC_BOARD={board.model}"]
        includes = ["-I" + str(path) for path in
                    ("tests/support", "integration/openosc", runtime / "generated",
                     source / "include", source / "vendor/fatfs")]
        sources = ["file_validation.cpp", "installer.cpp", "image.cpp", "image_file.cpp", "crc32.cpp"]
        run([host_cxx, *flags, *includes, "tests/combined_host.cpp", "tests/support/memory_fat.cpp",
             *(source / "src/update" / name for name in sources),
             runtime / "tests/ff.o", runtime / "tests/ffunicode.o", "-o", binary])
        other = next(name for name in MODELS if model != name)
        run([binary, dist / f"openosc-retro-go-{model}.osc",
             dist / f"openosc-retro-go-{other}.osc"])
        print(f"{model}: stock Recovery install checks passed", flush=True)


def write_artifact(path, data):
    staged = path.with_name(path.name + ".tmp")
    staged.write_bytes(data)
    staged.replace(path)


def build(args, checkout, commit):
    build_dir = args.build
    pio_dir = build_dir / "platformio"
    runtime = build_dir / "runtime"
    output = build_dir / "dist"
    env = os.environ | {"PLATFORMIO_BUILD_DIR": str(pio_dir), "PYTHONDONTWRITEBYTECODE": "1"}
    print(f"OpenOSC source: {commit}", flush=True)
    arguments = [args.pio, "run", "--project-dir", checkout]
    for environment in MODELS.values():
        arguments += ["-e", environment]
    run(arguments, env)
    run([args.make, f"OPENOSC={checkout}", f"BUILD={runtime}",
         f"ARM_PREFIX={args.arm_prefix}", f"PROFILE={args.profile}",
         f"HOST_CXX={args.host_cxx}", "runtime-check" if args.check else "runtime"])

    sys.path.insert(0, str(checkout / "scripts"))
    import board_config
    import package_image

    app_version = (checkout / "VERSION").read_text().strip()
    game_version = (ROOT / "VERSION").read_text().strip()
    provenance = {"openosc_commit": commit, "openosc_version": app_version,
                  "retrogo_version": game_version, "cores": [], "models": {}}
    artifacts = {}
    for recipe in sorted((ROOT / "cores").glob("*/module.mk")):
        name = f"{recipe.parent.name}.rgc"
        artifacts[name] = (runtime / "dist" / name).read_bytes()
        metadata = json.loads(recipe.with_name("manifest.json").read_text())
        provenance["cores"].append({"file": name, "abi": metadata.get("abi", 1),
                                    "extensions": metadata["extensions"]})
    for model, name in MODELS.items():
        app = (pio_dir / name / "firmware.bin").read_bytes()
        game = (runtime / model / "retro-go.bin").read_bytes()
        board = board_config.load(model)
        filename = f"openosc-retro-go-{model}.osc"
        artifacts[filename] = combined_image(app, game, app_version, game_version, board, package_image)
        provenance["models"][model] = {"file": filename, "openosc_bytes": len(app),
                                       "retrogo_bytes": len(game), "package_bytes": len(artifacts[filename])}

    output.mkdir(parents=True, exist_ok=True)
    for name, payload in artifacts.items():
        write_artifact(output / name, payload)
        print(f"{output / name}: {len(payload)} bytes", flush=True)
    manifest = json.dumps(provenance, indent=2) + "\n"
    checksums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                        for name, data in sorted(artifacts.items()))
    write_artifact(output / "build.json", manifest.encode())
    write_artifact(output / "SHA256SUMS", checksums.encode())

    if args.check:
        native_install_check(checkout, output, build_dir, runtime, args.host_cxx, board_config)
        run([sys.executable, "-m", "unittest", "discover", "-s", "tests"],
            env | {"RETROGO_BUILD": str(build_dir)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openosc", type=Path, required=True)
    parser.add_argument("--ref", default="")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--pio", default="pio")
    parser.add_argument("--make", default="make")
    parser.add_argument("--arm-prefix", required=True)
    parser.add_argument("--host-cxx", default="c++")
    parser.add_argument("--profile", choices=["0", "1"], default="0")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    args.openosc = args.openosc.resolve()
    args.build = args.build.resolve()
    if args.build.is_relative_to(args.openosc):
        parser.error("choose a build directory outside the OpenOSC checkout")
    args.build.mkdir(parents=True, exist_ok=True)
    try:
        with (args.build / ".lock").open("w") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            source = SourceCheckout(args.openosc, args.build / "openosc")
            commit = source.select(args.ref)
            if not re.fullmatch(r"[0-9a-f]{40,64}", commit):
                raise ValueError("OpenOSC source reference did not resolve to a commit")
            source.prepare(commit)
            build(args, source.destination, commit)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
