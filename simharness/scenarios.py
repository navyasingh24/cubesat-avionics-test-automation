"""Data-driven fault-injection scenarios.

A scenario file (scenarios/*.json) describes:
  * the initial state, reached by ``setup`` commands and VERIFIED before the
    fault is judged (a scenario cannot pass by accident from a wrong state),
  * the fault to inject (``inject`` commands, or a broken config/input),
  * the expected behaviour (``expect``).

The runner sends setup + inject + a final STATUS to ONE simulator process,
compares actual against expected, and returns every mismatch. It never
weakens an expectation: any difference is a failure.
"""

from __future__ import annotations

import json
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

from .build import REPO_ROOT
from .simulator import EXIT_OK, NOMINAL_BOOT, Simulator

SCENARIO_DIR = REPO_ROOT / "scenarios"
MACROS = {"@nominal_boot": NOMINAL_BOOT}
DIAGNOSTIC_EVENTS = {
    "MODE_CHANGE",
    "SAFETY_VIOLATION",
    "SAFETY_CLEARED",
    "COMMAND_REJECTED",
    "TELEMETRY_REJECTED",
    "FAULT_INJECTED",
    "RECOVERY_ATTEMPT",
    "RECOVERY_DENIED",
    "RECOVERY_SUCCEEDED",
    "COMM_LINK_DOWN",
    "COMM_LINK_RESTORED",
    "SELF_TEST_FAILED",
    "PAYLOAD_POWER_OFF",
    "RESET_REQUESTED",
    "INVARIANT_VIOLATION",
}
ALLOWED_EXPECT_KEYS = {
    "exit_code",
    "stdout_error_code",
    "inject_status",
    "inject_error_codes",
    "final_mode",
    "active_violations",
    "events_present",
    "events_absent",
    "modes_never",
}


@dataclass
class ScenarioResult:
    scenario_id: str
    title: str
    passed: bool
    mismatches: List[str]
    actual: Dict[str, Any]
    diagnostics: List[str] = field(default_factory=list)


def expand(commands: List[str]) -> List[str]:
    out: List[str] = []
    for command in commands:
        out.extend(MACROS.get(command, [command]))
    return out


def load_scenarios(directory: Path = SCENARIO_DIR) -> List[Dict[str, Any]]:
    scenarios = []
    for path in sorted(directory.glob("*.json")):
        with open(path, encoding="utf-8") as handle:
            scenario = json.load(handle)
        try:
            scenario["_path"] = str(path.relative_to(REPO_ROOT))
        except ValueError:
            scenario["_path"] = str(path)
        unknown = set(scenario.get("expect", {})) - ALLOWED_EXPECT_KEYS
        if unknown:
            raise ValueError(f"{path}: unknown expect keys {sorted(unknown)}")
        scenarios.append(scenario)
    return scenarios


def _patched_config(base: Path, patch: Dict[str, Any]) -> Dict[str, Any]:
    with open(base, encoding="utf-8") as handle:
        config = json.load(handle)
    for dotted, value in patch.items():
        node = config
        *parents, leaf = dotted.split(".")
        for key in parents:
            node = node[key]
        if value == "__DELETE__":
            node.pop(leaf, None)
        else:
            node[leaf] = value
    return config


def run_scenario(sim: Simulator, scenario: Dict[str, Any]) -> ScenarioResult:
    expect = scenario["expect"]
    setup = expand(scenario.get("setup", []))
    inject = expand(scenario.get("inject", []))
    mismatches: List[str] = []

    with tempfile.TemporaryDirectory(prefix="cubesat-scenario-") as tmp:
        config_path: Optional[Path] = None
        if "config_raw" in scenario:
            config_path = Path(tmp) / "config.json"
            config_path.write_text(scenario["config_raw"], encoding="utf-8")
        elif "config_patch" in scenario:
            config_path = Path(tmp) / "config.json"
            config_path.write_text(
                json.dumps(_patched_config(sim.default_config, scenario["config_patch"])),
                encoding="utf-8",
            )
        elif scenario.get("config_missing"):
            config_path = Path(tmp) / "does-not-exist.json"

        extra_args: List[str] = []
        if scenario.get("script_missing"):
            extra_args = ["--script", str(Path(tmp) / "missing-script.txt")]

        commands = setup + inject + ["STATUS"]
        run = sim.run(commands, config=config_path, extra_args=extra_args)

    actual: Dict[str, Any] = {"exit_code": run.exit_code}
    expected_exit = expect.get("exit_code", EXIT_OK)
    if run.exit_code != expected_exit:
        mismatches.append(f"exit_code: expected {expected_exit}, got {run.exit_code}")

    if run.exit_code != EXIT_OK:
        code = run.responses[0]["error"]["code"] if run.responses else None
        actual["stdout_error_code"] = code
        if "stdout_error_code" in expect and code != expect["stdout_error_code"]:
            mismatches.append(
                f"stdout_error_code: expected {expect['stdout_error_code']}, got {code}"
            )
        return ScenarioResult(
            scenario["id"], scenario["title"], not mismatches, mismatches, actual,
            [line for line in run.stderr.splitlines() if line.strip()][:5],
        )

    expected_responses = len(setup) + len(inject) + 1
    if len(run.responses) != expected_responses:
        mismatches.append(
            f"response count: expected {expected_responses}, got {len(run.responses)}"
        )
        return ScenarioResult(scenario["id"], scenario["title"], False, mismatches, actual)

    # Precondition: the setup really reached the documented initial state.
    initial_mode = run.responses[len(setup) - 1]["mode"] if setup else "BOOT"
    actual["initial_mode"] = initial_mode
    if initial_mode != scenario["initial_state"]:
        mismatches.append(
            f"precondition: setup should reach {scenario['initial_state']}, got {initial_mode}"
        )

    inject_responses = run.responses[len(setup) : len(setup) + len(inject)]
    final_status = run.responses[-1]["data"]
    actual["inject_status"] = [r["status"] for r in inject_responses]
    actual["inject_error_codes"] = [
        r["error"]["code"] if r["status"] == "REJECTED" else None for r in inject_responses
    ]
    actual["final_mode"] = final_status["mode"]
    actual["active_violations"] = final_status["active_violations"]
    actual["modes_seen"] = sorted({r["mode"] for r in inject_responses})

    for key in ("inject_status", "inject_error_codes", "final_mode"):
        if key in expect and actual[key] != expect[key]:
            mismatches.append(f"{key}: expected {expect[key]}, got {actual[key]}")
    if "active_violations" in expect and sorted(actual["active_violations"]) != sorted(
        expect["active_violations"]
    ):
        mismatches.append(
            f"active_violations: expected {sorted(expect['active_violations'])}, "
            f"got {sorted(actual['active_violations'])}"
        )
    for mode in expect.get("modes_never", []):
        if mode in actual["modes_seen"] or actual["final_mode"] == mode:
            mismatches.append(f"mode {mode} must never occur, but it did")

    phase_events = run.events_after_command(len(setup))
    names = [e["event"] for e in phase_events]
    actual["events"] = sorted(set(names))
    for name in expect.get("events_present", []):
        if name not in names:
            mismatches.append(f"expected log event {name} was not emitted")
    for name in expect.get("events_absent", []):
        if name in names:
            mismatches.append(f"log event {name} must not be emitted, but was")

    diagnostics = [
        f"[{e['severity']}] {e['component']}.{e['event']} mode={e['mode']}: {e['message']}"
        + (f" ({e['details'].get('rule')}: {e['details'].get('detail')})" if "rule" in e["details"] else "")
        + (f" ({e['details'].get('code')})" if "code" in e["details"] else "")
        for e in phase_events
        if e["event"] in DIAGNOSTIC_EVENTS
    ]
    return ScenarioResult(
        scenario["id"], scenario["title"], not mismatches, mismatches, actual, diagnostics
    )
