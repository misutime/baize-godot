#!/usr/bin/env python3
"""Cross-platform scons build wrapper for baize-godot.

Replaces build-windows.ps1 and build-macos.sh.
Usage:
    python misc/scripts/build.py --preset dev --jobs 16
    python misc/scripts/build.py -p pro -j 8 -- extra-scons-arg
"""

import argparse
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)

PRESET_MAP: dict[str, dict[str, str]] = {
    "dev": {
        "windows": "misc/customization/scons-profiles/windows_3d_dev.py",
        "darwin": "misc/customization/scons-profiles/macos_3d_dev.py",
    },
    "pro": {
        "windows": "misc/customization/scons-profiles/windows_3d_pro.py",
        "darwin": "misc/customization/scons-profiles/macos_3d_pro.py",
    },
}


def detect_platform() -> str:
    if sys.platform.startswith("win"):
        return "windows"
    if sys.platform == "darwin":
        return "darwin"
    return "linux"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build Godot engine (baize).")
    parser.add_argument("--preset", "-p", choices=["dev", "pro"], default="dev")
    parser.add_argument("--jobs", "-j", type=int, default=8)
    args, extra = parser.parse_known_args()

    platform = detect_platform()
    profile = PRESET_MAP[args.preset].get(platform)
    if not profile:
        print(f"Unsupported platform: {sys.platform}", file=sys.stderr)
        sys.exit(2)

    scons_args = [f"profile={profile}", f"-j{args.jobs}"] + extra
    os.chdir(REPO_ROOT)

    print(f"Running preset '{args.preset}': {' '.join(scons_args)}")

    try:
        subprocess.run(["scons"] + scons_args, check=True)
    except FileNotFoundError:
        subprocess.run(
            [sys.executable, "-m", "SCons.Script"] + scons_args, check=True
        )


if __name__ == "__main__":
    main()
