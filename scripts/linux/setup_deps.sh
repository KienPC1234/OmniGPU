#!/usr/bin/env bash
#===============================================================================
# OmniGPU Linux Dependency Setup
#
# Installs the system toolchain and native dependencies required to build
# OmniGPU on Debian/Ubuntu, then bootstraps vcpkg for the remaining packages.
#
# Usage:
#   sudo ./scripts/linux/setup_deps.sh
#   VCPKG_ROOT=/opt/vcpkg ./scripts/linux/setup_deps.sh
#===============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

if [[ "${EUID}" -ne 0 ]]; then
    echo "[ERROR] Run as root: sudo $0" >&2
    exit 1
fi

echo "=== [1/3] Installing system packages ==="
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
    build-essential \
    ninja-build \
    cmake \
    pkg-config \
    clang \
    clang-tools \
    lld \
    libc++-dev \
    libc++abi-dev \
    libclang-dev \
    libvulkan-dev \
    vulkan-tools \
    mesa-vulkan-drivers \
    libflatbuffers-dev \
    flatbuffers-compiler \
    libfmt-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    liblz4-dev \
    libgtest-dev \
    glslang-tools \
    git \
    curl \
    unzip \
    zip \
    tar \
    ca-certificates \
    python3 \
    python3-pip \
    python3-venv

echo "=== [2/3] Installing Python code-generation dependencies ==="
pip3 install --break-system-packages -r "${PROJECT_ROOT}/gen/requirements.txt"

echo "=== [3/3] Bootstrapping vcpkg ==="
VCPKG_DIR="${VCPKG_ROOT:-${PROJECT_ROOT}/vcpkg}"
if [[ ! -d "${VCPKG_DIR}" ]]; then
    git clone --depth=1 https://github.com/microsoft/vcpkg.git "${VCPKG_DIR}"
fi
"${VCPKG_DIR}/bootstrap-vcpkg.sh" -disableMetrics

echo ""
echo "============================================"
echo "  OmniGPU dependencies installed"
echo "============================================"
echo "  Export VCPKG_ROOT before configuring:"
echo "    export VCPKG_ROOT=${VCPKG_DIR}"
echo "    cmake --preset linux && cmake --build --preset linux"
echo "============================================"
