#!/usr/bin/env python3
"""
Fetch pre-built Mesa Zink (OpenGL→Vulkan) binaries for the OmniGPU guest.

Usage:
    python third_party/fetch_zink.py --output-dir third_party/zink
    python third_party/fetch_zink.py --output-dir third_party/zink --version latest

On Windows, this downloads Mesa's opengl32.dll (Zink driver) along with
the Gallium helper DLLs (libglapi.dll, libgallium_wc.dll) needed for
global OpenGL forwarding. Both 64-bit and 32-bit versions are extracted.

DLLs required for Zink to work (deployed to System32/SysWOW64):
  - opengl32.dll          OpenGL → Gallium state tracker (loader)
  - libgallium_wgl.dll    Gallium driver core (Zink + softpipe + WGL)
  - d3d12.dll             D3D12 Gallium driver (optional, for D3D)
  - dxil.dll              D3D12 shader compiler (optional)

NOTE: The Mesa MSVC build (pal1000/mesa-dist-win) does NOT use
libglapi.dll or libgallium_wc.dll. Everything is in opengl32.dll
+ libgallium_wgl.dll.
"""

import argparse
import json
import os
import platform
import re
import sys
import urllib.request
import zipfile
import subprocess
import shutil
from pathlib import Path

# GitHub API for latest release
GITHUB_API = "https://api.github.com/repos/pal1000/mesa-dist-win/releases/latest"
FALLBACK_URL = "https://github.com/pal1000/mesa-dist-win/releases/download/26.1.3/mesa3d-26.1.3-release-msvc.7z"


def get_latest_release_url() -> str:
    print(f"Checking latest Mesa Zink release via {GITHUB_API}...")
    try:
        req = urllib.request.Request(GITHUB_API, headers={"User-Agent": "OmniGPU"})
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            tag = data.get("tag_name", "")
            assets = data.get("assets", [])
            # Find the MSVC 7z asset
            for asset in assets:
                name = asset.get("name", "")
                if name.endswith("-release-msvc.7z"):
                    url = asset.get("browser_download_url", "")
                    print(f"  Latest release: {tag} -> {name}")
                    return url
    except Exception as e:
        print(f"  API failed ({e}), using fallback URL")

    print(f"  Using fallback: {FALLBACK_URL}")
    return FALLBACK_URL


def fetch_windows(output_dir: Path, version: str) -> bool:
    if version == "latest":
        url = get_latest_release_url()
    else:
        url = FALLBACK_URL

    archive_path = output_dir / "mesa.7z"
    mesa_dir = output_dir / "mesa-temp"

    print(f"Downloading Mesa Zink from {url}...")
    try:
        urllib.request.urlretrieve(url, archive_path)
    except Exception as e:
        print(f"ERROR: Failed to download Mesa: {e}")
        return False

    print("Extracting 7z archive...")
    try:
        # Recreate temp dir
        shutil.rmtree(mesa_dir, ignore_errors=True)
        mesa_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(["7z", "x", str(archive_path), f"-o{mesa_dir}", "-y"], check=True)
    except Exception as e:
        print(f"ERROR: Failed to extract Mesa archive: {e}")
        archive_path.unlink(missing_ok=True)
        shutil.rmtree(mesa_dir, ignore_errors=True)
        return False

    # Extract both 64-bit and 32-bit Mesa DLLs
    # mesa-dist-win archive layout:
    #   x64/opengl32.dll, x64/libglapi.dll, x64/libgallium_wc.dll, ...
    #   x86/opengl32.dll, x86/libglapi.dll, x86/libgallium_wc.dll, ...

    ZINK_DLLS = [
        "opengl32.dll",
        "libgallium_wgl.dll",
    ]

    OPTIONAL_DLLS = [
        "d3d12.dll",
        "dxil.dll",
        "dxcompiler.dll",
    ]

    def copy_arch(src_arch: str, dst_dir: Path) -> bool:
        arch_dir = mesa_dir / src_arch
        if not arch_dir.is_dir():
            print(f"  [--] Architecture directory not found: {src_arch}")
            return False
        found_any = False
        for dll in ZINK_DLLS:
            src = arch_dir / dll
            if src.exists():
                dst = dst_dir / dll
                shutil.copy2(src, dst)
                size_mb = os.path.getsize(dst) / (1024 * 1024)
                print(f"  -> {dst} ({size_mb:.1f} MB)")
                found_any = True
        return found_any

    # 64-bit → output_dir/
    found_64 = copy_arch("x64", output_dir)
    # 32-bit → output_dir/x86/
    x86_dir = output_dir / "x86"
    x86_dir.mkdir(parents=True, exist_ok=True)
    found_32 = copy_arch("x86", x86_dir)

    if not found_64 and not found_32:
        # Try rglob as fallback (older package format)
        for dll in ZINK_DLLS:
            for f in mesa_dir.rglob(dll):
                if "x86" in str(f) or "32" in str(f):
                    dst = x86_dir / f.name
                else:
                    dst = output_dir / f.name
                shutil.copy2(f, dst)
                print(f"  -> {dst} ({os.path.getsize(dst)} bytes)")
                found_64 = True

    if not found_64:
        print("ERROR: opengl32.dll not found in Mesa package")
        shutil.rmtree(mesa_dir, ignore_errors=True)
        archive_path.unlink(missing_ok=True)
        return False

    # Also copy optional D3D12 helper DLLs (both archs)
    for extra in OPTIONAL_DLLS:
        for f in mesa_dir.rglob(extra):
            if "x86" in str(f) or "32" in str(f):
                dst = x86_dir / f.name
            else:
                dst = output_dir / f.name
            shutil.copy2(f, dst)
            print(f"  -> {dst} ({os.path.getsize(dst)} bytes)")
            break

    archive_path.unlink(missing_ok=True)
    shutil.rmtree(mesa_dir, ignore_errors=True)
    return True


def fetch_linux(output_dir: Path) -> bool:
    print("Linux: Zink is provided by the system Mesa package.")
    print("  Install: sudo apt install mesa-utils")
    print("  Enable:  export MESA_LOADER_DRIVER_OVERRIDE=zink")
    print("  Or:      export GALLIUM_DRIVER=zink")
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Fetch Mesa Zink binaries for OmniGPU guest"
    )
    parser.add_argument("--output-dir", default="third_party/zink")
    parser.add_argument("--version", default="latest",
                        help="Version tag or 'latest'")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if platform.system() == "Windows":
        success = fetch_windows(output_dir, args.version)
    else:
        success = fetch_linux(output_dir)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
