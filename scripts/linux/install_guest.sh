#!/usr/bin/env bash
#===============================================================================
# OmniGPU Guest Installer for Linux
#
# Usage:
#   sudo ./scripts/install_guest_linux.sh                    # auto-detect build
#   sudo ./scripts/install_guest_linux.sh --build-dir build/linux
#   sudo ./scripts/install_guest_linux.sh --prefix /usr/local
#   sudo ./scripts/install_guest_linux.sh --uninstall
#
# This script:
#   1. Copies omnigpu_guest.so + vk_icd.json to system directories
#   2. Registers the ICD in /usr/share/vulkan/icd.d/ for auto-discovery
#   3. Configures LD_LIBRARY_PATH persistence
#===============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
BUILD_DIR=""
PREFIX="/usr"
UNINSTALL=false

# --- Parse arguments --------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --prefix)    PREFIX="$2"; shift 2 ;;
        --uninstall) UNINSTALL=true; shift ;;
        --help)
            echo "OmniGPU Guest Installer for Linux"
            echo ""
            echo "Usage:"
            echo "  sudo $0                              # auto-detect"
            echo "  sudo $0 --build-dir build/linux"
            echo "  sudo $0 --prefix /usr/local"
            echo "  sudo $0 --uninstall"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# --- Auto-detect build dir --------------------------------------------------
if [[ -z "$BUILD_DIR" ]]; then
    for candidate in build/linux build/release build/default; do
        if [[ -f "${PROJECT_ROOT}/${candidate}/bin/omnigpu_guest.so" ]]; then
            BUILD_DIR="${PROJECT_ROOT}/${candidate}"
            echo "[INFO] Auto-detected build: ${BUILD_DIR}"
            break
        fi
    done
fi

if [[ -z "$BUILD_DIR" || ! -f "${BUILD_DIR}/bin/omnigpu_guest.so" ]]; then
    echo "[ERROR] Build not found. Specify --build-dir or build the project first."
    exit 1
fi

# --- Paths ------------------------------------------------------------------
LIB_DIR="${PREFIX}/lib/omnigpu"
ICD_DIR="${PREFIX}/share/vulkan/icd.d"
BIN_DIR="${BUILD_DIR}/bin"
ICD_JSON_SRC="${PROJECT_ROOT}/src/guest/vk_icd.json"
ICD_JSON_DST="${ICD_DIR}/omnigpu_guest.json"
THIRD_PARTY="${PROJECT_ROOT}/third_party"

# ============================================================================
# Uninstall
# ============================================================================
if [[ "$UNINSTALL" == true ]]; then
    echo "=== OmniGPU Guest Uninstall ==="

    # Remove ICD manifest
    if [[ -f "${ICD_JSON_DST}" ]]; then
        rm -f "${ICD_JSON_DST}"
        echo "[OK] Removed: ${ICD_JSON_DST}"
    fi

    # Remove library
    if [[ -d "${LIB_DIR}" ]]; then
        rm -rf "${LIB_DIR}"
        echo "[OK] Removed: ${LIB_DIR}"
    fi

    # Remove ldconfig config
    if [[ -f /etc/ld.so.conf.d/omnigpu.conf ]]; then
        rm -f /etc/ld.so.conf.d/omnigpu.conf
        ldconfig
        echo "[OK] Removed ldconfig config"
    fi

    echo "=== Uninstall complete ==="
    exit 0
fi

# ============================================================================
# Install
# ============================================================================
echo "=== OmniGPU Guest Install ==="
echo "  Build dir: ${BUILD_DIR}"
echo "  Prefix:    ${PREFIX}"
echo "  Lib dir:   ${LIB_DIR}"
echo "  ICD dir:   ${ICD_DIR}"
echo ""

# Create directories
mkdir -p "${LIB_DIR}"
mkdir -p "${ICD_DIR}"

# Copy guest library
cp "${BIN_DIR}/omnigpu_guest.so" "${LIB_DIR}/"
chmod 755 "${LIB_DIR}/omnigpu_guest.so"
echo "[OK] Copied: omnigpu_guest.so -> ${LIB_DIR}/"

# Copy vk_icd.json
if [[ -f "${ICD_JSON_SRC}" ]]; then
    # Update library_path in ICD manifest to absolute path
    sed -E "s|\"[^\"]*omnigpu_guest\\.dll\"|\"${LIB_DIR}/omnigpu_guest.so\"|g" \
        "${ICD_JSON_SRC}" > "${ICD_JSON_DST}"
    chmod 644 "${ICD_JSON_DST}"
    echo "[OK] Registered ICD: ${ICD_JSON_DST}"
    echo "     library_path -> ${LIB_DIR}/omnigpu_guest.so"
fi

# Copy translation layers
if [[ -f "${THIRD_PARTY}/clvk/libOpenCL.so" ]]; then
    cp "${THIRD_PARTY}/clvk/libOpenCL.so" "${LIB_DIR}/" 2>/dev/null || true
    echo "[OK] Copied: clvk/libOpenCL.so"
fi
for lib in "${THIRD_PARTY}/clvk/"*.so*; do
    if [[ -f "$lib" ]]; then
        cp "$lib" "${LIB_DIR}/" 2>/dev/null || true
    fi
done

# Copy binary test tool if present
if [[ -f "${BIN_DIR}/omnigpu_guest_test" ]]; then
    cp "${BIN_DIR}/omnigpu_guest_test" "${LIB_DIR}/"
    chmod 755 "${LIB_DIR}/omnigpu_guest_test"
    echo "[OK] Copied: omnigpu_guest_test"
fi

# Configure ldconfig so libvulkan can find omnigpu_guest.so
echo "${LIB_DIR}" > /etc/ld.so.conf.d/omnigpu.conf
ldconfig
echo "[OK] Configured ldconfig: /etc/ld.so.conf.d/omnigpu.conf"

# ============================================================================
# Summary
# ============================================================================
echo ""
echo "=== Installation Complete ==="
echo "  Library:  ${LIB_DIR}/omnigpu_guest.so"
echo "  ICD:      ${ICD_JSON_DST}"
echo ""
echo "  Vulkan apps will now automatically discover OmniGPU via ICD."
echo "  Run 'vulkaninfo' to verify."
echo ""
echo "  To uninstall: sudo $0 --uninstall"
echo "================================="
