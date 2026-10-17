"""Data-driven fault-injection scenarios (scenarios/*.json).

Each scenario is run against the real simulator by simharness.scenarios,
the same engine used by tools/run_scenarios.py to produce the report.
"""

import re

import pytest

from simharness.scenarios import SCENARIO_DIR, load_scenarios, run_scenario

SCENARIOS = load_scenarios()
REQUIRED_SCENARIO_IDS = {f"FI-{n:02d}" for n in range(1, 15)}


def _params():
    for scenario in SCENARIOS:
        marks = [pytest.mark.req("REQ-FI-001", *scenario.get("requirements", []))]
        yield pytest.param(scenario, id=f"{scenario['id']}-{scenario['title']}", marks=marks)


@pytest.mark.fault_injection
@pytest.mark.parametrize("scenario", list(_params()))
def test_fault_injection_scenario(sim, scenario):
    result = run_scenario(sim, scenario)
    details = "\n".join(
        ["Mismatches:"] + [f"  - {m}" for m in result.mismatches]
        + ["Diagnostics:"] + [f"  {d}" for d in result.diagnostics]
    )
    assert result.passed, f"{scenario['id']} ({scenario['_path']}) failed\n{details}"


@pytest.mark.req("REQ-FI-001")
def test_required_scenarios_exist_and_ids_are_unique():
    ids = [s["id"] for s in SCENARIOS]
    assert len(ids) == len(set(ids)), "duplicate scenario IDs"
    assert REQUIRED_SCENARIO_IDS <= set(ids)
    for scenario in SCENARIOS:
        assert re.fullmatch(r"FI-\d{2}", scenario["id"])
        for key in ("title", "description", "initial_state", "expect"):
            assert scenario.get(key) not in (None, ""), f"{scenario['id']} missing {key}"


@pytest.mark.req("REQ-FI-001")
def test_negative_control_scenario_is_detected_as_failure(sim):
    """The runner must FAIL a scenario whose expectation is wrong.

    NC-01 claims low battery keeps the computer NOMINAL. If this test ever
    sees NC-01 'pass', the scenario engine is not really checking anything.
    """
    (negative,) = load_scenarios(SCENARIO_DIR / "negative_control")
    result = run_scenario(sim, negative)
    assert not result.passed
    assert any(m.startswith("final_mode: expected NOMINAL, got SAFE_MODE") for m in result.mismatches)
