#!/usr/bin/env bash
#===============================================================================
# OmniGPU Build + Package for Linux
#
# Clean build and assemble everything into a single deployable tarball:
#   build/dist/OmniGPU-v{version}-linux.tar.gz
#
# Usage:
#   ./scripts/linux/build-and-package.sh
#   ./scripts/linux/build-and-package.sh --skip-build
#   ./scripts/linux/build-and-package.sh --install
#===============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="0.1.0"
DIST_DIR="${PROJECT_ROOT}/build/dist/OmniGPU-v${VERSION}"
BUILD_DIR="${PROJECT_ROOT}/build/linux"
SKIP_BUILD=false
INSTALL=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build) SKIP_BUILD=true; shift ;;
        --install)    INSTALL=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ============================================================================
# Step 1: Configure & Build
# ============================================================================
if [[ "$SKIP_BUILD" == false ]]; then
    echo "=== [1/4] Configuring & Building Linux Targets ==="
    cmake --preset linux
    cmake --build "${BUILD_DIR}" --clean-first --parallel
    echo "  [OK] Build complete"
fi

# ============================================================================
# Step 2: Assemble distribution package
# ============================================================================
echo "=== [2/4] Assembling Distribution Package ==="
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"
mkdir -p "${DIST_DIR}/bin"
mkdir -p "${DIST_DIR}/scripts"
mkdir -p "${DIST_DIR}/docs"

# Copy libraries and binaries
cp "${BUILD_DIR}/bin/omnigpu_guest.so" "${DIST_DIR}/bin/" 2>/dev/null || true
cp "${BUILD_DIR}/bin/omnigpu_host" "${DIST_DIR}/bin/" 2>/dev/null || true
cp "${PROJECT_ROOT}/src/guest/vk_icd.json" "${DIST_DIR}/bin/" 2>/dev/null || true

# Copy clvk libraries if present in third_party
if [[ -d "${PROJECT_ROOT}/third_party/clvk" ]]; then
    mkdir -p "${DIST_DIR}/third_party/clvk"
    cp -r "${PROJECT_ROOT}/third_party/clvk"/* "${DIST_DIR}/third_party/clvk/" 2>/dev/null || true
fi

# Copy scripts and configurations
cp "${PROJECT_ROOT}/scripts/linux/install_guest.sh" "${DIST_DIR}/scripts/" 2>/dev/null || true
cp "${PROJECT_ROOT}/scripts/linux/diagnose.sh" "${DIST_DIR}/scripts/" 2>/dev/null || true
cp "${PROJECT_ROOT}/omnigpu_guest.json" "${DIST_DIR}/" 2>/dev/null || true
cp "${PROJECT_ROOT}/omnigpu_host.json" "${DIST_DIR}/" 2>/dev/null || true

# Copy docs
cp "${PROJECT_ROOT}/README.md" "${DIST_DIR}/docs/" 2>/dev/null || true
cp "${PROJECT_ROOT}/AGENTS.md" "${DIST_DIR}/docs/" 2>/dev/null || true

# ============================================================================
# Step 3: Create tarball package
# ============================================================================
echo "=== [3/4] Creating tarball package ==="
TARBALL="${PROJECT_ROOT}/build/dist/OmniGPU-v${VERSION}-linux.tar.gz"
cd "${PROJECT_ROOT}/build/dist"
tar -czf "${TARBALL}" "OmniGPU-v${VERSION}"
echo "[OK] Created: ${TARBALL}"

# ============================================================================
# Step 4: Optionally install
# ============================================================================
if [[ "$INSTALL" == true ]]; then
    echo "=== [4/4] Installing system-wide ==="
    sudo "${DIST_DIR}/scripts/install_guest.sh" --build-dir "${BUILD_DIR}"
fi

echo ""
echo "============================================"
echo "  OmniGPU v${VERSION} packaged successfully!"
echo "  Package: ${DIST_DIR}"
echo "  Tarball: ${TARBALL}"
echo "============================================"
