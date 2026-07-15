#!/usr/bin/env python3
"""
Fetch pre-built clvk (OpenCL→Vulkan) binaries for the OmniGPU guest.

Usage:
    python third_party/fetch_clvk.py --output-dir third_party/clvk
    python third_party/fetch_clvk.py --output-dir third_party/clvk --version latest

Auto-detects the latest release from GitHub when --version latest is used.
"""

import argparse
import json
import os
import platform
import sys
import urllib.request
import zipfile
import tarfile
import shutil
from pathlib import Path

GITHUB_API = "https://api.github.com/repos/kpet/clvk/releases/latest"
FALLBACK_WIN = "https://github.com/kpet/clvk/releases/download/v0.0.1/clvk-windows-x64.zip"
FALLBACK_LINUX = "https://github.com/kpet/clvk/releases/download/v0.0.1/clvk-linux-x64.tar.gz"


def get_latest_release_urls():
    print(f"Checking latest clvk release via {GITHUB_API}...")
    win_url = FALLBACK_WIN
    linux_url = FALLBACK_LINUX
    try:
        req = urllib.request.Request(GITHUB_API, headers={"User-Agent": "OmniGPU"})
        with urllib.request.urlopen(req, timeout=10) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            tag = data.get("tag_name", "")
            for asset in data.get("assets", []):
                name = asset.get("name", "")
                url = asset.get("browser_download_url", "")
                if "windows" in name.lower() and name.endswith(".zip"):
                    win_url = url
                elif "linux" in name.lower() and name.endswith(".tar.gz"):
                    linux_url = url
            print(f"  Latest release: {tag}")
    except Exception as e:
        print(f"  API failed ({e}), using fallback URLs")
    return win_url, linux_url


def fetch_windows(output_dir: Path, url: str) -> bool:
    zip_path = output_dir / "clvk.zip"
    extract_dir = output_dir / "clvk-temp"

    print(f"Downloading clvk from {url}...")
    try:
        urllib.request.urlretrieve(url, zip_path)
    except Exception as e:
        print(f"ERROR: Failed to download clvk: {e}")
        return False

    print("Extracting...")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(extract_dir)

    found = False
    for dll in extract_dir.rglob("OpenCL.dll"):
        shutil.copy2(dll, output_dir / "OpenCL.dll")
        print(f"  -> {output_dir / 'OpenCL.dll'} ({os.path.getsize(output_dir / 'OpenCL.dll')} bytes)")
        found = True
        break

    if not found:
        print("WARNING: OpenCL.dll not found")
        zip_path.unlink(missing_ok=True)
        shutil.rmtree(extract_dir, ignore_errors=True)
        return False

    for extra in ["clvk.dll", "clspv.dll"]:
        for f in extract_dir.rglob(extra):
            shutil.copy2(f, output_dir / extra)
            print(f"  -> {output_dir / extra}")

    zip_path.unlink(missing_ok=True)
    shutil.rmtree(extract_dir, ignore_errors=True)
    return True


def fetch_linux(output_dir: Path, url: str) -> bool:
    tgz_path = output_dir / "clvk.tar.gz"
    extract_dir = output_dir / "clvk-temp"

    print(f"Downloading clvk from {url}...")
    try:
        urllib.request.urlretrieve(url, tgz_path)
    except Exception as e:
        print(f"ERROR: Failed to download clvk: {e}")
        return False

    print("Extracting...")
    with tarfile.open(tgz_path, "r:gz") as tf:
        tf.extractall(extract_dir)

    found = False
    for lib in extract_dir.rglob("libOpenCL.so*"):
        dst = output_dir / lib.name
        shutil.copy2(lib, dst)
        print(f"  -> {dst} ({os.path.getsize(dst)} bytes)")
        found = True

    if not found:
        print("WARNING: libOpenCL.so not found")
        tgz_path.unlink(missing_ok=True)
        shutil.rmtree(extract_dir, ignore_errors=True)
        return False

    for extra in ["libclvk.so", "libclspv.so"]:
        for f in extract_dir.rglob(extra):
            shutil.copy2(f, output_dir / extra)
            print(f"  -> {output_dir / extra}")

    tgz_path.unlink(missing_ok=True)
    shutil.rmtree(extract_dir, ignore_errors=True)
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Fetch clvk binaries for OmniGPU guest"
    )
    parser.add_argument("--output-dir", default="third_party/clvk")
    parser.add_argument("--version", default="latest",
                        help="Version tag or 'latest'")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    win_url, linux_url = get_latest_release_urls()

    if platform.system() == "Windows":
        success = fetch_windows(output_dir, win_url)
    else:
        success = fetch_linux(output_dir, linux_url)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
