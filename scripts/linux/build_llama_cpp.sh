#!/usr/bin/env bash
set -Eeuo pipefail
usage(){ cat <<'USAGE'
Usage: omnigpu-build-llama-cpp LLAMA_CPP_SOURCE [BUILD_DIR] [INSTALL_PREFIX]

Builds current llama.cpp with the Vulkan backend, dynamic backend loading, and
only CPU + Vulkan accelerator backends. BUILD_DIR defaults to
LLAMA_CPP_SOURCE/build-omnigpu-vulkan. INSTALL_PREFIX must be absolute.
USAGE
}
[[ ${1:-} != -h && ${1:-} != --help ]] || { usage; exit 0; }
(($# >= 1 && $# <= 3)) || { usage >&2; exit 2; }
[[ -d "$1" ]] || { echo "llama.cpp source directory not found: $1" >&2; exit 2; }
src=$(cd -- "$1" && pwd -P)
[[ -f "$src/CMakeLists.txt" && -d "$src/ggml" && -d "$src/tools" ]] || {
  echo "directory does not look like a current llama.cpp source checkout: $src" >&2; exit 2;
}

build_input=${2:-"$src/build-omnigpu-vulkan"}
mkdir -p -- "$build_input"
build=$(cd -- "$build_input" && pwd -P)
[[ "$build" != "$src" ]] || { echo 'refusing an in-source build' >&2; exit 2; }
prefix=${3:-}
if [[ -n "$prefix" && "$prefix" != /* ]]; then
  echo 'INSTALL_PREFIX must be an absolute path' >&2; exit 2
fi

command -v cmake >/dev/null 2>&1 || { echo 'cmake is required' >&2; exit 2; }
command -v glslc >/dev/null 2>&1 || { echo 'glslc is required (Ubuntu: sudo apt install glslc)' >&2; exit 2; }

if [[ -f "$build/CMakeCache.txt" ]]; then
  cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$build/CMakeCache.txt" | tail -n 1)
  [[ -z "$cached_source" || "$cached_source" == "$src" ]] || {
    echo "build directory belongs to another source tree: $cached_source" >&2; exit 2;
  }
fi

parallel=${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
[[ "$parallel" =~ ^[1-9][0-9]*$ ]] || { echo 'CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer' >&2; exit 2; }
(( parallel <= 1024 )) || { echo 'CMAKE_BUILD_PARALLEL_LEVEL must not exceed 1024' >&2; exit 2; }

cmake_args=(
  -S "$src" -B "$build"
  -DBUILD_SHARED_LIBS=ON
  -DGGML_VULKAN=ON
  -DGGML_BACKEND_DL=ON
  -DGGML_NATIVE=OFF
  -DGGML_CUDA=OFF
  -DGGML_HIP=OFF
  -DGGML_SYCL=OFF
  -DGGML_OPENCL=OFF
  -DLLAMA_BUILD_TESTS=OFF
  -DLLAMA_BUILD_TOOLS=ON
  -DLLAMA_BUILD_EXAMPLES=ON
  -DLLAMA_BUILD_SERVER=ON
  -DCMAKE_BUILD_TYPE=Release
)
if command -v ninja >/dev/null 2>&1 && [[ ! -f "$build/CMakeCache.txt" ]]; then
  cmake_args+=(-G Ninja)
fi
[[ -z "$prefix" ]] || cmake_args+=("-DCMAKE_INSTALL_PREFIX=$prefix")
cmake "${cmake_args[@]}"

for expected in \
  'BUILD_SHARED_LIBS:BOOL=ON' \
  'GGML_VULKAN:BOOL=ON' \
  'GGML_BACKEND_DL:BOOL=ON' \
  'GGML_NATIVE:BOOL=OFF' \
  'LLAMA_BUILD_SERVER:BOOL=ON'; do
  grep -Fxq -- "$expected" "$build/CMakeCache.txt" || {
    echo "configured llama.cpp cache is missing: $expected" >&2; exit 1;
  }
done

cmake --build "$build" --config Release --parallel "$parallel" \
  --target llama-cli llama-server llama-bench

for tool in llama-cli llama-server llama-bench; do
  [[ -x "$build/bin/$tool" ]] || { echo "expected build output missing: $build/bin/$tool" >&2; exit 1; }
done
compgen -G "$build/bin/libggml-vulkan.so*" >/dev/null || {
  echo "Vulkan backend library missing from $build/bin" >&2; exit 1;
}

if [[ -n "$prefix" ]]; then
  # CMake's install target depends on the complete installable target set;
  # unlike a bare `cmake --install`, this cannot fail because an installable
  # llama.cpp tool was never built.
  cmake --build "$build" --config Release --parallel "$parallel" --target install
  for tool in llama-cli llama-server llama-bench; do
    [[ -x "$prefix/bin/$tool" ]] || { echo "expected installed tool missing: $prefix/bin/$tool" >&2; exit 1; }
  done
  find "$prefix" -type f -name 'libggml-vulkan.so*' -print -quit | grep -q . || {
    echo "installed Vulkan backend library not found under $prefix" >&2; exit 1;
  }
fi
cat <<MSG
llama.cpp Vulkan build completed.
Probe through OmniGPU with:
  omnigpu-llama --icd /usr/share/vulkan/icd.d/omnigpu_guest.json -- $build/bin/llama-cli --list-devices
MSG
