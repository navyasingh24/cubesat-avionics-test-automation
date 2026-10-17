# Fault-Injection Framework

> All faults are **simulated**. Nothing here touches real hardware or files
> outside a temporary directory that the harness creates and deletes.

## What fault injection means here

Fault injection deliberately feeds a system *bad conditions* (failing
sensors, lost links, corrupt input, broken configuration, internal errors)
and checks that it reacts **in the specified way**. A good response means
entering the right mode, rejecting the right input with the right error
code, logging the right event, and never crashing.

This project injects faults at four levels:

| Level | How it is injected | Examples |
|-------|--------------------|----------|
| Telemetry | `TELEMETRY` frames with out-of-limit, implausible, invalid-flagged or replayed values | low battery, over-temperature, `sensors=INVALID`, `battery_v=25` |
| Time | `TICK` without fresh frames or without `comm=UP` | stale telemetry, communication timeout |
| Commands / input bytes | unknown commands, bad arguments, control characters, oversized lines | `DEPLOY_SOLAR_SAIL`, `TICK -5`, `\x07STATUS` |
| Environment | broken configuration file, missing script file | contradictory thresholds, truncated JSON |
| Internal | `FAULT_INJECT INTERNAL_ERROR`, `FAULT_INJECT SELF_TEST_FAILURE` | simulated software failure |

## Scenario files

Each scenario is a JSON file in `scenarios/` that states its precondition,
the fault and the exact expectation. For example, `FI-01_low_battery.json`:

```json
{
  "id": "FI-01",
  "title": "Low battery",
  "requirements": ["REQ-SAF-001", "REQ-MODE-001", "REQ-MODE-006"],
  "initial_state": "NOMINAL",
  "setup": ["@nominal_boot", "PAYLOAD ON"],
  "inject": ["TELEMETRY seq=2 battery_v=6.79 temp_c=22.5 attitude_deg=2.0 comm=UP sensors=VALID"],
  "expect": {
    "inject_status": ["OK"],
    "final_mode": "SAFE_MODE",
    "active_violations": ["BATTERY_LOW"],
    "events_present": ["SAFETY_VIOLATION", "MODE_CHANGE", "PAYLOAD_POWER_OFF"],
    "modes_never": ["FAULT"]
  }
}
```

How `simharness/scenarios.py` runs it:

1. Expand macros (`@nominal_boot` = one nominal frame + `INIT`).
2. Optionally write a patched or corrupt configuration to a temporary directory.
3. Send `setup + inject + STATUS` to **one** simulator process.
4. **Check the precondition:** the mode after `setup` must equal
   `initial_state`. A scenario cannot pass by accident from the wrong state.
5. Compare every expectation:

| `expect` key | Checked against |
|--------------|-----------------|
| `exit_code`, `stdout_error_code` | process exit code and the structured error object |
| `inject_status`, `inject_error_codes` | the response to each injected command, in order |
| `final_mode`, `active_violations` | the final `STATUS` (violations compared as a set) |
| `events_present`, `events_absent` | log events emitted **after** the setup phase |
| `modes_never` | every mode reported during injection |

6. Any difference is reported as a mismatch and the scenario FAILS.
   Expectations are never relaxed.

## Scenario catalogue

| ID | Scenario | Initial state | Fault injected | Expected behaviour |
|----|----------|---------------|----------------|--------------------|
| FI-01 | Low battery | NOMINAL, payload ON | `battery_v=6.79` (just below 6.8) | SAFE_MODE, `BATTERY_LOW`, payload forced OFF, never FAULT |
| FI-02 | Excessive temperature | NOMINAL | `temp_c=60.01` (just above 60) | SAFE_MODE, `TEMPERATURE_HIGH` |
| FI-03 | Invalid sensor readings | NOMINAL | `battery_v=25` (implausible), then `sensors=INVALID` with garbage `battery_v=0.5` | First frame rejected `OUT_OF_RANGE`; second gives SAFE_MODE with **only** `SENSOR_INVALID` (garbage value not trusted) |
| FI-04 | Missing telemetry | NOMINAL | incomplete frame; no frames for 10 s then 11 s | `MISSING_FIELD`; still NOMINAL at 10 s; SAFE_MODE with `TELEMETRY_STALE` at 11 s |
| FI-05 | Communication timeout | NOMINAL, `comm_timeout_s=20` | `comm=DOWN` frames for 20 s, then 21 s | warning only up to 20 s; SAFE_MODE with `COMM_TIMEOUT` at 21 s; never FAULT |
| FI-06 | Unknown command | NOMINAL | `DEPLOY_SOLAR_SAIL`, `self_destruct now` | `UNKNOWN_COMMAND`, logged, no mode change |
| FI-07 | Malformed arguments | NOMINAL | 9 invalid argument forms (`TICK abc`, `TICK -5`, `TICK 0`, `TICK 3601`, `PAYLOAD MAYBE`, `RESET NOW`, ...) | all `INVALID_ARGUMENT`, no state change |
| FI-08 | Out-of-range orientation | NOMINAL | `attitude_deg=181`, then `15.01` | `OUT_OF_RANGE`; then SAFE_MODE with `ATTITUDE_EXCEEDED` |
| FI-09 | Recovery after recoverable fault | SAFE_MODE (low battery) | `EXIT_SAFE_MODE` at 6.5 V, 6.9 V and 7.0 V | refused, refused (hysteresis), accepted; payload can be re-enabled |
| FI-10 | Multiple simultaneous violations | NOMINAL | battery + temperature + attitude in one frame; then only battery recovers | all three reported; recovery refused while two remain |
| FI-11 | Contradictory configuration | (not started) | `battery_recover_v` 6.0 < `battery_min_v` 6.8 | exit code 3, `CONFIG_INVALID` |
| FI-12 | Repeated command execution | NOMINAL | replayed frame (same `seq`), `ENTER_SAFE_MODE` x3 | `STALE_SEQUENCE`; mode command idempotent |
| FI-13 | Unexpected transition attempts | FAULT | `EXIT_SAFE_MODE`, `ENTER_SAFE_MODE`, `INIT`, `TELEMETRY`, `PAYLOAD ON`, `FAULT_INJECT` | all `NOT_PERMITTED_IN_MODE`, stays FAULT |
| FI-14 | Missing input data | (not started) | `--script` file does not exist | exit code 4, `INPUT_NOT_FOUND` |
| FI-15 | Corrupt configuration | (not started) | truncated JSON | exit code 3, `CONFIG_INVALID` |
| FI-16 | Missing configuration | (not started) | config path does not exist | exit code 3, `CONFIG_NOT_FOUND` |
| FI-17 | Corrupt command bytes | NOMINAL | BEL control char, non-ASCII byte, 305-byte line | `MALFORMED_INPUT` x3, then a valid command still works |
| FI-18 | Self-test failure | BOOT | `FAULT_INJECT SELF_TEST_FAILURE`, then `INIT` | FAULT, `SELF_TEST_FAILED` |
| FI-19 | Controlled recovery from FAULT | FAULT | `RESET CONFIRM`, new frame, `INIT` | BOOT, then NOMINAL |

FI-01 to FI-14 are the fourteen scenario types in the project brief; FI-15
to FI-19 add more coverage. Scenarios FI-11, FI-14, FI-15 and FI-16 check that
the simulator **refuses to start**, so their "initial state" is "not started".

## Actual results

The **Actual behaviour**, **Pass/Fail** and **diagnostic output** columns are
produced from a real run, never written by hand:

```bash
python3 tools/run_scenarios.py      # writes reports/fault_injection_report.md and .json
```

A snapshot from a verified run is committed as
[fault-injection-report.md](fault-injection-report.md).

## Proving the framework can fail

A test framework that never fails proves nothing. Two deliberate controls
exist:

1. **Negative-control scenario**: `scenarios/negative_control/NC-01_wrong_expectation.json`
   claims that low battery leaves the computer NOMINAL. It is outside
   `scenarios/*.json`, so it never runs in the normal suite.
   ```bash
   python3 tools/run_scenarios.py --scenario-dir scenarios/negative_control --report-name negative_control_report
   # -> FAIL NC-01 ... final_mode: expected NOMINAL, got SAFE_MODE   (exit code 1)
   ```
   `tests/test_fault_injection.py::test_negative_control_scenario_is_detected_as_failure`
   asserts that this scenario fails.
2. **Seeded defect (mutation check)**: building with
   `-DCUBESAT_SEEDED_DEFECT=ON` changes the battery comparison from `<` to
   `<=`. `scripts/verify_defect_detection.sh` builds that broken variant in
   `build-defect/` and passes **only if** `test_battery_boundary[6.8-NOMINAL]`
   fails. Only the exact boundary test catches this defect, which shows why
   boundary-value tests are needed.

## Adding a scenario

1. Copy an existing file to `scenarios/FI-NN_short_name.json` with a new unique ID.
2. Fill in `requirements` (IDs from `tests/requirements.json`),
   `initial_state`, `setup`, `inject` and the strictest `expect` you can state.
3. Run `python3 tools/run_scenarios.py`. pytest picks the new file up
   automatically through `tests/test_fault_injection.py`.
