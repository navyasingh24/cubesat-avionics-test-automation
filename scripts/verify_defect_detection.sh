#!/usr/bin/env bash
# Prove the test suite DETECTS a real defect ("seeded defect" / mutation check).
#
# Builds a separate, deliberately broken simulator (CMake option
# CUBESAT_SEEDED_DEFECT=ON makes the low-battery rule trip at <= 6.8 V instead
# of < 6.8 V) into build-defect/, runs the safety-rule tests against it, and
# SUCCEEDS only if those tests FAIL. The normal build in build/ is untouched.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

DEFECT_DIR="${REPO_ROOT}/build-defect"
PY="$(pick_python)"
require_pytest "${PY}"
require_cmd cmake "Install CMake (see README)."

info "Building deliberately defective simulator in ${DEFECT_DIR}"
cmake -S "${REPO_ROOT}" -B "${DEFECT_DIR}" -DCUBESAT_SEEDED_DEFECT=ON -DBUILD_TESTING=OFF >/dev/null
cmake --build "${DEFECT_DIR}" --target cubesat_sim --parallel "$(cpu_count)" >/dev/null

info "Running safety-rule tests against the defective build (failures EXPECTED)"
set +e
CUBESAT_SIM_BIN="${DEFECT_DIR}/cubesat_sim" "${PY}" -m pytest -q -p no:cacheprovider \
    "${REPO_ROOT}/tests/test_safety_rules.py" 2>&1 | tee "${REPORT_DIR:-${REPO_ROOT}/reports}/seeded_defect_run.txt"
status=${PIPESTATUS[0]}
set -e

if [[ ${status} -eq 1 ]] && grep -q "test_battery_boundary\[6.8-NOMINAL\]" "${REPORT_DIR:-${REPO_ROOT}/reports}/seeded_defect_run.txt"; then
    ok "Seeded defect DETECTED by test_battery_boundary[6.8-NOMINAL] (pytest exit ${status})"
else
    die "Seeded defect was NOT detected as expected (pytest exit ${status}) - the tests are too weak"
fi
