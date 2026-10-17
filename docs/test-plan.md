# Test Plan

> Verification plan for an educational SIL simulator. The requirements are
> demonstration requirements written for this project, not derived from any
> real mission.

## 1. Scope

**In scope:** the `cubesat_sim` executable and its library: command
processing, mode state machine, telemetry validation, safety rules, recovery,
configuration handling, CLI contract, diagnostic logging and determinism.

**Out of scope:** real hardware, real-time behaviour, timing on target
processors, radio protocols, power/thermal physics, and qualification against
any spacecraft standard (see [limitations.md](limitations.md)).

## 2. Test levels

| Level | Tooling | What it covers | Location |
|-------|---------|----------------|----------|
| Unit | C++ built-in runner, CTest | JSON parser, config validation, telemetry parser, threshold predicates, hysteresis, transition table, CLI exit codes | `unit_tests/unit_tests.cpp` |
| System (SIL) | pytest + `simharness` driving the real binary | Every requirement in `tests/requirements.json` | `tests/test_*.py` |
| Fault injection | data-driven scenarios | 19 deliberate failure scenarios with preconditions | `scenarios/*.json`, `tests/test_fault_injection.py` |
| Regression | golden files + reruns | Exact responses and logs for reference scripts; byte-identical reruns | `tests/test_regression.py`, `tests/golden/` |
| Framework self-check | negative control, deliberately failing test, seeded defect | The tests and runners actually detect failures | `tests/test_selfcheck.py`, `scenarios/negative_control/`, `scripts/verify_defect_detection.sh` |

## 3. Techniques used

* **Boundary-value analysis.** Every threshold is tested just below, exactly
  at, and just above its limit (e.g. 6.81 / 6.80 / 6.79 V). The same applies
  to plausibility limits, timeouts (10/11 s, 60/61 s) and line length
  (256/257 bytes).
* **Equivalence partitioning.** Representative values for each class of
  invalid input: non-numeric, non-finite, hex, units, empty, out of range,
  duplicate or unknown fields.
* **State-transition testing.** A full command × mode permission matrix
  (11 × 4 = 44 cases), every documented transition, and every illegal exit
  from FAULT.
* **Negative testing.** Unknown commands, malformed bytes, corrupt and
  contradictory configuration, missing files.
* **Property-based random walks.** `test_invariants_hold_for_random_command_sequences`
  runs 8 seeded, reproducible random sequences of 300 commands. After every
  command it checks the invariants (never NOMINAL with active violations,
  payload ON only in NOMINAL, only documented transitions).
* **Robustness.** 1 MB lines, 20 kB of random bytes, 5,000-command streams.
* **Golden-output regression** and **determinism** (byte-identical reruns).
* **Mutation check.** A seeded defect must be detected.

## 4. Traceability

Every pytest test declares the requirements it verifies:

```python
@pytest.mark.req("REQ-SAF-001")
def test_battery_boundary(sim, volts, expected_mode): ...
```

* `tests/conftest.py` rejects unknown requirement IDs at collection time.
* The IDs are written into JUnit XML as `<property name="requirements">`.
* `tools/traceability_report.py` joins JUnit results with the catalogue and
  produces `reports/traceability.md`. It **fails** if any requirement has no
  test or any linked test failed.

The committed snapshot [test-matrix.md](test-matrix.md) shows the result of
a verified run. Columns: Requirement ID, Component, Requirement text,
Automated tests, Passed/Total, Result.

## 5. Entry and exit criteria

**Entry:** the simulator compiles with `-Wall -Wextra -Wpedantic -Wshadow
-Wconversion -Wsign-conversion` and no warnings (CI uses `-Werror`).

**Exit (all required):**

* All C++ unit tests pass.
* All pytest tests pass (0 failed, 0 errors). Skipped tests are reported, not hidden.
* All fault-injection scenarios pass.
* Every requirement in the catalogue has at least one passing test.
* The negative-control scenario and the seeded defect are both detected.

## 6. Running the tests

```bash
scripts/build.sh                      # compile
scripts/run_tests.sh                  # all stages + reports (exit != 0 on any failure)
scripts/run_tests.sh system           # one stage: unit | system | fault | regression | report

source .venv/bin/activate             # or use .venv/bin/python -m pytest
pytest                                # whole pytest suite (builds the binary if needed)
pytest tests/test_safety_rules.py -v  # one file
pytest -k battery_boundary -v         # by name
pytest -m fault_injection             # by marker: boundary, negative, fault_injection, regression
pytest --junitxml=reports/junit.xml   # JUnit XML for any selection
```

## 7. Sample test-report workflow

1. `scripts/build.sh && scripts/run_tests.sh`
2. Read the console summary. Any failing stage is named at the end and the
   script exits non-zero.
3. Open `reports/`:
   * `unit-tests.xml`, `junit-system.xml`, `junit-fault-injection.xml`,
     `junit-regression.xml`: machine-readable results (Jenkins/GitHub).
   * `fault_injection_report.md`: per-scenario expected vs actual, PASS/FAIL,
     diagnostics.
   * `traceability.md`: requirement coverage and results.
4. For a failing test, pytest prints the assertion and the relevant JSON.
   To reproduce by hand, copy the commands into a file and run
   `./build/cubesat_sim --config config/simulation_config.json --script file.txt`
   (logs on stderr).
5. Fix the **implementation**, not the expectation, unless the expectation is
   genuinely wrong per the specification in [state-machine.md](state-machine.md).

## 8. Test inventory (verified run)

Counts come from the JUnit XML of a real run (see the README "Verified
results" section for environment and date):

| Suite | Tests |
|-------|------:|
| C++ unit tests | 21 |
| pytest system tests | 260 |
| pytest fault-injection tests | 19 |
| pytest regression tests | 6 |
| **Total** | **306** |

No code-coverage percentage is reported because coverage was not measured.
