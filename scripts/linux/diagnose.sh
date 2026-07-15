#!/usr/bin/env bash

# OmniGPU Linux Diagnosis Tool
# Verifies the status of the Host Server and Guest Driver setup

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0;5m' # No Color
GRAY='\033[0;90m'

echo -e "${CYAN}=============================================${NC}"
echo -e "${CYAN}       OmniGPU Linux Diagnosis Tool          ${NC}"
echo -e "${CYAN}=============================================${NC}"
echo ""

errors=0
warnings=0

# ---------------------------------------------------------
# 1. Check Host Server Status
# ---------------------------------------------------------
echo -e "${YELLOW}[1/3] Checking OmniGPU Host Server...${NC}"

# Check process
host_pid=$(pgrep -f "omnigpu_host" || true)
if [ -n "$host_pid" ]; then
    echo -e "  ${GREEN}[OK] omnigpu_host is running (PID: $host_pid)${NC}"
else
    echo -e "  ${YELLOW}[WARN] omnigpu_host is NOT running.${NC}"
    warnings=$((warnings + 1))
fi

# Check port 50051
if command -v ss &> /dev/null; then
    port_active=$(ss -tlnp | grep -q ":50051" && echo "yes" || echo "no")
elif command -v netstat &> /dev/null; then
    port_active=$(netstat -tlnp | grep -q ":50051" && echo "yes" || echo "no")
else
    port_active="unknown"
fi

if [ "$port_active" = "yes" ]; then
    echo -e "  ${GREEN}[OK] TCP Port 50051 is active and listening${NC}"
elif [ "$port_active" = "no" ]; then
    if [ -n "$host_pid" ]; then
        echo -e "  ${RED}[ERROR] omnigpu_host is running, but port 50051 is not listening!${NC}"
        errors=$((errors + 1))
    else
        echo -e "  ${GRAY}[--] Port 50051 is inactive (server stopped)${NC}"
    fi
fi

# Detected Host GPUs via lspci
if command -v lspci &> /dev/null; then
    gpus=$(lspci | grep -i -E "vga|3d|display" || true)
    if [ -n "$gpus" ]; then
        echo -e "  ${GREEN}[OK] Detected Host GPU(s):${NC}"
        echo "$gpus" | while read -r line; do
            echo -e "    - $line"
        done
    fi
else
    echo -e "  ${YELLOW}[WARN] lspci not found, cannot detect physical GPUs.${NC}"
fi
echo ""

# ---------------------------------------------------------
# 2. Check Guest Driver Setup
# ---------------------------------------------------------
echo -e "${YELLOW}[2/3] Checking OmniGPU Guest Driver...${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bin_dir="$SCRIPT_DIR/../build/debug/bin"
if [ ! -d "$bin_dir" ]; then
    bin_dir="$SCRIPT_DIR/../bin"
fi

if [ -d "$bin_dir" ]; then
    echo -e "  ${GREEN}[OK] Binary directory found at: $bin_dir${NC}"
else
    echo -e "  ${RED}[ERROR] Binary output directory not found! Have you built the project?${NC}"
    exit 1
fi

# Check SO
guest_so="$bin_dir/libomnigpu_guest.so"
if [ -f "$guest_so" ]; then
    echo -e "  ${GREEN}[OK] libomnigpu_guest.so exists ($(stat -c%s "$guest_so") bytes)${NC}"
else
    # Fallback check for DLL if compiled on Wine/Cross-build
    guest_dll="$bin_dir/omnigpu_guest.dll"
    if [ -f "$guest_dll" ]; then
        echo -e "  ${GREEN}[OK] omnigpu_guest.dll exists (Windows ICD build)${NC}"
    else
        echo -e "  ${RED}[ERROR] libomnigpu_guest.so / omnigpu_guest.dll is missing!${NC}"
        errors=$((errors + 1))
    fi
fi

# Check ICD JSON
icd_json="$bin_dir/vk_icd.json"
if [ -f "$icd_json" ]; then
    echo -e "  ${GREEN}[OK] vk_icd.json exists${NC}"
    if command -v jq &> /dev/null; then
        lib_path=$(jq -r '.ICD.library_path' "$icd_json" 2>/dev/null || true)
        echo -e "    - ICD library_path: $lib_path"
    else
        echo -e "    - ICD content: $(cat "$icd_json")"
    fi
else
    echo -e "  ${RED}[ERROR] vk_icd.json is missing!${NC}"
    errors=$((errors + 1))
fi

# Check env
if [ -n "$VK_ICD_FILENAMES" ]; then
    echo -e "  ${GREEN}[OK] VK_ICD_FILENAMES is set to: $VK_ICD_FILENAMES${NC}"
else
    echo -e "  ${YELLOW}[WARN] VK_ICD_FILENAMES is not set. Setup environment or use helper script.${NC}"
    warnings=$((warnings + 1))
fi
echo ""

# ---------------------------------------------------------
# 3. Check Translation Layers
# ---------------------------------------------------------
echo -e "${YELLOW}[3/3] Checking Graphics Translation Layers...${NC}"

# On Linux, Zink is standard. Check if MESA is configured for Zink
if [ "$MESA_LOADER_DRIVER_OVERRIDE" = "zink" ] || [ "$GALLIUM_DRIVER" = "zink" ]; then
    echo -e "  ${GREEN}[OK] Gallium Zink driver override active${NC}"
else
    echo -e "  ${GRAY}[--] Zink driver override not set (normal behavior unless forced)${NC}"
fi

# Check clvk
clvk_so="$bin_dir/libOpenCL.so"
if [ -f "$clvk_so" ] || [ -f "$bin_dir/OpenCL.dll" ]; then
    echo -e "  ${GREEN}[OK] clvk translation layer deployed${NC}"
else
    echo -e "  ${YELLOW}[WARN] clvk (libOpenCL.so) is missing. OpenCL compute translation unavailable.${NC}"
    warnings=$((warnings + 1))
fi

echo ""
echo -e "${CYAN}=============================================${NC}"
if [ $errors -eq 0 ]; then
    echo -e " ${GREEN}Diagnosis Completed: SYSTEM STABLE ($errors errors, $warnings warnings)${NC}"
else
    echo -e " ${RED}Diagnosis Completed: $errors ERROR(S) FOUND! Fix before running.${NC}"
fi
echo -e "${CYAN}=============================================${NC}"
