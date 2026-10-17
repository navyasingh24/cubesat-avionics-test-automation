#!/usr/bin/env bash
# Deterministic demonstration for interviews and screen recordings.
#
#   1. Build the simulator.
#   2. Run a short annotated mission script and print it as a table.
#   3. Run all fault-injection scenarios            (expected: all PASS).
#   4. Run the negative-control scenario            (expected: FAIL detected).
#   5. Run tests against a seeded-defect build      (expected: FAIL detected).
#
# The script exits 0 only if passing checks passed AND deliberately failing
# checks were detected as failures. The normal build is never left broken.
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

PY="$(pick_python)"
CONFIG="${REPO_ROOT}/config/simulation_config.json"
mkdir -p "${REPORT_DIR}"

banner() { printf '\n%s========== %s ==========%s\n' "${C_BOLD}" "$*" "${C_RESET}"; }

banner "1. Build"
"${REPO_ROOT}/scripts/build.sh" >/dev/null
ok "Simulator built: $("${SIM_BIN}" --version)"

banner "2. Demo mission (tests/golden/mission_day.script)"
"${SIM_BIN}" --config "${CONFIG}" --script "${REPO_ROOT}/tests/golden/mission_day.script" \
    --log-file "${REPORT_DIR}/demo_mission.log.jsonl" | tee "${REPORT_DIR}/demo_mission.stdout.jsonl" \
    | "${PY}" "${REPO_ROOT}/tools/format_session.py"
info "Diagnostic log: ${REPORT_DIR}/demo_mission.log.jsonl ($(wc -l < "${REPORT_DIR}/demo_mission.log.jsonl" | tr -d ' ') entries)"
info "Mode changes recorded in the log:"
"${PY}" "${REPO_ROOT}/tools/format_session.py" --mode-changes < "${REPORT_DIR}/demo_mission.log.jsonl"

banner "3. Fault-injection scenarios (expect PASS)"
"${PY}" "${REPO_ROOT}/tools/run_scenarios.py" --report-dir "${REPORT_DIR}"

banner "4. Negative control: deliberately wrong expectation (expect FAIL)"
if "${PY}" "${REPO_ROOT}/tools/run_scenarios.py" --scenario-dir "${REPO_ROOT}/scenarios/negative_control" \
        --report-dir "${REPORT_DIR}" --report-name negative_control_report; then
    die "Negative control unexpectedly PASSED - the scenario runner is not checking results"
fi
ok "Negative control FAILED as intended: the framework detects a wrong expectation"

banner "5. Seeded defect: deliberately broken build (expect FAIL)"
"${REPO_ROOT}/scripts/verify_defect_detection.sh"

banner "Demo complete"
ok "Passing checks passed; deliberately failing checks were detected. Reports in ${REPORT_DIR}"
