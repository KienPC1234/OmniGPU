#!/usr/bin/env python3
"""
Fetch pre-built FFmpeg shared binaries (Windows) for OmniGPU.
Downloads from BtbN/FFmpeg-Builds GitHub releases.
"""

import argparse
import os
import platform
import re
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

FFMPEG_URLS = {
    "win64": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip",
    "win32": "https://github.com/defisym/FFmpeg-Builds-Win32/releases/download/latest/ffmpeg-n7.1-latest-win32-gpl-shared-7.1.zip",
    "linux64": "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-linux64-gpl-shared.tar.xz",
}


def get_win32_url() -> str:
    url = "https://github.com/defisym/FFmpeg-Builds-Win32/releases"
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    try:
        with urllib.request.urlopen(req) as response:
            html = response.read().decode("utf-8")
        match = re.search(
            r'src="(https://github.com/defisym/FFmpeg-Builds-Win32/releases/expanded_assets/[^"]+)"',
            html,
        )
        if match:
            assets_url = match.group(1)
            assets_req = urllib.request.Request(
                assets_url, headers={"User-Agent": "Mozilla/5.0"}
            )
            with urllib.request.urlopen(assets_req) as assets_response:
                assets_html = assets_response.read().decode("utf-8")
            matches = re.findall(
                r'href="([^"]+win32-gpl-shared[^"]+\.zip)"', assets_html
            )
            if matches:
                return "https://github.com" + matches[0]
    except Exception as e:
        print(f"WARNING: Dynamic win32 URL resolution failed: {e}")
    return FFMPEG_URLS["win32"]


def fetch_ffmpeg(output_dir: Path, arch: str = "win64") -> bool:
    url = FFMPEG_URLS.get(arch)
    if arch == "win32":
        url = get_win32_url()

    if not url:
        print(
            f"ERROR: Unknown architecture '{arch}'. Use win64, win32, or linux64."
        )
        return False

    archive_ext = ".tar.xz" if arch == "linux64" else ".zip"
    archive_path = output_dir.parent / f"ffmpeg-{arch}{archive_ext}"

    print(f"Downloading FFmpeg {arch} shared binaries...")
    try:
        urllib.request.urlretrieve(url, archive_path)
    except Exception as e:
        print(f"ERROR: Failed to download FFmpeg: {e}")
        return False

    print(f"Extracting to {output_dir}...")
    if output_dir.exists():
        shutil.rmtree(output_dir, ignore_errors=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    success = False
    try:
        if archive_ext == ".zip":
            with zipfile.ZipFile(archive_path, "r") as zf:
                prefix = None
                for name in zf.namelist():
                    if (
                        name.endswith("/bin/")
                        or name.endswith("/include/")
                        or name.endswith("/lib/")
                    ):
                        parts = name.rstrip("/").split("/")
                        if len(parts) >= 2:
                            prefix = parts[0]
                            break
                if prefix:
                    for name in zf.namelist():
                        if (
                            name.startswith(prefix + "/bin/")
                            or name.startswith(prefix + "/include/")
                            or name.startswith(prefix + "/lib/")
                        ):
                            rel = "/".join(name.split("/")[1:])
                            dst = output_dir / rel
                            if name.endswith("/"):
                                dst.mkdir(parents=True, exist_ok=True)
                            else:
                                dst.parent.mkdir(parents=True, exist_ok=True)
                                with zf.open(name) as src, open(
                                    dst, "wb"
                                ) as f:
                                    shutil.copyfileobj(src, f)
                else:
                    zf.extractall(output_dir)
            success = True
        elif archive_ext == ".tar.xz":
            import tarfile

            with tarfile.open(archive_path, "r:xz") as tf:
                prefix = None
                for member in tf.getmembers():
                    name = member.name
                    if (
                        name.endswith("/bin")
                        or name.endswith("/include")
                        or name.endswith("/lib")
                        or "/bin/" in name
                        or "/include/" in name
                        or "/lib/" in name
                    ):
                        parts = name.rstrip("/").split("/")
                        if len(parts) >= 2:
                            prefix = parts[0]
                            break
                if prefix:
                    for member in tf.getmembers():
                        name = member.name
                        if (
                            name.startswith(prefix + "/bin/")
                            or name.startswith(prefix + "/include/")
                            or name.startswith(prefix + "/lib/")
                        ):
                            rel = "/".join(name.split("/")[1:])
                            dst = output_dir / rel
                            if member.isdir():
                                dst.mkdir(parents=True, exist_ok=True)
                            else:
                                dst.parent.mkdir(parents=True, exist_ok=True)
                                src_f = tf.extractfile(member)
                                if src_f:
                                    with src_f as src, open(dst, "wb") as f:
                                        shutil.copyfileobj(src, f)
                else:
                    tf.extractall(output_dir)
            success = True
    except Exception as e:
        print(f"ERROR: Failed to extract FFmpeg archive: {e}")
        success = False
    finally:
        if archive_path.exists():
            try:
                archive_path.unlink()
            except Exception as e:
                print(f"WARNING: Failed to remove temporary archive: {e}")

    if success:
        print("FFmpeg downloaded and extracted successfully!")
    return success


def main():
    parser = argparse.ArgumentParser(
        description="Fetch FFmpeg shared binaries for OmniGPU"
    )
    parser.add_argument("--output-dir", default="third_party/ffmpeg-bin")

    default_arch = "linux64" if platform.system() == "Linux" else "win64"
    parser.add_argument(
        "--arch", choices=["win64", "win32", "linux64"], default=default_arch
    )
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()

    success = fetch_ffmpeg(output_dir, args.arch)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
