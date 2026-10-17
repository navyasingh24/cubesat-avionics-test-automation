#!/usr/bin/env bash
# Run the verification suites against the ALREADY BUILT simulator and write
# JUnit XML + Markdown reports to reports/. Jenkins and GitHub Actions call
# this same script, so local and CI test procedures cannot drift apart.
#
# Usage: scripts/run_tests.sh [all|unit|system|fault|regression|report] [-- extra pytest args]
#   unit        C++ unit tests                        -> reports/unit-tests.xml
#   system      pytest system tests                   -> reports/junit-system.xml
#   fault       fault-injection pytest + scenario run -> reports/junit-fault-injection.xml,
#                                                        reports/fault_injection_report.md
#   regression  golden-output/determinism tests       -> reports/junit-regression.xml
#   report      requirement traceability matrix       -> reports/traceability.md
#   all         everything above (default). Every stage runs even if an
#               earlier one fails; the script exits non-zero if ANY failed.
set -euo pipefail
# shellcheck source=scripts/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

STAGE="${1:-all}"
[[ $# -gt 0 ]] && shift
if [[ "${1:-}" == "--" ]]; then shift; fi
PYTEST_EXTRA=("$@")

PY="$(pick_python)"
mkdir -p "${REPORT_DIR}"
export CUBESAT_SIM_BIN="${SIM_BIN}"

run_unit() {
    require_built_simulator
    local unit_bin="${BUILD_DIR}/cubesat_unit_tests"
    [[ -x "${unit_bin}" ]] || die "C++ unit tests not built at ${unit_bin} (configure with BUILD_TESTING=ON)"
    info "C++ unit tests"
    "${unit_bin}" --junit "${REPORT_DIR}/unit-tests.xml"
}

run_pytest() {  # <junit name> <marker expression>
    require_built_simulator
    require_pytest "${PY}"
    info "pytest: ${1} (-m '${2}')"
    "${PY}" -m pytest -m "${2}" --junitxml="${REPORT_DIR}/junit-${1}.xml" ${PYTEST_EXTRA[@]+"${PYTEST_EXTRA[@]}"}
}

run_system()     { run_pytest system "not fault_injection and not regression"; }
run_regression() { run_pytest regression "regression"; }
# Stage functions are called inside `if !` in "all" mode, where bash
# suspends `set -e`, so multi-command stages must propagate failures
# explicitly instead of returning only the last command's status.
run_fault() {
    local rc=0
    run_pytest fault-injection "fault_injection" || rc=1
    info "Fault-injection scenario report"
    "${PY}" "${REPO_ROOT}/tools/run_scenarios.py" --report-dir "${REPORT_DIR}" || rc=1
    return "${rc}"
}
run_report() {
    info "Requirement traceability report"
    "${PY}" "${REPO_ROOT}/tools/traceability_report.py" --output "${REPORT_DIR}/traceability.md" \
        "${REPORT_DIR}"/junit-*.xml
}

case "${STAGE}" in
    unit) run_unit ;;
    system) run_system ;;
    fault) run_fault ;;
    regression) run_regression ;;
    report) run_report ;;
    all)
        failed=()
        for stage in unit system fault regression report; do
            if ! "run_${stage}"; then
                fail "stage '${stage}' failed"
                failed+=("${stage}")
            fi
        done
        echo
        if [[ ${#failed[@]} -gt 0 ]]; then
            die "Failed stages: ${failed[*]}  (reports in ${REPORT_DIR})"
        fi
        ok "All verification stages passed (reports in ${REPORT_DIR})"
        ;;
    *) die "unknown stage '${STAGE}' (use all|unit|system|fault|regression|report)" ;;
esac
