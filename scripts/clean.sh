#!/usr/bin/env bash
# Remove generated artifacts from THIS repository only.
#
# Usage: scripts/clean.sh [--all]
#   default : build directories, generated reports, Python caches
#   --all   : additionally remove the project virtualenv (.venv)
#
# Safety: paths are fixed, relative to the repository root resolved from this
# script's own location, and the root is verified before anything is deleted.
set -euo pipefail
# shellcheck source=scripts/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

grep -q "CubeSatAvionicsTestAutomation" "${REPO_ROOT}/CMakeLists.txt" 2>/dev/null \
    || die "Refusing to clean: ${REPO_ROOT} does not look like this project"

remove() {
    if [[ -e "$1" ]]; then
        rm -rf -- "$1"
        echo "removed ${1#"${REPO_ROOT}/"}"
    fi
}

remove "${REPO_ROOT}/build"
remove "${REPO_ROOT}/build-defect"
remove "${REPO_ROOT}/.pytest_cache"
find "${REPO_ROOT}/reports" -mindepth 1 ! -name '.gitkeep' -exec rm -rf -- {} + 2>/dev/null || true
echo "emptied reports/ (kept .gitkeep)"
find "${REPO_ROOT}/simharness" "${REPO_ROOT}/tests" "${REPO_ROOT}/tools" -type d -name '__pycache__' -prune \
    -exec rm -rf -- {} + 2>/dev/null || true

if [[ "${1:-}" == "--all" ]]; then
    remove "${REPO_ROOT}/.venv"
elif [[ $# -gt 0 ]]; then
    die "unknown argument '$1' (usage: scripts/clean.sh [--all])"
fi
ok "Clean complete"
