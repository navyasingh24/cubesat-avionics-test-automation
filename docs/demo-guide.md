# Demonstration Guide

Useful for an interview walkthrough or a short screen recording. Every
command below was run during development. Output is deterministic, so you
will see the same results.

## 1. One-command demo (about 10 seconds)

```bash
scripts/run_demo.sh
```

It builds the simulator, plays a simulated "mission day" as a readable
table, lists the mode changes, runs all fault-injection scenarios, then runs
two deliberate failure checks and shows that both are detected. It exits 0
only if passing checks passed **and** deliberately failing checks failed.

## 2. Talk-through scenarios

| # | What to show | Command | What to point out |
|---|--------------|---------|-------------------|
| 1 | Normal boot | `printf 'INIT\nTELEMETRY seq=1 battery_v=7.6 temp_c=22.5 attitude_deg=2.0 comm=UP sensors=VALID\nINIT\nSTATUS\n' \| ./build/cubesat_sim --config config/simulation_config.json 2>/dev/null \| python3 tools/format_session.py` | `INIT` is refused until telemetry exists (`NO_TELEMETRY`) |
| 2 | Safety response | `scripts/run_demo.sh` (step 2) | Cold eclipse at t=10 s → `TEMPERATURE_LOW` → SAFE_MODE; payload refused; recovery refused until the temperature clears |
| 3 | Boundary precision | `.venv/bin/python -m pytest tests/test_safety_rules.py -k battery_boundary -v` | 6.80 V is OK, 6.79 V trips. Strict versus inclusive comparisons are specified and tested |
| 4 | Fault-injection report | `python3 tools/run_scenarios.py` then open `reports/fault_injection_report.md` | Expected vs actual, PASS/FAIL, diagnostics for 19 scenarios |
| 5 | Communication loss is not a FAULT | `.venv/bin/python -m pytest tests/test_safety_rules.py -k comm -v` | 60 s without contact = warning only; 61 s = SAFE_MODE; never FAULT |
| 6 | Illegal transitions | `.venv/bin/python -m pytest tests/test_operating_modes.py -k fault_cannot -v` | 9 different attempts to leave FAULT; only `RESET CONFIRM` works |
| 7 | Detecting a wrong expectation | `python3 tools/run_scenarios.py --scenario-dir scenarios/negative_control --report-name negative_control_report; echo "exit=$?"` | FAIL with exact mismatches, exit code 1 |
| 8 | Detecting a real bug | `scripts/verify_defect_detection.sh` | A one-character change (`<` to `<=`) is caught by exactly one boundary test |
| 9 | Traceability | `scripts/run_tests.sh` then open `reports/traceability.md` | 42 requirements, each linked to passing tests |
| 10 | Robust input | `head -c 20000 /dev/urandom \| ./build/cubesat_sim --config config/simulation_config.json 2>/dev/null \| tail -n 2; echo "exit=${PIPESTATUS[1]}"` | Random bytes produce structured rejections and exit 0, with no crash |

## 3. Interactive session

```bash
./build/cubesat_sim --config config/simulation_config.json --log-file /tmp/cubesat.log.jsonl
```

Type commands such as `HELP`, `STATUS`, `TELEMETRY seq=1 battery_v=7.6
temp_c=22.5 attitude_deg=2.0 comm=UP sensors=VALID`, `INIT`, `TICK 30`.
Press **Ctrl-D** to end. Watch the diagnostics in a second terminal with
`tail -f /tmp/cubesat.log.jsonl`.
