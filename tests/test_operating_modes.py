"""Operating-mode state machine: transitions, restrictions and invariants."""

import random

import pytest

from simharness import NOMINAL_BOOT, telemetry

# Legal transitions (docs/state-machine.md), written independently of the C++.
ALLOWED_TRANSITIONS = {
    ("BOOT", "NOMINAL"),
    ("BOOT", "SAFE_MODE"),
    ("BOOT", "FAULT"),
    ("NOMINAL", "SAFE_MODE"),
    ("NOMINAL", "FAULT"),
    ("NOMINAL", "BOOT"),
    ("SAFE_MODE", "NOMINAL"),
    ("SAFE_MODE", "FAULT"),
    ("SAFE_MODE", "BOOT"),
    ("FAULT", "BOOT"),
}


@pytest.mark.req("REQ-MODE-001", "REQ-MODE-007")
def test_safety_violation_moves_nominal_to_safe_mode(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, temp_c=61), "STATUS"])
    assert run.responses[2]["mode"] == "SAFE_MODE"
    change = run.events_named("MODE_CHANGE")[-1]
    assert change["details"] == {"from": "NOMINAL", "to": "SAFE_MODE", "reason": "TEMPERATURE_HIGH"}
    assert change["severity"] == "WARNING"
    assert run.final_status["last_safe_mode_reason"] == "TEMPERATURE_HIGH"
    assert run.final_status["counters"]["safe_mode_entries"] == 1


@pytest.mark.req("REQ-MODE-002")
def test_ground_command_enters_safe_mode(sim):
    run = sim.run([*NOMINAL_BOOT, "ENTER_SAFE_MODE", "STATUS"])
    assert run.responses[2]["data"] == {"previous_mode": "NOMINAL", "changed": True}
    assert run.final_status["mode"] == "SAFE_MODE"
    assert run.final_status["last_safe_mode_reason"] == "GROUND_COMMAND"
    assert run.final_status["active_violations"] == []


@pytest.mark.req("REQ-MODE-003")
def test_exit_safe_mode_without_violations_returns_to_nominal(sim):
    run = sim.run([*NOMINAL_BOOT, "ENTER_SAFE_MODE", "EXIT_SAFE_MODE", "STATUS"])
    assert run.responses[3]["status"] == "OK"
    assert run.final_status["mode"] == "NOMINAL"
    assert run.mode_changes()[-1] == ("SAFE_MODE", "NOMINAL")
    assert run.events_named("RECOVERY_SUCCEEDED")


@pytest.mark.negative
@pytest.mark.req("REQ-MODE-003")
def test_exit_safe_mode_refused_while_violation_active(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=6.0), "EXIT_SAFE_MODE", "STATUS"])
    response = run.responses[3]
    assert response["status"] == "REJECTED"
    assert response["error"]["code"] == "ACTIVE_VIOLATIONS"
    assert "BATTERY_LOW" in response["error"]["message"]
    assert run.final_status["mode"] == "SAFE_MODE"
    denied = run.events_named("RECOVERY_DENIED")
    assert denied and denied[0]["details"]["active_violations"] == ["BATTERY_LOW"]


@pytest.mark.negative
@pytest.mark.req("REQ-MODE-004")
@pytest.mark.parametrize(
    "attempt",
    ["EXIT_SAFE_MODE", "ENTER_SAFE_MODE", "INIT", telemetry(5), "PAYLOAD ON",
     "FAULT_INJECT INTERNAL_ERROR", "RESET", "RESET NOW", "TICK 100"],
)
def test_fault_cannot_be_left_except_by_reset_confirm(sim, attempt):
    run = sim.run([*NOMINAL_BOOT, "FAULT_INJECT INTERNAL_ERROR", attempt, "STATUS"])
    assert run.responses[2]["mode"] == "FAULT"
    assert run.final_status["mode"] == "FAULT"
    assert run.mode_changes() == [("BOOT", "NOMINAL"), ("NOMINAL", "FAULT")]


@pytest.mark.req("REQ-MODE-004", "REQ-REC-002")
def test_reset_confirm_leaves_fault_to_boot_only(sim):
    run = sim.run([*NOMINAL_BOOT, "FAULT_INJECT INTERNAL_ERROR", "RESET CONFIRM", "STATUS"])
    assert run.final_status["mode"] == "BOOT"
    assert run.mode_changes()[-1] == ("FAULT", "BOOT")


@pytest.mark.req("REQ-MODE-004")
def test_comm_and_telemetry_problems_never_cause_fault(sim):
    # Long silence: telemetry goes stale and the link times out.
    run = sim.run([*NOMINAL_BOOT, telemetry(2, comm="DOWN"), "TICK 3600", "TICK 3600", "STATUS"])
    status = run.final_status
    assert status["mode"] == "SAFE_MODE"
    assert set(status["active_violations"]) == {"TELEMETRY_STALE", "COMM_TIMEOUT"}
    assert status["counters"]["faults"] == 0


@pytest.mark.req("REQ-MODE-006")
def test_payload_is_forced_off_when_leaving_nominal(sim):
    run = sim.run([*NOMINAL_BOOT, "PAYLOAD ON", "STATUS", telemetry(2, attitude_deg=40), "STATUS",
                   "PAYLOAD ON", telemetry(3), "EXIT_SAFE_MODE", "STATUS"])
    assert run.responses[3]["data"]["payload"] == "ON"
    assert run.responses[5]["data"]["payload"] == "OFF"
    assert run.events_named("PAYLOAD_POWER_OFF")
    assert run.error_code(6) == "NOT_PERMITTED_IN_MODE"
    final = run.final_status
    assert final["mode"] == "NOMINAL"
    assert final["payload"] == "OFF", "payload must not switch back on automatically"


@pytest.mark.req("REQ-MODE-006")
@pytest.mark.parametrize("leave", ["ENTER_SAFE_MODE", "FAULT_INJECT INTERNAL_ERROR", "RESET CONFIRM"])
def test_payload_off_after_any_exit_from_nominal(sim, leave):
    run = sim.run([*NOMINAL_BOOT, "PAYLOAD ON", leave, "STATUS"])
    assert run.final_status["mode"] != "NOMINAL"
    assert run.final_status["payload"] == "OFF"


@pytest.mark.req("REQ-MODE-007")
def test_mode_change_events_match_responses(sim):
    commands = [*NOMINAL_BOOT, telemetry(2, battery_v=6.0), telemetry(3, battery_v=7.5),
                "EXIT_SAFE_MODE", "FAULT_INJECT INTERNAL_ERROR", "RESET CONFIRM"]
    run = sim.run(commands)
    response_modes = ["BOOT"] + [r["mode"] for r in run.responses]
    observed = [(a, b) for a, b in zip(response_modes, response_modes[1:]) if a != b]
    assert run.mode_changes() == observed
    for event in run.events_named("MODE_CHANGE"):
        assert set(event["details"]) == {"from", "to", "reason"}
        assert event["details"]["reason"]
        assert event["mode"] == event["details"]["to"]


def _random_command(rng: random.Random, seq: int) -> str:
    choice = rng.random()
    if choice < 0.45:
        return telemetry(
            seq,
            battery_v=round(rng.uniform(6.0, 8.0), 2),
            temp_c=round(rng.uniform(-30, 70), 1),
            attitude_deg=round(rng.uniform(0, 25), 1),
            comm=rng.choice(["UP", "UP", "UP", "DOWN"]),
            sensors=rng.choice(["VALID"] * 9 + ["INVALID"]),
        )
    return rng.choice(
        ["TICK 5", "TICK 12", "TICK 70", "INIT", "ENTER_SAFE_MODE", "EXIT_SAFE_MODE",
         "PAYLOAD ON", "PAYLOAD OFF", "RESET CONFIRM", "FAULT_INJECT INTERNAL_ERROR",
         "GET_TELEMETRY", "BOGUS"]
    )


@pytest.mark.req("REQ-MODE-005", "REQ-MODE-006", "REQ-MODE-007")
@pytest.mark.parametrize("seed", range(1, 9))
def test_invariants_hold_for_random_command_sequences(sim, seed):
    """Seeded (reproducible) random walk through the state machine.

    After every command a STATUS is taken and the invariants are checked:
    never NOMINAL with active violations, payload only ON in NOMINAL, every
    transition in the documented table.
    """
    rng = random.Random(seed)
    commands = [*NOMINAL_BOOT]
    for seq in range(2, 302):
        commands += [_random_command(rng, seq), "STATUS"]
    run = sim.run(commands)
    assert len(run.responses) == len(commands)
    for response in run.responses:
        if response["command"] != "STATUS":
            continue
        status = response["data"]
        if status["mode"] == "NOMINAL":
            assert status["active_violations"] == [], response
        if status["payload"] == "ON":
            assert status["mode"] == "NOMINAL", response
    for transition in run.mode_changes():
        assert transition in ALLOWED_TRANSITIONS, transition
    assert run.events_named("INVARIANT_VIOLATION") == []
    assert len(set(run.mode_changes())) >= 3, "random walk should exercise several transitions"
