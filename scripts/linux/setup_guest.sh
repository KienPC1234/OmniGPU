#!/usr/bin/env bash
#===============================================================================
# OmniGPU Guest Environment Setup for Linux
#
# Usage:
#   source ./setup_guest_linux.sh                    # interactive mode
#   source ./setup_guest_linux.sh /path/to/omnigpu   # specify install dir
#
# This script exports the environment variables needed for the Vulkan Loader
# to discover the OmniGPU guest ICD, and for OpenGL/OpenCL applications to
# be transparently redirected through OmniGPU.
#
# Variables set:
#   VK_ICD_FILENAMES   → points to omnigpu_guest's vk_icd.json
#   LD_LIBRARY_PATH    → includes omnigpu runtime directory
#   LD_PRELOAD         → preloads omnigpu_guest.so (optional)
#   OMNIGPU_HOST       → target host IP (optional)
#   OMNIGPU_PORT       → target host port (optional)
#===============================================================================


# --- Determine install directory -------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ $# -ge 1 ]; then
    OMNIGPU_DIR="$1"
else
    # Default: look relative to script location
    OMNIGPU_DIR="${SCRIPT_DIR}/.."
fi

# Resolve to absolute path
OMNIGPU_DIR="$(cd "${OMNIGPU_DIR}" 2>/dev/null && pwd)" || {
    echo "[ERROR] Invalid OmniGPU directory: ${OMNIGPU_DIR}"
    return 1
}

# --- Paths -----------------------------------------------------------------
ICD_JSON="${OMNIGPU_DIR}/src/guest/vk_icd.json"
GUEST_SO="${OMNIGPU_DIR}/build/linux/bin/omnigpu_guest.so"
GUEST_DIR="$(dirname "${GUEST_SO}")"

# --- Validate --------------------------------------------------------------
if [ ! -f "${ICD_JSON}" ]; then
    echo "[WARN] vk_icd.json not found at ${ICD_JSON}"
    echo "       Build the project first, or specify the build output directory."
fi

if [ ! -f "${GUEST_SO}" ]; then
    echo "[WARN] omnigpu_guest.so not found at ${GUEST_SO}"
    echo "       Build the project first with: cmake --build --preset linux"
fi

# --- Export environment variables ------------------------------------------
export VK_ICD_FILENAMES="${ICD_JSON}"
echo "[OK] VK_ICD_FILENAMES=${VK_ICD_FILENAMES}"

if [ -d "${GUEST_DIR}" ]; then
    if [[ ":$LD_LIBRARY_PATH:" != *":${GUEST_DIR}:"* ]]; then
        export LD_LIBRARY_PATH="${GUEST_DIR}:${LD_LIBRARY_PATH}"
    fi
    echo "[OK] LD_LIBRARY_PATH includes ${GUEST_DIR}"
fi

# Optional: preload the guest library so all Vulkan apps use it automatically
if [ -f "${GUEST_SO}" ]; then
    export LD_PRELOAD="${GUEST_SO}${LD_PRELOAD:+:$LD_PRELOAD}"
    echo "[OK] LD_PRELOAD=${GUEST_SO}"
fi

# --- Host configuration (optional) -----------------------------------------
if [ -z "${OMNIGPU_HOST:-}" ]; then
    echo "[INFO] OMNIGPU_HOST not set. Default: 127.0.0.1"
    echo "       Set it with: export OMNIGPU_HOST=192.168.1.100"
fi

if [ -z "${OMNIGPU_PORT:-}" ]; then
    echo "[INFO] OMNIGPU_PORT not set. Default: 9443"
fi

# --- Summary ---------------------------------------------------------------
echo ""
echo "============================================"
echo " OmniGPU Guest Environment Ready"
echo "============================================"
echo " Run any Vulkan/OpenGL/OpenCL application:"
echo "   glxinfo | grep 'OpenGL renderer'"
echo "   vulkaninfo | grep 'deviceName'"
echo "   clinfo"
echo ""
echo " To verify OmniGPU is active:"
echo "   grep 'OmniGPU' <(vulkaninfo 2>&1)"
echo "============================================"
