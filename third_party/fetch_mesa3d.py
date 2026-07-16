#!/usr/bin/env python3
"""
Fetch Mesa3D MSVC pre-built binaries for the OmniGPU guest.
Downloads the release from pal1000/mesa-dist-win and extracts it.
"""

import argparse
import os
import platform
import subprocess
import sys
import urllib.request
import shutil
from pathlib import Path

MESA3D_URL = "https://github.com/pal1000/mesa-dist-win/releases/download/26.1.3/mesa3d-26.1.3-release-msvc.7z"


def fetch_windows(output_dir: Path) -> bool:
    archive_path = output_dir.parent / "mesa3d.7z"

    print(f"Downloading Mesa3D from {MESA3D_URL}...")
    try:
        urllib.request.urlretrieve(MESA3D_URL, archive_path)
    except Exception as e:
        print(f"ERROR: Failed to download Mesa3D: {e}")
        return False

    print(f"Cleaning output directory: {output_dir}")
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Extracting 7z archive...")
    try:
        # Call 7z to extract directly to output_dir
        subprocess.run(["7z", "x", str(archive_path), f"-o{output_dir}", "-y"], check=True)
    except Exception as e:
        print(f"ERROR: Failed to extract Mesa3D archive: {e}")
        if archive_path.exists():
            archive_path.unlink()
        return False

    # Delete the downloaded archive
    if archive_path.exists():
        archive_path.unlink()
    print("Mesa3D fetched and extracted successfully!")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Fetch Mesa3D binaries for OmniGPU guest"
    )
    parser.add_argument("--output-dir", default="third_party/mesa3d")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()

    if platform.system() == "Windows":
        success = fetch_windows(output_dir)
    else:
        print("Mesa3D fetch is only supported on Windows. On Linux, please use the system Mesa.")
        success = True  # Non-blocking on non-Windows

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
