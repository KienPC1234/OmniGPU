#!/usr/bin/env bash
# Install the OmniGPU Linux host and optional systemd service.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Works from <root>/scripts/linux and <prefix>/libexec/omnigpu.
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR=""
PREFIX="/usr"
START_SERVICE=false
UNINSTALL=false
TEMP_DIR=""

cleanup() {
    [[ -z "${TEMP_DIR}" ]] || rm -rf -- "${TEMP_DIR}"
}
trap cleanup EXIT

usage() {
    cat <<USAGE
Usage: sudo $0 [--build-dir DIR] [--prefix PREFIX] [--start] [--uninstall]

PREFIX must be absolute. On non-systemd systems the binary/configuration are
installed, but service enable/start is skipped.
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) [[ $# -ge 2 ]] || { echo "--build-dir requires a value" >&2; exit 2; }; BUILD_DIR="$2"; shift 2 ;;
        --prefix) [[ $# -ge 2 ]] || { echo "--prefix requires a value" >&2; exit 2; }; PREFIX="$2"; shift 2 ;;
        --start) START_SERVICE=true; shift ;;
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

SERVICE_NAME="omnigpu-host.service"
INSTALL_PREFIX="${DESTDIR}${PREFIX_ROOT}"
SERVICE_PATH="${DESTDIR}/etc/systemd/system/${SERVICE_NAME}"
CONFIG_DIR="${DESTDIR}/etc/omnigpu"
STATE_DIR="${DESTDIR}/var/lib/omnigpu"
LOG_DIR="${DESTDIR}/var/log/omnigpu"
RUNTIME_BINARY="${PREFIX_ROOT}/bin/omnigpu_host"
RUNTIME_CONFIG="/etc/omnigpu/omnigpu_host.json"
SYSTEMD_AVAILABLE=false
if [[ -z "${DESTDIR}" ]] && command -v systemctl >/dev/null 2>&1 && [[ -d /run/systemd/system ]]; then
    SYSTEMD_AVAILABLE=true
fi

if [[ "${START_SERVICE}" == true && -z "${DESTDIR}" && "${SYSTEMD_AVAILABLE}" != true ]]; then
    echo "ERROR: --start requires an active systemd instance." >&2
    exit 1
fi

if [[ "${UNINSTALL}" == true ]]; then
    if [[ "${SYSTEMD_AVAILABLE}" == true ]]; then
        systemctl disable --now "${SERVICE_NAME}" 2>/dev/null || true
    fi
    rm -f -- "${SERVICE_PATH}" "${INSTALL_PREFIX}/bin/omnigpu_host"
    if [[ "${SYSTEMD_AVAILABLE}" == true ]]; then systemctl daemon-reload; fi
    echo "OmniGPU host removed; configuration in ${CONFIG_DIR} was retained."
    exit 0
fi

command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required to validate the host configuration." >&2
    exit 1
}

if [[ -z "${BUILD_DIR}" ]]; then
    for candidate in build/linux build/release build/default .; do
        if [[ -x "${PROJECT_ROOT}/${candidate}/bin/omnigpu_host" ]]; then
            BUILD_DIR="$(cd "${PROJECT_ROOT}/${candidate}" && pwd)"
            break
        fi
    done
elif [[ "${BUILD_DIR}" != /* ]]; then
    BUILD_DIR="${PROJECT_ROOT}/${BUILD_DIR}"
fi

if [[ -z "${BUILD_DIR}" ]]; then
    echo "No Linux host build/package was found. Use --build-dir." >&2
    exit 1
fi
HOST_BINARY="${BUILD_DIR}/bin/omnigpu_host"
if [[ ! -x "${HOST_BINARY}" ]]; then
    echo "Host binary not found: ${HOST_BINARY}" >&2
    exit 1
fi

CONFIG_SOURCE=""
for candidate in \
    "${PROJECT_ROOT}/omnigpu_host.json" \
    "${PROJECT_ROOT}/etc/omnigpu/omnigpu_host.json" \
    "${BUILD_DIR}/etc/omnigpu/omnigpu_host.json" \
    "${BUILD_DIR}/omnigpu_host.json"; do
    if [[ -f "${candidate}" ]]; then CONFIG_SOURCE="${candidate}"; break; fi
done
EXISTING_CONFIG="${CONFIG_DIR}/omnigpu_host.json"
if [[ -z "${CONFIG_SOURCE}" && ! -f "${EXISTING_CONFIG}" ]]; then
    echo "Missing omnigpu_host.json in the source/package and destination." >&2
    exit 1
fi
if [[ -f "${EXISTING_CONFIG}" ]]; then
    CONFIG_TO_VALIDATE="${EXISTING_CONFIG}"
else
    CONFIG_TO_VALIDATE="${CONFIG_SOURCE}"
fi

SERVICE_TEMPLATE=""
for candidate in \
    "${PROJECT_ROOT}/packaging/linux/omnigpu-host.service.in" \
    "${SCRIPT_DIR}/omnigpu-host.service.in" \
    "${PROJECT_ROOT}/libexec/omnigpu/omnigpu-host.service.in"; do
    if [[ -f "${candidate}" ]]; then SERVICE_TEMPLATE="${candidate}"; break; fi
done

# Validate configuration and generate the unit before changing system paths.
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/omnigpu-host-install.XXXXXX")"
GENERATED_SERVICE="${TEMP_DIR}/${SERVICE_NAME}"
python3 - "${CONFIG_TO_VALIDATE}" <<'PY'
import json
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
config = json.loads(path.read_text(encoding="utf-8"))
if not isinstance(config, dict):
    raise SystemExit(f"host configuration must be a JSON object: {path}")
port = config.get("port", 9443)
if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
    raise SystemExit(f"host configuration has an invalid port: {path}")
for key in ("max_sessions", "max_msg_size_mb"):
    if key in config and (isinstance(config[key], bool) or
                          not isinstance(config[key], int) or config[key] <= 0):
        raise SystemExit(f"host configuration has an invalid {key}: {path}")
if "session_timeout_s" in config and (isinstance(config["session_timeout_s"], bool) or
                                       not isinstance(config["session_timeout_s"], int) or
                                       config["session_timeout_s"] < 0):
    raise SystemExit(f"host configuration has an invalid session_timeout_s: {path}")
if "auth_token" in config and not isinstance(config["auth_token"], str):
    raise SystemExit(f"host configuration auth_token must be a string: {path}")
PY

if [[ -n "${SERVICE_TEMPLATE}" ]]; then
    sed_escape_replacement() {
        printf '%s' "$1" | sed -e 's/[\&|]/\\&/g'
    }
    escaped_binary="$(sed_escape_replacement "${RUNTIME_BINARY}")"
    escaped_config="$(sed_escape_replacement "${RUNTIME_CONFIG}")"
    sed \
        -e "s|@OMNIGPU_HOST_BINARY@|${escaped_binary}|g" \
        -e "s|@OMNIGPU_HOST_CONFIG@|${escaped_config}|g" \
        "${SERVICE_TEMPLATE}" > "${GENERATED_SERVICE}"
else
    cat > "${GENERATED_SERVICE}" <<UNIT
[Unit]
Description=OmniGPU remote Vulkan compute host
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
User=omnigpu
Group=omnigpu
WorkingDirectory=/var/lib/omnigpu
ExecStart=${RUNTIME_BINARY} ${RUNTIME_CONFIG} --no-cli
Restart=on-failure
RestartSec=2s
UMask=0077
TimeoutStopSec=15s
NoNewPrivileges=true
PrivateTmp=true
ProtectHome=true
ProtectSystem=strict
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
ReadWritePaths=/var/lib/omnigpu /var/log/omnigpu /tmp
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
UNIT
fi
if grep -q '@OMNIGPU_' "${GENERATED_SERVICE}"; then
    echo "Unresolved placeholder in generated systemd unit" >&2
    exit 1
fi

if [[ -z "${DESTDIR}" ]]; then
    for tool in useradd usermod groupadd getent; do
        command -v "${tool}" >/dev/null 2>&1 || {
            echo "${tool} is required for a system installation." >&2
            exit 1
        }
    done
    if ! getent group omnigpu >/dev/null; then
        groupadd --system omnigpu
    fi
    if ! id -u omnigpu >/dev/null 2>&1; then
        nologin_shell="$(command -v nologin || true)"
        [[ -n "${nologin_shell}" ]] || nologin_shell="/usr/sbin/nologin"
        useradd --system --gid omnigpu --home-dir "/var/lib/omnigpu" \
            --create-home --shell "${nologin_shell}" omnigpu
    fi
    for group in video render; do
        if getent group "${group}" >/dev/null; then
            usermod -a -G "${group}" omnigpu
        fi
    done
fi

install -d -m 0755 "${INSTALL_PREFIX}/bin" "${CONFIG_DIR}" "$(dirname "${SERVICE_PATH}")"
install -d -m 0750 "${STATE_DIR}" "${LOG_DIR}"
if [[ -z "${DESTDIR}" ]]; then chown omnigpu:omnigpu "${STATE_DIR}" "${LOG_DIR}"; fi
install -m 0755 "${HOST_BINARY}" "${INSTALL_PREFIX}/bin/omnigpu_host"

if [[ ! -f "${EXISTING_CONFIG}" ]]; then
    if [[ -z "${DESTDIR}" ]]; then
        install -m 0640 -o root -g omnigpu "${CONFIG_SOURCE}" "${EXISTING_CONFIG}"
    else
        install -m 0640 "${CONFIG_SOURCE}" "${EXISTING_CONFIG}"
    fi
else
    # Preserve administrator content, but repair permissions on upgrades.
    chmod 0640 "${EXISTING_CONFIG}"
    if [[ -z "${DESTDIR}" ]]; then chown root:omnigpu "${EXISTING_CONFIG}"; fi
fi
install -m 0644 "${GENERATED_SERVICE}" "${SERVICE_PATH}"

if [[ "${SYSTEMD_AVAILABLE}" == true ]]; then
    systemctl daemon-reload
    systemctl enable "${SERVICE_NAME}"
    if [[ "${START_SERVICE}" == true ]]; then
        systemctl restart "${SERVICE_NAME}"
        systemctl --no-pager --full status "${SERVICE_NAME}"
    fi
elif [[ -z "${DESTDIR}" ]]; then
    echo "WARNING: systemd is not active; the unit was installed but not enabled." >&2
elif [[ "${START_SERVICE}" == true ]]; then
    echo "--start is ignored when DESTDIR is set" >&2
fi

echo "OmniGPU host installed. Start manually with: ${RUNTIME_BINARY} ${RUNTIME_CONFIG} --no-cli"
