#!/usr/bin/env bash
# OmniGPU Linux diagnosis tool.
set -o pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PORT="${OMNIGPU_PORT:-}"
if [[ -z "${PORT}" ]] && command -v python3 >/dev/null 2>&1; then
    for host_config in \
        "${PROJECT_ROOT}/omnigpu_host.json" \
        "${PROJECT_ROOT}/etc/omnigpu/omnigpu_host.json" \
        "/etc/omnigpu/omnigpu_host.json"; do
        if [[ -f "${host_config}" ]]; then
            PORT="$(python3 - "${host_config}" <<'PY' 2>/dev/null || true
import json
import pathlib
import sys
value = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")).get("port")
if isinstance(value, int) and not isinstance(value, bool) and 1 <= value <= 65535:
    print(value)
PY
)"
            [[ -n "${PORT}" ]] && break
        fi
    done
fi
PORT="${PORT:-9443}"
if [[ ! "${PORT}" =~ ^[0-9]+$ ]] || (( PORT < 1 || PORT > 65535 )); then
    echo "Invalid OMNIGPU_PORT: ${PORT}" >&2
    exit 2
fi
errors=0
warnings=0

ok() { echo -e "  ${GREEN}[OK]${NC} $*"; }
warn() { echo -e "  ${YELLOW}[WARN]${NC} $*"; warnings=$((warnings + 1)); }
fail() { echo -e "  ${RED}[ERROR]${NC} $*"; errors=$((errors + 1)); }

printf "%b\n" "${CYAN}=== OmniGPU Linux Diagnosis ===${NC}"

echo "[1/4] Host runtime"
if pgrep -x omnigpu_host >/dev/null 2>&1; then
    ok "omnigpu_host process is running"
elif command -v systemctl >/dev/null 2>&1 && systemctl is-active --quiet omnigpu-host.service 2>/dev/null; then
    ok "omnigpu-host.service is active"
else
    warn "host is not running"
fi

if command -v ss >/dev/null 2>&1; then
    if ss -H -ltn "sport = :${PORT}" 2>/dev/null | grep -q .; then
        ok "TCP port ${PORT} is listening"
    else
        warn "TCP port ${PORT} is not listening"
    fi
else
    warn "ss is not installed; port check skipped"
fi

if command -v lspci >/dev/null 2>&1; then
    gpu_lines="$(lspci | grep -iE 'vga|3d|display' || true)"
    [[ -n "${gpu_lines}" ]] && ok "PCI display device detected" || warn "no PCI display device detected"
fi

echo "[2/4] Build/install artifacts"
manifest=""
for candidate in \
    "${PROJECT_ROOT}/build/linux/src/guest/vk_icd.json" \
    "${PROJECT_ROOT}/share/vulkan/icd.d/omnigpu_guest.json" \
    "/usr/share/vulkan/icd.d/omnigpu_guest.json" \
    "/usr/local/share/vulkan/icd.d/omnigpu_guest.json"; do
    if [[ -f "${candidate}" ]]; then manifest="${candidate}"; break; fi
done
[[ -n "${manifest}" ]] && ok "ICD manifest: ${manifest}" || fail "OmniGPU ICD manifest not found"

guest_library=""
if [[ -n "${manifest}" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        if guest_library="$(python3 - "${manifest}" <<'PY' 2>/dev/null
import json
import pathlib
import sys
manifest = pathlib.Path(sys.argv[1])
data = json.loads(manifest.read_text(encoding="utf-8"))
if not isinstance(data, dict) or not isinstance(data.get("ICD"), dict):
    raise SystemExit(1)
library_value = data["ICD"].get("library_path")
if not isinstance(library_value, str) or not library_value:
    raise SystemExit(1)
path = pathlib.Path(library_value)
if not path.is_absolute():
    path = (manifest.parent / path).resolve()
print(path)
PY
)"; then
            if [[ ! -f "${guest_library}" ]]; then
                fail "manifest library_path does not exist: ${guest_library}"
                guest_library=""
            fi
        else
            fail "ICD manifest is malformed or unreadable: ${manifest}"
            guest_library=""
        fi
    else
        warn "python3 is not installed; ICD manifest parsing skipped"
    fi
fi
if [[ -z "${guest_library}" ]]; then
    for candidate in \
        "${PROJECT_ROOT}/build/linux/lib/libomnigpu_guest.so" \
        "${PROJECT_ROOT}/lib/omnigpu/libomnigpu_guest.so" \
        "/usr/lib/omnigpu/libomnigpu_guest.so" \
        "/usr/local/lib/omnigpu/libomnigpu_guest.so"; do
        if [[ -f "${candidate}" ]]; then guest_library="${candidate}"; break; fi
    done
fi
[[ -n "${guest_library}" ]] && ok "guest ICD: ${guest_library}" || fail "libomnigpu_guest.so not found"

host_binary=""
for candidate in \
    "${PROJECT_ROOT}/build/linux/bin/omnigpu_host" \
    "${PROJECT_ROOT}/bin/omnigpu_host" \
    "/usr/bin/omnigpu_host" \
    "/usr/local/bin/omnigpu_host"; do
    if [[ -x "${candidate}" ]]; then host_binary="${candidate}"; break; fi
done
[[ -n "${host_binary}" ]] && ok "host binary: ${host_binary}" || warn "omnigpu_host binary not found"

if [[ -n "${guest_library}" ]] && command -v ldd >/dev/null 2>&1; then
    if dependencies="$(ldd "${guest_library}" 2>&1)"; then
        missing="$(printf '%s\n' "${dependencies}" | grep 'not found' || true)"
        [[ -z "${missing}" ]] && ok "guest shared-library dependencies resolve" || fail "missing dependencies: ${missing}"
    else
        fail "ldd could not inspect ${guest_library}: ${dependencies}"
    fi
fi

echo "[3/4] Vulkan loader"
if [[ -n "${VK_DRIVER_FILES:-}" ]]; then
    ok "VK_DRIVER_FILES=${VK_DRIVER_FILES}"
elif [[ -n "${VK_ICD_FILENAMES:-}" ]]; then
    warn "VK_ICD_FILENAMES is set; VK_DRIVER_FILES is preferred by newer loaders"
else
    ok "using system ICD discovery paths"
fi

if command -v vulkaninfo >/dev/null 2>&1; then
    VULKANINFO_LOG="$(mktemp "${TMPDIR:-/tmp}/omnigpu-vulkaninfo.XXXXXX")" || {
        fail "could not create a private vulkaninfo log"
        VULKANINFO_LOG=""
    }
    if [[ -n "${VULKANINFO_LOG}" ]]; then
        chmod 0600 "${VULKANINFO_LOG}"
        if command -v timeout >/dev/null 2>&1; then
            vulkaninfo_command=(timeout 15s vulkaninfo --summary)
        else
            vulkaninfo_command=(vulkaninfo --summary)
        fi
        if "${vulkaninfo_command[@]}" >"${VULKANINFO_LOG}" 2>&1; then
            ok "vulkaninfo completed"
            rm -f "${VULKANINFO_LOG}"
        else
            warn "vulkaninfo failed or timed out; inspect ${VULKANINFO_LOG}"
        fi
    fi
else
    warn "vulkaninfo not installed (package: vulkan-tools)"
fi

echo "[4/4] Configuration"
config="${OMNIGPU_CONFIG:-}"
if [[ -z "${config}" && -n "${XDG_CONFIG_HOME:-}" ]]; then
    config="${XDG_CONFIG_HOME}/omnigpu/omnigpu_guest.json"
fi
if [[ -z "${config}" && -n "${HOME:-}" ]]; then
    config="${HOME}/.config/omnigpu/omnigpu_guest.json"
fi
if [[ -z "${config}" || ! -f "${config}" ]]; then
    [[ -f /etc/omnigpu/omnigpu_guest.json ]] && config=/etc/omnigpu/omnigpu_guest.json
fi
[[ -n "${config}" && -f "${config}" ]] && ok "guest config: ${config}" || warn "guest config not found"

printf "%b\n" "${CYAN}=== Result: ${errors} error(s), ${warnings} warning(s) ===${NC}"
[[ ${errors} -eq 0 ]]
