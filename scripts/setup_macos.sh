#!/usr/bin/env bash
# Check (and optionally prepare) a macOS development environment.
#
# Usage: scripts/setup_macos.sh [--create-venv]
#
# By default this script ONLY CHECKS for the required tools and prints how to
# install anything missing. It never installs system packages, never uses
# sudo and never modifies files outside this repository.
#   --create-venv  create <repo>/.venv and install requirements.txt into it
#                  (project-local; nothing is installed system-wide)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

[[ "$(uname -s)" == "Darwin" ]] || warn "This is not macOS; consider scripts/setup_linux.sh"

missing=0
check() {  # <command> <install hint>
    if command -v "$1" >/dev/null 2>&1; then
        ok "$1: $("$1" --version 2>&1 | head -n1)"
    else
        fail "$1 not found -> $2"
        missing=1
    fi
}

info "macOS $(sw_vers -productVersion 2>/dev/null || echo unknown) on $(uname -m)"
check clang++ "xcode-select --install   (Xcode Command Line Tools)"
check cmake   "brew install cmake   (or download from https://cmake.org/download/)"
check python3 "brew install python   (or https://www.python.org/downloads/macos/)"
check git     "xcode-select --install"

if command -v cmake >/dev/null 2>&1; then
    cmake_version="$(cmake --version | head -n1 | awk '{print $3}')"
    version_ge "${cmake_version}" "3.16.0" || { fail "CMake ${cmake_version} < 3.16"; missing=1; }
    version_ge "${cmake_version}" "3.21.0" || warn "CMake ${cmake_version} < 3.21: C++ unit-test JUnit XML will be skipped"
fi
if command -v python3 >/dev/null 2>&1; then
    python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' \
        || { fail "Python >= 3.9 required"; missing=1; }
fi

if [[ "${1:-}" == "--create-venv" ]]; then
    [[ ${missing} -eq 0 ]] || die "Install the missing tools first."
    info "Creating project virtualenv at ${REPO_ROOT}/.venv"
    python3 -m venv "${REPO_ROOT}/.venv"
    "${REPO_ROOT}/.venv/bin/python" -m pip install --upgrade pip >/dev/null
    "${REPO_ROOT}/.venv/bin/python" -m pip install -r "${REPO_ROOT}/requirements.txt"
    ok "pytest installed in .venv ($("${REPO_ROOT}/.venv/bin/python" -m pytest --version 2>&1))"
elif [[ $# -gt 0 ]]; then
    die "unknown argument '$1' (usage: scripts/setup_macos.sh [--create-venv])"
elif [[ -x "${REPO_ROOT}/.venv/bin/python" ]] && "${REPO_ROOT}/.venv/bin/python" -c 'import pytest' 2>/dev/null; then
    ok "pytest available in .venv"
else
    warn "pytest not set up yet. Run: scripts/setup_macos.sh --create-venv"
fi

[[ ${missing} -eq 0 ]] || die "Some required tools are missing (see above)."
ok "Environment ready. Next: scripts/build.sh && scripts/run_tests.sh"
