#!/usr/bin/env python3
"""
Fetch pre-built FFmpeg shared binaries (Windows) for OmniGPU.
Downloads from BtbN/FFmpeg-Builds GitHub releases.
"""

import argparse
import os
import platform
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

FFMPEG_URLS = {
    "win64": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip",
    "win32": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win32-gpl-shared.zip",
}


def fetch_windows(output_dir: Path, arch: str = "win64") -> bool:
    if arch not in FFMPEG_URLS:
        print(f"ERROR: Unknown architecture '{arch}'. Use win64 or win32.")
        return False
    archive_path = output_dir.parent / f"ffmpeg-{arch}.zip"

    print(f"Downloading FFmpeg {arch} shared binaries...")
    try:
        urllib.request.urlretrieve(FFMPEG_URLS[arch], archive_path)
    except Exception as e:
        print(f"ERROR: Failed to download FFmpeg: {e}")
        return False

    print(f"Extracting to {output_dir}...")
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        with zipfile.ZipFile(archive_path, "r") as zf:
            # Find the bin/ include/ lib/ dirs inside the archive
            prefix = None
            for name in zf.namelist():
                if name.endswith("/bin/") or name.endswith("/include/") or name.endswith("/lib/"):
                    parts = name.rstrip("/").split("/")
                    if len(parts) >= 2:
                        prefix = parts[0]
                        break
            if prefix:
                for name in zf.namelist():
                    if name.startswith(prefix + "/bin/") or name.startswith(prefix + "/include/") or name.startswith(prefix + "/lib/"):
                        rel = "/".join(name.split("/")[1:])
                        dst = output_dir / rel
                        if name.endswith("/"):
                            dst.mkdir(parents=True, exist_ok=True)
                        else:
                            dst.parent.mkdir(parents=True, exist_ok=True)
                            with zf.open(name) as src, open(dst, "wb") as f:
                                shutil.copyfileobj(src, f)
            else:
                zf.extractall(output_dir)
    except Exception as e:
        print(f"ERROR: Failed to extract FFmpeg archive: {e}")
        if archive_path.exists():
            archive_path.unlink()
        return False

    if archive_path.exists():
        archive_path.unlink()
    print("FFmpeg downloaded and extracted successfully!")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Fetch FFmpeg shared binaries for OmniGPU"
    )
    parser.add_argument("--output-dir", default="third_party/ffmpeg-bin")
    parser.add_argument("--arch", choices=["win64", "win32"], default="win64")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()

    if platform.system() == "Windows":
        success = fetch_windows(output_dir, args.arch)
    else:
        print("FFmpeg fetch is only supported on Windows. On Linux, use system packages.")
        success = True

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
