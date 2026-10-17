#!/usr/bin/env bash
# Configure and compile the simulator and C++ unit tests with CMake.
#
# Usage: scripts/build.sh [all|configure|compile] [--clean]
#   all        configure + compile (default)
#   configure  only run CMake configure (Jenkins "Configure" stage)
#   compile    only compile an already configured build directory
# Environment:
#   BUILD_DIR      build directory (default: <repo>/build)
#   BUILD_TYPE     CMake build type (default: Debug)
#   CUBESAT_WERROR ON to treat compiler warnings as errors (CI sets ON)
#   CUBESAT_SANITIZERS ON to build with AddressSanitizer/UBSan
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

STEP="all"
for arg in "$@"; do
    case "${arg}" in
        all|configure|compile) STEP="${arg}" ;;
        --clean) info "Removing ${BUILD_DIR}"; rm -rf -- "${BUILD_DIR}" ;;
        *) die "unknown argument '${arg}' (usage: scripts/build.sh [all|configure|compile] [--clean])" ;;
    esac
done

require_cmd cmake "Install CMake >= 3.16 (macOS: 'brew install cmake'; Ubuntu: 'sudo apt-get install cmake')."
if ! command -v c++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    die "No C++ compiler found (macOS: 'xcode-select --install'; Ubuntu: 'sudo apt-get install build-essential')."
fi

if [[ "${STEP}" != "compile" ]]; then
    info "Configuring (${BUILD_TYPE:-Debug}) in ${BUILD_DIR}"
    cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}" \
        -DCUBESAT_WARNINGS_AS_ERRORS="${CUBESAT_WERROR:-OFF}" \
        -DCUBESAT_ENABLE_SANITIZERS="${CUBESAT_SANITIZERS:-OFF}"
fi
[[ "${STEP}" == "configure" ]] && exit 0

[[ -f "${BUILD_DIR}/CMakeCache.txt" ]] || die "${BUILD_DIR} is not configured. Run scripts/build.sh configure first."
info "Compiling with $(cpu_count) parallel jobs"
cmake --build "${BUILD_DIR}" --parallel "$(cpu_count)"

require_built_simulator
ok "Built ${SIM_BIN}"
"${SIM_BIN}" --version
