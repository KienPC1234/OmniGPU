#!/usr/bin/env bash
# OmniGPU Vulkan guest ICD installer for Linux.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Source/package layout: <root>/scripts/install_guest_linux.sh
# CMake-installed layout: <prefix>/libexec/omnigpu/install_guest_linux.sh
if [[ -f "${SCRIPT_DIR}/../CMakeLists.txt" || -d "${SCRIPT_DIR}/../src" ]]; then
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
elif [[ -d "${SCRIPT_DIR}/../../bin" || -d "${SCRIPT_DIR}/../../lib" || -d "${SCRIPT_DIR}/../../share" ]]; then
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
else
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
fi

BUILD_DIR=""
PREFIX="/usr"
UNINSTALL=false
TEMP_DIR=""

cleanup() {
    [[ -z "${TEMP_DIR}" ]] || rm -rf -- "${TEMP_DIR}"
}
trap cleanup EXIT

usage() {
    cat <<USAGE
Usage: sudo $0 [--build-dir DIR] [--prefix PREFIX] [--uninstall]

Installs the Linux Vulkan ICD, a non-secret system-wide guest configuration,
and the loader manifest. The default prefix is /usr. PREFIX must be absolute.
Use a per-user config or OMNIGPU_AUTH_TOKEN for credentials.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) [[ $# -ge 2 ]] || { echo "--build-dir requires a value" >&2; exit 2; }; BUILD_DIR="$2"; shift 2 ;;
        --prefix) [[ $# -ge 2 ]] || { echo "--prefix requires a value" >&2; exit 2; }; PREFIX="$2"; shift 2 ;;
        --uninstall) UNINSTALL=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

validate_install_path() {
    local name="$1" value="$2"
    [[ -z "${value}" || "${value}" == /* ]] || {
        echo "${name} must be an absolute path: ${value}" >&2
        return 1
    }
    [[ "${value}" != *$'\n'* && "${value}" != *$'\r'* &&
       "${value}" != *$'\t'* && "${value}" != *' '* ]] || {
        echo "${name} must not contain whitespace: ${value}" >&2
        return 1
    }
    [[ "/${value#/}/" != *"/../"* ]] || {
        echo "${name} must not contain '..' path components: ${value}" >&2
        return 1
    }
    [[ "${value}" =~ ^/[A-Za-z0-9._+@/:=-]*$ || -z "${value}" ]] || {
        echo "${name} contains characters unsafe for service/manifest paths: ${value}" >&2
        return 1
    }
}

validate_install_path PREFIX "${PREFIX}" || exit 2
DESTDIR="${DESTDIR:-}"
validate_install_path DESTDIR "${DESTDIR}" || exit 2
PREFIX_ROOT="${PREFIX%/}"
[[ -n "${PREFIX_ROOT}" ]] || PREFIX_ROOT=""
if [[ ${EUID} -ne 0 && -z "${DESTDIR}" ]]; then
    echo "This installer must run as root (use sudo), unless DESTDIR is set." >&2
    exit 1
fi

INSTALL_PREFIX="${DESTDIR}${PREFIX_ROOT}"
RUNTIME_LIB_DIR="${PREFIX_ROOT}/lib/omnigpu"
LIB_DIR="${INSTALL_PREFIX}/lib/omnigpu"
ICD_DIR="${INSTALL_PREFIX}/share/vulkan/icd.d"
CONFIG_DIR="${DESTDIR}/etc/omnigpu"
ICD_JSON_DST="${ICD_DIR}/omnigpu_guest.json"
LDCONFIG_FILE="${DESTDIR}/etc/ld.so.conf.d/omnigpu.conf"
RUNTIME_BIN_DIR="${PREFIX_ROOT}/bin"

if [[ "${UNINSTALL}" == true ]]; then
    rm -f -- "${ICD_JSON_DST}" "${LDCONFIG_FILE}"
    rm -f -- "${LIB_DIR}/libomnigpu_guest.so" \
          "${INSTALL_PREFIX}/bin/omnigpu_guest_test"
    rmdir -- "${LIB_DIR}" 2>/dev/null || true
    if [[ -z "${DESTDIR}" ]] && command -v ldconfig >/dev/null 2>&1; then ldconfig; fi
    echo "OmniGPU guest ICD removed. Configuration was retained in ${CONFIG_DIR}."
    exit 0
fi

command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required to validate configuration and generate the ICD manifest." >&2
    exit 1
}

if [[ -z "${BUILD_DIR}" ]]; then
    for candidate in build/linux build/release build/default .; do
        candidate_root="${PROJECT_ROOT}/${candidate}"
        if [[ -f "${candidate_root}/lib/libomnigpu_guest.so" ]] ||
           find "${candidate_root}/lib" -maxdepth 4 -name libomnigpu_guest.so \
                -type f -print -quit 2>/dev/null | grep -q .; then
            BUILD_DIR="$(cd "${candidate_root}" && pwd)"
            break
        fi
    done
elif [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
fi

if [[ -z "${BUILD_DIR}" ]]; then
    echo "No Linux guest build/package was found. Use --build-dir." >&2
    exit 1
fi
GUEST_LIBRARY="${BUILD_DIR}/lib/libomnigpu_guest.so"
if [[ ! -f "${GUEST_LIBRARY}" ]]; then
    GUEST_LIBRARY="$(find "${BUILD_DIR}/lib" -maxdepth 4 \
        -name libomnigpu_guest.so -type f -print -quit 2>/dev/null || true)"
fi
if [[ -z "${GUEST_LIBRARY}" || ! -f "${GUEST_LIBRARY}" ]]; then
    echo "Missing libomnigpu_guest.so; build with 'cmake --preset linux && cmake --build --preset linux'." >&2
    exit 1
fi

BUILD_MANIFEST=""
for candidate in \
    "${BUILD_DIR}/src/guest/vk_icd.json" \
    "${BUILD_DIR}/share/vulkan/icd.d/omnigpu_guest.json"; do
    if [[ -f "${candidate}" ]]; then BUILD_MANIFEST="${candidate}"; break; fi
done
if [[ -z "${BUILD_MANIFEST}" ]]; then
    echo "Missing generated/packaged OmniGPU ICD manifest under ${BUILD_DIR}." >&2
    exit 1
fi

CONFIG_SOURCE=""
for candidate in \
    "${PROJECT_ROOT}/omnigpu_guest.json" \
    "${PROJECT_ROOT}/etc/omnigpu/omnigpu_guest.json" \
    "${BUILD_DIR}/etc/omnigpu/omnigpu_guest.json" \
    "${BUILD_DIR}/omnigpu_guest.json"; do
    if [[ -f "${candidate}" ]]; then CONFIG_SOURCE="${candidate}"; break; fi
done
EXISTING_CONFIG="${CONFIG_DIR}/omnigpu_guest.json"
if [[ -z "${CONFIG_SOURCE}" && ! -f "${EXISTING_CONFIG}" ]]; then
    echo "Missing omnigpu_guest.json in the source/package and destination." >&2
    exit 1
fi

# Validate and generate every derived file before mutating the destination.
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/omnigpu-guest-install.XXXXXX")"
GENERATED_MANIFEST="${TEMP_DIR}/omnigpu_guest.json"
GENERATED_CONFIG="${TEMP_DIR}/omnigpu_guest_config.json"
if [[ -f "${EXISTING_CONFIG}" ]]; then
    CONFIG_TO_VALIDATE="${EXISTING_CONFIG}"
else
    CONFIG_TO_VALIDATE="${CONFIG_SOURCE}"
fi
python3 - "${BUILD_MANIFEST}" "${GENERATED_MANIFEST}" \
    "${RUNTIME_LIB_DIR}/libomnigpu_guest.so" \
    "${CONFIG_TO_VALIDATE}" "${GENERATED_CONFIG}" <<'PY'
import json
import pathlib
import sys

manifest_source = pathlib.Path(sys.argv[1])
manifest_destination = pathlib.Path(sys.argv[2])
library = sys.argv[3]
config_source = pathlib.Path(sys.argv[4])
config_destination = pathlib.Path(sys.argv[5])

manifest = json.loads(manifest_source.read_text(encoding="utf-8"))
if not isinstance(manifest, dict):
    raise SystemExit(f"invalid Vulkan ICD manifest root: {manifest_source}")
icd = manifest.get("ICD")
if not isinstance(icd, dict) or not isinstance(icd.get("api_version"), str):
    raise SystemExit(f"invalid Vulkan ICD manifest: {manifest_source}")
icd["library_path"] = library
manifest_destination.write_text(json.dumps(manifest, indent=4) + "\n", encoding="utf-8")

config = json.loads(config_source.read_text(encoding="utf-8"))
if not isinstance(config, dict):
    raise SystemExit(f"guest configuration must be a JSON object: {config_source}")
host = config.get("host", "127.0.0.1")
port = config.get("port", 9443)
if not isinstance(host, str) or not host.strip():
    raise SystemExit(f"guest configuration has an invalid host: {config_source}")
if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
    raise SystemExit(f"guest configuration has an invalid port: {config_source}")
token = config.get("auth_token", "")
if token is not None and not isinstance(token, str):
    raise SystemExit(f"guest configuration auth_token must be a string: {config_source}")
if token:
    print("WARNING: removed auth_token from world-readable system guest config; "
          "use a per-user config or OMNIGPU_AUTH_TOKEN", file=sys.stderr)
config["auth_token"] = ""
config_destination.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
PY

install -d -m 0755 "${LIB_DIR}" "${ICD_DIR}" "${CONFIG_DIR}"
install -m 0755 "${GUEST_LIBRARY}" "${LIB_DIR}/libomnigpu_guest.so"
install -m 0644 "${GENERATED_MANIFEST}" "${ICD_JSON_DST}"

install -d -m 0755 "$(dirname "${LDCONFIG_FILE}")"
printf '%s\n' "${RUNTIME_LIB_DIR}" > "${LDCONFIG_FILE}"
chmod 0644 "${LDCONFIG_FILE}"
if [[ -z "${DESTDIR}" ]] && command -v ldconfig >/dev/null 2>&1; then ldconfig; fi

# Ordinary applications must be able to read this shared configuration.
# Reinstall the validated/sanitized representation even on upgrades so an old
# world-readable auth token or unsafe mode cannot survive indefinitely.
install -m 0644 "${GENERATED_CONFIG}" "${EXISTING_CONFIG}"

if [[ -x "${BUILD_DIR}/bin/omnigpu_guest_test" ]]; then
    install -d -m 0755 "${INSTALL_PREFIX}/bin"
    install -m 0755 "${BUILD_DIR}/bin/omnigpu_guest_test" "${INSTALL_PREFIX}/bin/omnigpu_guest_test"
fi

if [[ "${PREFIX_ROOT:-/}" != "/usr" && "${PREFIX_ROOT:-/}" != "/usr/local" ]]; then
    echo "WARNING: ${PREFIX_ROOT:-/}/share may not be in XDG_DATA_DIRS; set VK_DRIVER_FILES=${PREFIX_ROOT}/share/vulkan/icd.d/omnigpu_guest.json if discovery fails." >&2
fi

cat <<SUMMARY
OmniGPU guest installed:
  ICD library : ${RUNTIME_LIB_DIR}/libomnigpu_guest.so
  Manifest    : ${ICD_JSON_DST}
  Config      : ${EXISTING_CONFIG}

Validate with:
  VK_LOADER_DEBUG=error,warn vulkaninfo --summary
  ${RUNTIME_BIN_DIR}/omnigpu_guest_test
SUMMARY
