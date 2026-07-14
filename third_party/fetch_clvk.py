#!/usr/bin/env python3
"""
Fetch pre-built clvk (OpenCL→Vulkan) binaries for the OmniGPU guest.

Usage:
    python third_party/fetch_clvk.py --output-dir third_party/clvk

clvk is an OpenCL implementation on top of Vulkan.
It allows OpenCL applications to run through our Vulkan intercept layer.
"""

import argparse
import os
import platform
import sys
import urllib.request
import zipfile
import tarfile
import shutil
from pathlib import Path

# Default source: clvk pre-built releases
CLVK_WIN_RELEASE = "https://github.com/kpet/clvk/releases/download/v0.0.1/clvk-windows-x64.zip"
CLVK_LINUX_RELEASE = "https://github.com/kpet/clvk/releases/download/v0.0.1/clvk-linux-x64.tar.gz"


def fetch_windows(output_dir: Path) -> bool:
    zip_path = output_dir / "clvk.zip"
    extract_dir = output_dir / "clvk-temp"

    print(f"Downloading clvk from {CLVK_WIN_RELEASE}...")
    try:
        urllib.request.urlretrieve(CLVK_WIN_RELEASE, zip_path)
    except Exception as e:
        print(f"ERROR: Failed to download clvk: {e}")
        return False

    print("Extracting...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(extract_dir)

    # Find and copy OpenCL.dll
    dlls = list(extract_dir.rglob("OpenCL.dll"))
    if dlls:
        shutil.copy2(dlls[0], output_dir / "OpenCL.dll")
        print(f"  -> {output_dir / 'OpenCL.dll'} ({os.path.getsize(output_dir / 'OpenCL.dll')} bytes)")
    else:
        print("WARNING: OpenCL.dll not found in clvk package")
        return False

    # Copy any other relevant DLLs (clvk-specific)
    for extra in ["clvk.dll", "clspv.dll"]:
        found = list(extract_dir.rglob(extra))
        if found:
            shutil.copy2(found[0], output_dir / extra)
            print(f"  -> {output_dir / extra}")

    zip_path.unlink()
    shutil.rmtree(extract_dir, ignore_errors=True)
    return True


def fetch_linux(output_dir: Path) -> bool:
    tgz_path = output_dir / "clvk.tar.gz"
    extract_dir = output_dir / "clvk-temp"

    print(f"Downloading clvk from {CLVK_LINUX_RELEASE}...")
    try:
        urllib.request.urlretrieve(CLVK_LINUX_RELEASE, tgz_path)
    except Exception as e:
        print(f"ERROR: Failed to download clvk: {e}")
        return False

    print("Extracting...")
    with tarfile.open(tgz_path, "r:gz") as tf:
        tf.extractall(extract_dir)

    libs = list(extract_dir.rglob("libOpenCL.so*"))
    if libs:
        for lib in libs:
            dst = output_dir / lib.name
            shutil.copy2(lib, dst)
            print(f"  -> {dst} ({os.path.getsize(dst)} bytes)")
    else:
        print("WARNING: libOpenCL.so not found in clvk package")
        return False

    for extra in ["libclvk.so", "libclspv.so"]:
        found = list(extract_dir.rglob(extra))
        if found:
            shutil.copy2(found[0], output_dir / extra)
            print(f"  -> {output_dir / extra}")

    tgz_path.unlink()
    shutil.rmtree(extract_dir, ignore_errors=True)
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Fetch clvk binaries for OmniGPU guest"
    )
    parser.add_argument(
        "--output-dir",
        default="third_party/clvk",
        help="Output directory for clvk binaries",
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
