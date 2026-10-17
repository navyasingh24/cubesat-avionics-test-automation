"""Recovery paths: SAFE_MODE -> NOMINAL and FAULT -> BOOT -> NOMINAL."""

import pytest

from simharness import NOMINAL_BOOT, telemetry


@pytest.mark.req("REQ-REC-001")
def test_full_recovery_cycle_and_second_event(sim):
    run = sim.run([
        *NOMINAL_BOOT,
        "PAYLOAD ON",
        telemetry(2, temp_c=62),        # violation -> SAFE_MODE, payload off
        telemetry(3, temp_c=54),        # clears (<= 55)
        "EXIT_SAFE_MODE",
        "PAYLOAD ON",                   # operator re-enables the payload
        telemetry(4, battery_v=6.5),    # a second, different event
        "STATUS",
    ])
    assert [r["status"] for r in run.responses[:-1]] == ["OK"] * 8
    status = run.final_status
    assert status["mode"] == "SAFE_MODE"
    assert status["last_safe_mode_reason"] == "BATTERY_LOW"
    assert status["counters"]["safe_mode_entries"] == 2
    assert run.mode_changes() == [
        ("BOOT", "NOMINAL"), ("NOMINAL", "SAFE_MODE"), ("SAFE_MODE", "NOMINAL"), ("NOMINAL", "SAFE_MODE"),
    ]


@pytest.mark.req("REQ-REC-001")
def test_recovery_is_not_automatic(sim):
    """Clearing a violation does not by itself leave SAFE_MODE (ground-in-the-loop design)."""
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=6.0), telemetry(3, battery_v=7.5), "TICK 5", "STATUS"])
    assert run.final_status["active_violations"] == []
    assert run.final_status["mode"] == "SAFE_MODE"


@pytest.mark.req("REQ-REC-002")
def test_reset_from_fault_clears_volatile_state(sim):
    run = sim.run([*NOMINAL_BOOT, "PAYLOAD ON", "TICK 7", "FAULT_INJECT INTERNAL_ERROR", "STATUS",
                   "RESET CONFIRM", "STATUS", "INIT", telemetry(1), "INIT", "STATUS"])
    in_fault = run.responses[5]["data"]
    assert in_fault["mode"] == "FAULT"
    assert in_fault["fault_reason"] == "INJECTED_INTERNAL_ERROR"
    after_reset = run.responses[7]["data"]
    assert after_reset["mode"] == "BOOT"
    assert after_reset["telemetry_available"] is False
    assert after_reset["active_violations"] == []
    assert after_reset["payload"] == "OFF"
    assert after_reset["fault_reason"] is None
    assert after_reset["comm_link"] == "UNKNOWN"
    assert after_reset["sim_time_s"] == 7, "simulated time continues across a reset"
    assert after_reset["counters"]["resets"] == 1
    assert run.error_code(8) == "NO_TELEMETRY", "INIT needs fresh telemetry after a reset"
    assert run.final_status["mode"] == "NOMINAL"
    assert run.events_named("RESET_REQUESTED")


@pytest.mark.req("REQ-REC-002")
@pytest.mark.parametrize("start", ["NOMINAL", "SAFE_MODE"])
def test_reset_is_also_allowed_from_nominal_and_safe_mode(sim, reach_mode, start):
    setup = reach_mode(start)
    run = sim.run([*setup, "RESET CONFIRM", "STATUS"])
    assert run.responses[len(setup) - 1]["mode"] == start
    assert run.final_status["mode"] == "BOOT"


@pytest.mark.negative
@pytest.mark.req("REQ-REC-002", "REQ-CMD-004")
def test_reset_is_not_permitted_in_boot(sim):
    run = sim.run(["RESET CONFIRM"])
    assert run.error_code(0) == "NOT_PERMITTED_IN_MODE"


@pytest.mark.req("REQ-REC-002")
def test_reset_from_fault_with_violating_telemetry_goes_to_safe_mode_not_nominal(sim):
    run = sim.run([*NOMINAL_BOOT, "FAULT_INJECT INTERNAL_ERROR", "RESET CONFIRM", telemetry(1, temp_c=80),
                   "INIT", "STATUS"])
    assert run.final_status["mode"] == "SAFE_MODE"
    assert ("FAULT", "NOMINAL") not in run.mode_changes()
