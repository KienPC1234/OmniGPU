#!/usr/bin/env python3
"""
Fetch pre-built Mesa Zink (OpenGL→Vulkan) binaries for the OmniGPU guest.

Usage:
    python third_party/fetch_zink.py --output-dir third_party/zink

On Windows, this downloads Mesa's opengl32.dll (Zink driver).
On Linux, this is typically installed via system packages but we
provide a script for convenience.
"""

import argparse
import os
import platform
import sys
import urllib.request
import zipfile
import json
import shutil
from pathlib import Path

# Default source: mesa-dist-win pre-built releases
MESA_WIN_RELEASE = "https://github.com/anthroxx/mesa-dist-win/releases/download/v25.1.4/mesa3d-25.1.4-release-msvc.zip"
MESA_WIN_ZINK_PATH = "x64/opengl32.dll"

# On Linux: check system Mesa provides Zink
# Zink is built into Mesa since 21.0; just need to verify it's available
LINUX_CHECK_CMD = "ldconfig -p | grep libGL.so || true"


def fetch_windows(output_dir: Path) -> bool:
    zip_path = output_dir / "mesa.zip"
    mesa_dir = output_dir / "mesa-temp"

    print(f"Downloading Mesa Zink from {MESA_WIN_RELEASE}...")
    try:
        urllib.request.urlretrieve(MESA_WIN_RELEASE, zip_path)
    except Exception as e:
        print(f"ERROR: Failed to download Mesa: {e}")
        return False

    print("Extracting...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(mesa_dir)

    src = mesa_dir / MESA_WIN_ZINK_PATH
    dst = output_dir / "opengl32.dll"
    if src.exists():
        shutil.copy2(src, dst)
        print(f"  -> {dst} ({os.path.getsize(dst)} bytes)")
    else:
        print(f"WARNING: {src} not found in Mesa package")
        # Try to find any opengl32.dll in the extracted files
        for f in mesa_dir.rglob("opengl32.dll"):
            shutil.copy2(f, dst)
            print(f"  -> {dst} (from {f})")
            break
        else:
            print("ERROR: opengl32.dll not found in Mesa package")
            return False

    # Cleanup
    zip_path.unlink()
    shutil.rmtree(mesa_dir, ignore_errors=True)
    return True


def fetch_linux(output_dir: Path) -> bool:
    print("On Linux, Zink is provided by Mesa.")
    print("Install it with: sudo apt install mesa-utils")
    print("Enable Zink with: export MESA_LOADER_DRIVER_OVERRIDE=zink")
    print()
    print("Pre-built download not implemented for Linux.")
    print("We recommend using the system Mesa package.")
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Fetch Mesa Zink binaries for OmniGPU guest"
    )
    parser.add_argument(
        "--output-dir",
        default="third_party/zink",
        help="Output directory for Zink binaries",
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if platform.system() == "Windows":
        success = fetch_windows(output_dir)
    else:
        success = fetch_linux(output_dir)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
