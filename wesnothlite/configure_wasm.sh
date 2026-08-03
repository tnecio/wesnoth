#!/usr/bin/env bash
# configure_wasm.sh — Configure the WesnothLite WASM build.
#
# Prerequisites:
#   - /emsdk activated (run `. /emsdk/emsdk_env.sh` first)
#   - VCPKG_ROOT set to your vcpkg installation (e.g. /usr/local/vcpkg)
#   - vcpkg packages installed for wasm32-emscripten triplet:
#       See wesnothlite/wasm_guide.md §1.2 for the full package list.
#       boost-coroutine is excluded (unsupported on Emscripten).
#
# Usage:
#   cd /wesnoth_wl
#   export VCPKG_ROOT=/usr/local/vcpkg
#   . /emsdk/emsdk_env.sh
#   bash wesnothlite/configure_wasm.sh
#   cmake --build build-wasm --target wesnothlite -j4
#
# Build type defaults to RelWithDebInfo. A Debug build links a ~156 MB
# wesnothlite.wasm, which is slow enough to instantiate that it dominates
# page-load time during frontend iteration. Override with:
#   WL_BUILD_TYPE=Debug bash wesnothlite/configure_wasm.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build-wasm"
VCPKG_ROOT="${VCPKG_ROOT:-/vcpkg}"
EMSDK="${EMSDK:-/emsdk}"
EMSCRIPTEN_ROOT="${EMSCRIPTEN_ROOT:-${EMSDK}/upstream/emscripten}"

echo "=== Configuring WesnothLite WASM build ==="
echo "  Repo:        ${REPO_ROOT}"
echo "  Build dir:   ${BUILD_DIR}"
echo "  vcpkg:       ${VCPKG_ROOT}"
echo "  Emscripten:  ${EMSCRIPTEN_ROOT}"

mkdir -p "${BUILD_DIR}"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake" \
    -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DCMAKE_BUILD_TYPE="${WL_BUILD_TYPE:-RelWithDebInfo}" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
    -DENABLE_GAME=OFF \
    -DENABLE_SERVER=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_TOOLS=OFF \
    -DENABLE_DISPLAY_REVISION=OFF \
    "$@"

echo ""
echo "=== Configuration complete ==="
echo "Build with:"
echo "  cmake --build ${BUILD_DIR} --target wesnothlite -j4"
