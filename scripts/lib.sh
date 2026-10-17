#!/usr/bin/env bash
# Shared helpers for the project scripts. Source it; do not execute it.
# Every script runs with `set -euo pipefail`, so any failing command stops
# the script immediately with a non-zero exit code.

# Absolute path of the repository root (the directory above scripts/).
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export REPO_ROOT

BUILD_DIR="${BUILD_DIR:-${REPO_ROOT}/build}"
REPORT_DIR="${REPORT_DIR:-${REPO_ROOT}/reports}"
SIM_BIN="${BUILD_DIR}/cubesat_sim"

if [[ -t 1 ]]; then
    C_BOLD=$'\033[1m'; C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'; C_RESET=$'\033[0m'
else
    C_BOLD=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_RESET=""
fi

info()    { printf '%s==>%s %s\n' "${C_BOLD}" "${C_RESET}" "$*"; }
ok()      { printf '%s[ OK ]%s %s\n' "${C_GREEN}" "${C_RESET}" "$*"; }
warn()    { printf '%s[WARN]%s %s\n' "${C_YELLOW}" "${C_RESET}" "$*" >&2; }
fail()    { printf '%s[FAIL]%s %s\n' "${C_RED}" "${C_RESET}" "$*" >&2; }
die()     { fail "$*"; exit 1; }

# require_cmd <command> <install hint>
require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "'$1' not found. $2"
}

cpu_count() {
    if command -v nproc >/dev/null 2>&1; then nproc
    elif command -v sysctl >/dev/null 2>&1; then sysctl -n hw.ncpu
    else echo 2
    fi
}

# Prefer the project virtualenv, then $PYTHON, then python3.
pick_python() {
    if [[ -n "${PYTHON:-}" ]]; then echo "${PYTHON}"
    elif [[ -x "${REPO_ROOT}/.venv/bin/python" ]]; then echo "${REPO_ROOT}/.venv/bin/python"
    else echo "python3"
    fi
}

require_pytest() {
    local py="$1"
    "${py}" -c 'import pytest' >/dev/null 2>&1 || die "pytest is not available for ${py}. Run: scripts/setup_$( [[ "$(uname -s)" == Darwin ]] && echo macos || echo linux ).sh --create-venv"
}

require_built_simulator() {
    [[ -x "${SIM_BIN}" ]] || die "Simulator not built at ${SIM_BIN}. Run scripts/build.sh first."
}

# version_ge <actual> <minimum>  (dotted numeric versions)
version_ge() {
    [[ "$(printf '%s\n%s\n' "$2" "$1" | sort -t. -k1,1n -k2,2n -k3,3n | head -n1)" == "$2" ]]
}
