#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'USAGE'
Usage: omnigpu-llama [options] -- <llama-cli|llama-server|llama-bench> [arguments...]

Options:
  --icd PATH              OmniGPU Vulkan ICD manifest.
  --device VulkanN        Explicit single llama.cpp Vulkan device.
  --require-device TEXT   Require TEXT in a reported Vulkan device line.
  --probe-timeout SEC     Device-probe deadline in seconds (default: 15).
  --skip-probe            Skip --list-devices validation. Non-list execution
                          still requires an explicit VulkanN device.
  --no-auto-device        Do not inject --device VulkanN after a successful probe.
  --no-auto-offload       Do not inject --n-gpu-layers 999; requires an explicit
                          positive llama.cpp GPU-layer argument.
  --print-env             Print selected ICD/device to stderr before execution.
  -h, --help              Show help.

The launcher restricts Vulkan driver discovery to OmniGPU's ICD, disables
Vulkan layers, verifies a Vulkan device, selects it explicitly, and requests
full model-layer offload. It exits instead of silently accepting CPU-only
execution. Do not run it through sudo or a setuid/capability wrapper: Vulkan
loaders may ignore driver-selection environment variables in elevated mode.
USAGE
}

die() { printf 'omnigpu-llama: %s\n' "$*" >&2; exit 2; }
warn() { printf 'omnigpu-llama: warning: %s\n' "$*" >&2; }

is_positive_integer() { [[ $1 =~ ^[1-9][0-9]*$ ]]; }

icd="${OMNIGPU_ICD_MANIFEST:-}"
explicit_device="${OMNIGPU_LLAMA_DEVICE:-}"
require_device="${OMNIGPU_LLAMA_REQUIRE_DEVICE:-}"
probe_timeout="${OMNIGPU_LLAMA_PROBE_TIMEOUT:-15}"
skip_probe=0
auto_device=1
auto_offload=1
print_env=0
while (($#)); do
  case "$1" in
    --icd) (($# >= 2)) || die '--icd requires a path'; icd=$2; shift 2 ;;
    --device) (($# >= 2)) || die '--device requires a name'; explicit_device=$2; shift 2 ;;
    --require-device) (($# >= 2)) || die '--require-device requires text'; require_device=$2; shift 2 ;;
    --probe-timeout) (($# >= 2)) || die '--probe-timeout requires seconds'; probe_timeout=$2; shift 2 ;;
    --skip-probe) skip_probe=1; shift ;;
    --no-auto-device) auto_device=0; shift ;;
    --no-auto-offload) auto_offload=0; shift ;;
    --print-env) print_env=1; shift ;;
    -h|--help) usage; exit 0 ;;
    --) shift; break ;;
    *) die "unknown launcher option: $1 (use -- before llama.cpp arguments)" ;;
  esac
done
(($#)) || die 'missing llama.cpp executable after --'
is_positive_integer "$probe_timeout" || die '--probe-timeout must be a positive integer'
(( probe_timeout <= 300 )) || die '--probe-timeout must not exceed 300 seconds'
[[ -z "$explicit_device" || "$explicit_device" =~ ^Vulkan[0-9]+$ ]] || \
  die '--device must be a single device token such as Vulkan0'

# Vulkan's loader may ignore driver-selection variables for elevated processes.
# Refuse by default because that could silently select a local system GPU.
ruid=$(id -ru 2>/dev/null || id -u)
rgid=$(id -rg 2>/dev/null || id -g)
euid=${EUID:-$(id -u)}
egid=$(id -g)
if [[ "$euid" != "$ruid" || "$egid" != "$rgid" || "$euid" == 0 ]]; then
  if [[ ${OMNIGPU_LLAMA_ALLOW_ELEVATED:-0} != 1 ]]; then
    die 'refusing elevated execution; run as a normal user (or set OMNIGPU_LLAMA_ALLOW_ELEVATED=1 only in a verified container)'
  fi
  warn 'elevated execution override enabled; verify that the Vulkan loader honors VK_DRIVER_FILES'
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ -z "$icd" ]]; then
  candidates=()
  if [[ -n ${XDG_DATA_HOME:-} ]]; then
    candidates+=("$XDG_DATA_HOME/vulkan/icd.d/omnigpu_guest.json")
  elif [[ -n ${HOME:-} ]]; then
    candidates+=("$HOME/.local/share/vulkan/icd.d/omnigpu_guest.json")
  fi
  # Installed command in <prefix>/bin.
  candidates+=("$script_dir/../share/vulkan/icd.d/omnigpu_guest.json")
  # Backward-compatible libexec layout: <prefix>/libexec/omnigpu.
  candidates+=("$script_dir/../../share/vulkan/icd.d/omnigpu_guest.json")
  candidates+=(
    /etc/vulkan/icd.d/omnigpu_guest.json
    /usr/local/share/vulkan/icd.d/omnigpu_guest.json
    /usr/share/vulkan/icd.d/omnigpu_guest.json
  )
  for candidate in "${candidates[@]}"; do
    if [[ -f "$candidate" && ! -L "$candidate" ]]; then icd=$candidate; break; fi
  done
fi
[[ -n "$icd" ]] || die 'OmniGPU ICD manifest not found; pass --icd PATH'
[[ -f "$icd" && ! -L "$icd" ]] || die "ICD manifest is not a regular file: $icd"
manifest_size=$(wc -c < "$icd")
(( manifest_size > 0 && manifest_size <= 1048576 )) || die 'ICD manifest size is invalid'
grep -aEqi -- '"library_path"[[:space:]]*:[[:space:]]*"[^"]*omnigpu_guest[^"]*"' "$icd" || \
  die 'ICD manifest library_path does not reference the OmniGPU guest driver'
icd=$(cd -- "$(dirname -- "$icd")" && printf '%s/%s\n' "$PWD" "$(basename -- "$icd")")

binary=$1; requested_binary=$1; shift
if [[ "$binary" == */* ]]; then
  [[ -x "$binary" && ! -d "$binary" ]] || die "not an executable file: $binary"
  binary=$(cd -- "$(dirname -- "$binary")" && printf '%s/%s\n' "$PWD" "$(basename -- "$binary")")
else
  binary=$(type -P -- "$binary" || true)
  [[ -n "$binary" ]] || die "external executable not found: $requested_binary"
fi
[[ ! -u "$binary" && ! -g "$binary" ]] || die "refusing setuid/setgid executable: $binary"
if command -v getcap >/dev/null 2>&1; then
  capabilities=$(getcap -- "$binary" 2>/dev/null || true)
  [[ -z "$capabilities" ]] || die "refusing executable with file capabilities: $binary"
fi

# Restrict driver discovery to OmniGPU and neutralize stale selectors/layers.
unset VK_ADD_DRIVER_FILES VK_LOADER_DRIVERS_SELECT VK_LOADER_DRIVERS_DISABLE
unset VK_LAYER_PATH VK_ADD_LAYER_PATH VK_IMPLICIT_LAYER_PATH VK_ADD_IMPLICIT_LAYER_PATH
unset VK_INSTANCE_LAYERS VK_LOADER_LAYERS_ENABLE
export VK_LOADER_LAYERS_DISABLE='*'
export VK_DRIVER_FILES="$icd"
export VK_ICD_FILENAMES="$icd"
# Prevent stale llama.cpp/Vulkan environment from defeating explicit CLI policy.
unset LLAMA_ARG_DEVICE LLAMA_ARG_N_GPU_LAYERS GGML_VK_VISIBLE_DEVICES

args=("$@")
has_device=0; has_offload=0; list_only=0
user_device_spec=''
user_offload_spec=''
for ((i=0; i<${#args[@]}; ++i)); do
  case "${args[i]}" in
    --list-devices) list_only=1 ;;
    -dev|--device)
      (( i + 1 < ${#args[@]} )) || die "${args[i]} requires a device value"
      candidate=${args[i+1]}
      [[ -z "$user_device_spec" || "$user_device_spec" == "$candidate" ]] || die 'multiple conflicting llama.cpp device arguments'
      user_device_spec=$candidate
      has_device=1
      ((i+=1))
      ;;
    -dev=*|--device=*)
      candidate=${args[i]#*=}
      [[ -n "$candidate" ]] || die "${args[i]%%=*} requires a device value"
      [[ -z "$user_device_spec" || "$user_device_spec" == "$candidate" ]] || die 'multiple conflicting llama.cpp device arguments'
      user_device_spec=$candidate
      has_device=1
      ;;
    -ngl|--gpu-layers|--n-gpu-layers)
      (( i + 1 < ${#args[@]} )) || die "${args[i]} requires a layer count"
      candidate=${args[i+1]}
      [[ -z "$user_offload_spec" || "$user_offload_spec" == "$candidate" ]] || die 'multiple conflicting llama.cpp GPU-layer arguments'
      user_offload_spec=$candidate
      has_offload=1
      ((i+=1))
      ;;
    -ngl=*|--gpu-layers=*|--n-gpu-layers=*)
      candidate=${args[i]#*=}
      [[ -n "$candidate" ]] || die "${args[i]%%=*} requires a layer count"
      [[ -z "$user_offload_spec" || "$user_offload_spec" == "$candidate" ]] || die 'multiple conflicting llama.cpp GPU-layer arguments'
      user_offload_spec=$candidate
      has_offload=1
      ;;
  esac
done
[[ -z "$explicit_device" || -z "$user_device_spec" ]] || die 'launcher --device conflicts with llama.cpp --device/-dev'
if [[ -n "$user_offload_spec" ]]; then
  [[ "$user_offload_spec" =~ ^[1-9][0-9]*$ ]] || die 'llama.cpp GPU-layer count must be a positive integer'
  (( user_offload_spec <= 1000000 )) || die 'llama.cpp GPU-layer count must not exceed 1000000'
fi

requested_device_spec=${explicit_device:-$user_device_spec}
requested_device_ids=()
if [[ -n "$requested_device_spec" ]]; then
  normalized=${requested_device_spec//\//,}
  IFS=',' read -r -a requested_device_ids <<< "$normalized"
  ((${#requested_device_ids[@]} > 0)) || die 'empty device specification'
  for requested_id in "${requested_device_ids[@]}"; do
    [[ "$requested_id" =~ ^Vulkan[0-9]+$ ]] || die "device specification must contain only VulkanN tokens: $requested_device_spec"
  done
fi

device="$requested_device_spec"
listing=''
if (( skip_probe && ! list_only )) && [[ -z "$requested_device_spec" ]]; then
  die '--skip-probe requires an explicit launcher or llama.cpp VulkanN device'
fi
if (( ! list_only && ! auto_offload && ! has_offload )); then
  die '--no-auto-offload requires an explicit positive llama.cpp GPU-layer count'
fi
if (( ! skip_probe )); then
  command -v timeout >/dev/null 2>&1 || die 'coreutils timeout is required for bounded device probing'
  probe_file=$(mktemp "${TMPDIR:-/tmp}/omnigpu-llama-probe.XXXXXX")
  cleanup_probe() { rm -f -- "$probe_file"; }
  trap cleanup_probe EXIT HUP INT TERM
  set +e
  timeout --signal=TERM --kill-after=2 "${probe_timeout}s" \
    "$binary" --list-devices >"$probe_file" 2>&1
  probe_rc=$?
  set -e
  probe_size=$(wc -c < "$probe_file")
  (( probe_size <= 4194304 )) || { cleanup_probe; trap - EXIT HUP INT TERM; die 'device probe output exceeded 4 MiB'; }
  listing=$(cat -- "$probe_file")
  cleanup_probe
  trap - EXIT HUP INT TERM
  if (( probe_rc == 124 || probe_rc == 137 )); then
    printf '%s\n' "$listing" >&2
    die "device probe timed out after ${probe_timeout}s"
  fi
  (( probe_rc == 0 )) || { printf '%s\n' "$listing" >&2; die "device probe failed (exit $probe_rc)"; }

  device_ids=()
  device_lines=()
  declare -A seen_device_ids=()
  while IFS= read -r line; do
    if [[ $line =~ ^[[:space:]]*(Vulkan[0-9]+):[[:space:]]*(.*)$ ]]; then
      parsed_id=${BASH_REMATCH[1]}
      [[ -z ${seen_device_ids[$parsed_id]+x} ]] || {
        printf '%s\n' "$listing" >&2
        die "llama.cpp reported duplicate Vulkan device id: $parsed_id"
      }
      seen_device_ids[$parsed_id]=1
      device_ids+=("$parsed_id")
      device_lines+=("$parsed_id: ${BASH_REMATCH[2]}")
    fi
  done <<< "$listing"
  ((${#device_ids[@]} > 0)) || { printf '%s\n' "$listing" >&2; die 'llama.cpp reported no Vulkan device through the OmniGPU ICD'; }

  if ((${#requested_device_ids[@]} == 0)); then
    requested_device_ids=("${device_ids[0]}")
    device=${device_ids[0]}
  else
    for requested_id in "${requested_device_ids[@]}"; do
      matched=0
      for id in "${device_ids[@]}"; do [[ "$id" == "$requested_id" ]] && matched=1; done
      (( matched )) || { printf '%s\n' "$listing" >&2; die "requested device not found: $requested_id"; }
    done
  fi

  if [[ -n "$require_device" ]]; then
    for requested_id in "${requested_device_ids[@]}"; do
      selected_line=''
      for line in "${device_lines[@]}"; do
        [[ "$line" == "$requested_id:"* ]] && selected_line=$line
      done
      if [[ -z "$selected_line" ]] || ! grep -Fqi -- "$require_device" <<< "$selected_line"; then
        printf '%s\n' "$listing" >&2
        die "required device text not found in selected device $requested_id: $require_device"
      fi
    done
  fi

  if (( list_only )); then
    (( print_env )) && {
      printf 'VK_DRIVER_FILES=%s\n' "$VK_DRIVER_FILES" >&2
      printf 'VK_ICD_FILENAMES=%s\n' "$VK_ICD_FILENAMES" >&2
      printf 'LLAMA_DEVICE=%s\n' "$device" >&2
    }
    printf '%s\n' "$listing"
    exit 0
  fi
fi

if (( ! list_only && auto_device && ! has_device )); then
  [[ -n "$device" ]] || die 'cannot auto-select a Vulkan device when --skip-probe is used; pass launcher --device or --no-auto-device'
  args+=(--device "$device")
fi
if (( ! list_only && auto_offload && ! has_offload )); then
  # Numeric 999 is accepted by llama-cli, llama-server, and llama-bench;
  # llama-bench does not accept the CLI/server-only value "all".
  args+=(--n-gpu-layers 999)
fi
if (( print_env )); then
  printf 'VK_DRIVER_FILES=%s\n' "$VK_DRIVER_FILES" >&2
  printf 'VK_ICD_FILENAMES=%s\n' "$VK_ICD_FILENAMES" >&2
  printf 'LLAMA_DEVICE=%s\n' "${device:-user-specified}" >&2
fi
exec "$binary" "${args[@]}"
