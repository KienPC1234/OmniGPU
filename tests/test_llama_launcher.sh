#!/usr/bin/env bash
set -Eeuo pipefail
launcher=${1:?launcher path required}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
printf '{"file_format_version":"1.0.1","ICD":{"library_path":"/tmp/libomnigpu_guest.so","api_version":"1.3.0"}}\n' > "$tmp/icd.json"

cat > "$tmp/fake-llama" <<'SH'
#!/usr/bin/env bash
if [[ ${1:-} == --list-devices && ! -e ${FAKE_LIST_EXEC_MARKER:-/nonexistent} ]]; then
  [[ ${VK_DRIVER_FILES:-} == */icd.json ]] || exit 41
  [[ ${VK_ICD_FILENAMES:-} == */icd.json ]] || exit 42
  [[ -z ${VK_ADD_DRIVER_FILES:-} ]] || exit 43
  [[ ${VK_LOADER_LAYERS_DISABLE:-} == '*' ]] || exit 44
  [[ -z ${LLAMA_ARG_DEVICE:-} ]] || exit 45
  [[ -z ${LLAMA_ARG_N_GPU_LAYERS:-} ]] || exit 46
  [[ -z ${GGML_VK_VISIBLE_DEVICES:-} ]] || exit 47
  echo 'ggml_vulkan: Found 2 Vulkan devices:'
  echo 'Some unrelated Vulkan backend log text'
  echo 'Available devices:'
  echo '  Vulkan0: OmniGPU Remote Vulkan Device (8192 MiB, 7000 MiB free)'
  echo '  Vulkan10: Other Device (4096 MiB, 3000 MiB free)'
  exit 0
fi
printf '%s\n' "$@" > "${FAKE_ARGS_OUT:?}"
printf '%s\n' "$VK_DRIVER_FILES" > "${FAKE_ENV_OUT:?}"
SH
chmod +x "$tmp/fake-llama"
export FAKE_ARGS_OUT="$tmp/args" FAKE_ENV_OUT="$tmp/env" FAKE_LIST_EXEC_MARKER="$tmp/list-exec"
export VK_ADD_DRIVER_FILES=/evil/icd.json VK_LAYER_PATH=/evil/layers
export LLAMA_ARG_DEVICE=none LLAMA_ARG_N_GPU_LAYERS=0 GGML_VK_VISIBLE_DEVICES=99
export OMNIGPU_LLAMA_ALLOW_ELEVATED=1

"$launcher" --icd "$tmp/icd.json" --require-device OmniGPU -- "$tmp/fake-llama" -m model.gguf
[[ $(cat "$tmp/env") == "$tmp/icd.json" ]]
grep -Fx -- '--device' "$tmp/args" >/dev/null
grep -Fx -- 'Vulkan0' "$tmp/args" >/dev/null
grep -Fx -- '--n-gpu-layers' "$tmp/args" >/dev/null
grep -Fx -- '999' "$tmp/args" >/dev/null
if grep -Fx -- 'all' "$tmp/args" >/dev/null; then
  echo 'launcher injected llama-bench-incompatible offload value all' >&2; exit 1
fi

# Explicit offload policy must remain GPU-positive and unambiguous.
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" --n-gpu-layers 0 >/dev/null 2>&1; then
  echo 'launcher accepted zero GPU layers' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" -ngl -1 >/dev/null 2>&1; then
  echo 'launcher accepted a negative GPU-layer count' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" --n-gpu-layers >/dev/null 2>&1; then
  echo 'launcher accepted a missing GPU-layer count' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama"     -ngl 7 --n-gpu-layers 8 >/dev/null 2>&1; then
  echo 'launcher accepted conflicting GPU-layer arguments' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" --n-gpu-layers=1000001 >/dev/null 2>&1; then
  echo 'launcher accepted an excessive GPU-layer count' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --no-auto-offload -- "$tmp/fake-llama" -m model.gguf >/dev/null 2>&1; then
  echo 'launcher allowed disabled automatic offload without an explicit positive count' >&2; exit 1
fi
"$launcher" --icd "$tmp/icd.json" --no-auto-offload --   "$tmp/fake-llama" --n-gpu-layers=7 -m model.gguf
[[ $(grep -Fxc -- '--n-gpu-layers=7' "$tmp/args") == 1 ]]
if grep -Fx -- '999' "$tmp/args" >/dev/null; then
  echo 'launcher injected offload beside an explicit layer count' >&2; exit 1
fi

# Exact device validation must not confuse Vulkan1 with Vulkan10.
if "$launcher" --icd "$tmp/icd.json" --device Vulkan1 -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted a prefix-only device match' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --device Vulkan10 --require-device OmniGPU -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'required-device marker was matched against a different device' >&2; exit 1
fi
"$launcher" --icd "$tmp/icd.json" --device Vulkan10 --require-device 'Other Device' -- "$tmp/fake-llama" -ngl 7 -p "two words"
[[ $(grep -Fxc -- '--device' "$tmp/args") == 1 ]]
grep -Fx -- 'Vulkan10' "$tmp/args" >/dev/null
[[ $(grep -Fxc -- '-ngl' "$tmp/args") == 1 ]]
grep -Fx -- 'two words' "$tmp/args" >/dev/null

# llama.cpp's own device arguments must be parsed and validated. They must not
# bypass the selected-device marker check or cause a second injected device.
if "$launcher" --icd "$tmp/icd.json" --require-device OmniGPU --     "$tmp/fake-llama" --device Vulkan10 -m model.gguf >/dev/null 2>&1; then
  echo 'llama.cpp --device bypassed selected-device validation' >&2; exit 1
fi
"$launcher" --icd "$tmp/icd.json" --require-device 'Other Device' --   "$tmp/fake-llama" --device=Vulkan10 -m model.gguf
[[ $(grep -Fxc -- '--device=Vulkan10' "$tmp/args") == 1 ]]
[[ $(grep -Fxc -- '--device' "$tmp/args") == 0 ]]
if grep -Fx -- 'Vulkan0' "$tmp/args" >/dev/null; then
  echo 'launcher injected a different device beside user selection' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" --device none >/dev/null 2>&1; then
  echo 'launcher accepted llama.cpp CPU device none' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --device Vulkan0 --     "$tmp/fake-llama" --device Vulkan10 >/dev/null 2>&1; then
  echo 'launcher accepted conflicting launcher and llama.cpp devices' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" --device >/dev/null 2>&1; then
  echo 'launcher accepted a missing llama.cpp device value' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama"     --device Vulkan0 --device Vulkan10 >/dev/null 2>&1; then
  echo 'launcher accepted conflicting llama.cpp device arguments' >&2; exit 1
fi
"$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama"   --device=Vulkan0,Vulkan10 -m model.gguf
[[ $(grep -Fxc -- '--device=Vulkan0,Vulkan10' "$tmp/args") == 1 ]]
[[ $(grep -Fxc -- '--device' "$tmp/args") == 0 ]]
if "$launcher" --icd "$tmp/icd.json" --require-device OmniGPU --     "$tmp/fake-llama" --device=Vulkan0,Vulkan10 >/dev/null 2>&1; then
  echo 'multi-device selection validated only one selected device' >&2; exit 1
fi

# list-only mode is still validated and must not execute with injected args.
echo sentinel > "$tmp/args"
list_output=$("$launcher" --icd "$tmp/icd.json" --require-device OmniGPU --   "$tmp/fake-llama" --list-devices)
grep -Fq 'Vulkan0: OmniGPU Remote Vulkan Device' <<< "$list_output"
[[ $(cat "$tmp/args") == sentinel ]]
if "$launcher" --icd "$tmp/icd.json" --require-device Missing --    "$tmp/fake-llama" --list-devices >/dev/null 2>&1; then
  echo 'list-only mode bypassed required-device validation' >&2; exit 1
fi

# Probe bypass requires an explicit device or disabled auto-selection.
"$launcher" --icd "$tmp/icd.json" --skip-probe --device Vulkan0 -- "$tmp/fake-llama" -m model.gguf
"$launcher" --icd "$tmp/icd.json" --skip-probe --no-auto-device --   "$tmp/fake-llama" --device Vulkan0 -ngl 1 -m model.gguf
if "$launcher" --icd "$tmp/icd.json" --skip-probe --no-auto-device --     "$tmp/fake-llama" -ngl 1 >/dev/null 2>&1; then
  echo 'launcher allowed probe bypass without an explicit Vulkan device' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --skip-probe -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher allowed probe bypass without a device decision' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --device BadDevice -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted an invalid device token' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --require-device Missing -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted the wrong required-device marker' >&2; exit 1
fi

cat > "$tmp/duplicate-devices" <<'SH'
#!/usr/bin/env bash
if [[ ${1:-} == --list-devices ]]; then
  echo 'Vulkan0: OmniGPU Remote Vulkan Device'
  echo 'Vulkan0: Duplicate Device'
  exit 0
fi
exit 0
SH
chmod +x "$tmp/duplicate-devices"
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/duplicate-devices" >/dev/null 2>&1; then
  echo 'launcher accepted duplicate Vulkan device identifiers' >&2; exit 1
fi

cat > "$tmp/cpu-only" <<'SH'
#!/usr/bin/env bash
[[ ${1:-} == --list-devices ]] && { echo 'CPU: Generic CPU'; exit 0; }
exit 0
SH
chmod +x "$tmp/cpu-only"
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/cpu-only" >/dev/null 2>&1; then
  echo 'launcher accepted CPU-only device list' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/cpu-only" --list-devices >/dev/null 2>&1; then
  echo 'list-only mode accepted a CPU-only device list' >&2; exit 1
fi

cat > "$tmp/hanging" <<'SH'
#!/usr/bin/env bash
if [[ ${1:-} == --list-devices ]]; then
  (sleep 3; touch "${CHILD_SURVIVED:?}") &
  wait
fi
SH
chmod +x "$tmp/hanging"
export CHILD_SURVIVED="$tmp/child-survived"
start=$SECONDS
if "$launcher" --icd "$tmp/icd.json" --probe-timeout 1 -- "$tmp/hanging" >/dev/null 2>&1; then
  echo 'launcher accepted a timed-out probe' >&2; exit 1
fi
(( SECONDS - start < 6 )) || { echo 'probe timeout was not bounded' >&2; exit 1; }
sleep 3
[[ ! -e "$CHILD_SURVIVED" ]] || { echo 'probe timeout left a descendant process running' >&2; exit 1; }

cat > "$tmp/spew" <<'SH'
#!/usr/bin/env bash
if [[ ${1:-} == --list-devices ]]; then
  head -c 5000000 /dev/zero | tr '\0' X
fi
SH
chmod +x "$tmp/spew"
if "$launcher" --icd "$tmp/icd.json" -- "$tmp/spew" >/dev/null 2>&1; then
  echo 'launcher accepted excessive probe output' >&2; exit 1
fi

if "$launcher" --icd "$tmp/missing.json" -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted a missing ICD manifest' >&2; exit 1
fi
printf '{"ICD":{"library_path":"/tmp/libother.so"}}\n' > "$tmp/wrong.json"
if "$launcher" --icd "$tmp/wrong.json" -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted a non-OmniGPU ICD manifest' >&2; exit 1
fi
printf '{"note":"omnigpu_guest","ICD":{"library_path":"/tmp/libother.so"}}\n' > "$tmp/deceptive.json"
if "$launcher" --icd "$tmp/deceptive.json" -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted an OmniGPU marker outside ICD library_path' >&2; exit 1
fi
ln -s "$tmp/icd.json" "$tmp/icd-link.json"
if "$launcher" --icd "$tmp/icd-link.json" -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted a symlink ICD manifest' >&2; exit 1
fi
if "$launcher" --icd "$tmp/icd.json" --probe-timeout 0 -- "$tmp/fake-llama" >/dev/null 2>&1; then
  echo 'launcher accepted an invalid timeout' >&2; exit 1
fi
chmod u+s "$tmp/fake-llama" 2>/dev/null || true
if [[ -u "$tmp/fake-llama" ]]; then
  if "$launcher" --icd "$tmp/icd.json" -- "$tmp/fake-llama" >/dev/null 2>&1; then
    echo 'launcher accepted a setuid target executable' >&2; exit 1
  fi
  chmod u-s "$tmp/fake-llama"
else
  # Git Bash on NTFS cannot represent Unix setuid bits. The runtime guard is
  # covered on filesystems that support the bit; do not fail Windows packaging.
  printf '%s\n' 'setuid test skipped: filesystem does not support setuid bits' >&2
fi

# Diagnostic output belongs on stderr so benchmark JSON/stdout remains clean.
touch "$FAKE_LIST_EXEC_MARKER"
stdout=$tmp/stdout; stderr=$tmp/stderr
"$launcher" --icd "$tmp/icd.json" --skip-probe --device Vulkan0 --print-env -- \
  "$tmp/fake-llama" --list-devices >"$stdout" 2>"$stderr"
[[ ! -s "$stdout" ]]
grep -Fq 'VK_DRIVER_FILES=' "$stderr"

echo 'llama launcher tests passed'
