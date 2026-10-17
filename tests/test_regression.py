"""Regression tests: golden reference outputs and determinism.

Golden files live in tests/golden/. Each ``<name>.script`` has a recorded
``<name>.stdout.jsonl`` (responses) and ``<name>.log.jsonl`` (diagnostics).
Any behaviour change makes these tests fail until the golden files are
deliberately regenerated with tools/update_golden.py and the diff reviewed.
"""

import json
from pathlib import Path

import pytest

from simharness import EXIT_OK, NOMINAL_BOOT, telemetry

GOLDEN_DIR = Path(__file__).parent / "golden"
SCRIPTS = sorted(GOLDEN_DIR.glob("*.script"))


def _first_difference(expected_lines, actual_lines):
    for index, (exp, act) in enumerate(zip(expected_lines, actual_lines), start=1):
        if exp != act:
            return f"line {index} differs:\n  expected: {exp}\n  actual:   {act}"
    if len(expected_lines) != len(actual_lines):
        return f"line count differs: expected {len(expected_lines)}, got {len(actual_lines)}"
    return None


@pytest.mark.regression
@pytest.mark.req("REQ-REG-001")
@pytest.mark.parametrize("script", SCRIPTS, ids=[s.stem for s in SCRIPTS])
def test_golden_responses(sim, script):
    run = sim.run(script.read_bytes())
    assert run.exit_code == EXIT_OK
    expected = script.with_suffix(".stdout.jsonl").read_text(encoding="utf-8").splitlines()
    difference = _first_difference(expected, run.stdout.splitlines())
    assert difference is None, f"{script.name}: {difference}"


@pytest.mark.regression
@pytest.mark.req("REQ-REG-001", "REQ-LOG-002")
@pytest.mark.parametrize("script", SCRIPTS, ids=[s.stem for s in SCRIPTS])
def test_golden_log(sim, script):
    run = sim.run(script.read_bytes())
    expected = script.with_suffix(".log.jsonl").read_text(encoding="utf-8").splitlines()
    difference = _first_difference(expected, run.stderr.splitlines())
    assert difference is None, f"{script.name} log: {difference}"


@pytest.mark.regression
@pytest.mark.req("REQ-REG-001")
def test_golden_files_are_well_formed():
    assert SCRIPTS, "no golden scripts found"
    for script in SCRIPTS:
        for suffix in (".stdout.jsonl", ".log.jsonl"):
            path = script.with_suffix(suffix)
            assert path.is_file(), f"missing {path.name}; run tools/update_golden.py"
            for line in path.read_text(encoding="utf-8").splitlines():
                json.loads(line)


@pytest.mark.regression
@pytest.mark.req("REQ-LOG-002")
def test_identical_input_gives_byte_identical_output(sim):
    commands = [*NOMINAL_BOOT]
    for seq in range(2, 200):
        commands.append(telemetry(seq, battery_v=6.5 + (seq % 7) * 0.2, temp_c=seq % 80 - 25))
        commands.append(f"TICK {seq % 13 + 1}")
        if seq % 17 == 0:
            commands.append("EXIT_SAFE_MODE")
    first = sim.run(commands)
    second = sim.run(commands)
    assert first.stdout == second.stdout
    assert first.stderr == second.stderr
    assert len(first.responses) == len(commands)
