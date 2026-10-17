"""Startup and power-on self test (BOOT mode) behaviour."""

import pytest

from simharness import NOMINAL_BOOT, telemetry


@pytest.mark.req("REQ-BOOT-001")
def test_powers_on_in_boot_mode(sim):
    run = sim.run(["STATUS"])
    status = run.final_status
    assert status["mode"] == "BOOT"
    assert status["payload"] == "OFF"
    assert status["telemetry_available"] is False
    assert status["active_violations"] == []
    assert status["comm_link"] == "UNKNOWN"
    assert status["sim_time_s"] == 0
    assert run.events[0]["event"] == "POWER_ON"
    assert run.events[0]["mode"] == "BOOT"


@pytest.mark.req("REQ-BOOT-002", "REQ-MODE-007")
def test_init_with_nominal_telemetry_enters_nominal(sim):
    run = sim.run([*NOMINAL_BOOT, "STATUS"])
    init = run.responses[1]
    assert init["command"] == "INIT"
    assert init["status"] == "OK"
    assert init["data"] == {"self_test": "PASSED", "active_violations": []}
    assert init["mode"] == "NOMINAL"
    assert run.mode_changes() == [("BOOT", "NOMINAL")]
    assert "SELF_TEST_PASSED" in run.event_names()
    assert run.final_status["comm_link"] == "UP"


@pytest.mark.negative
@pytest.mark.req("REQ-BOOT-003")
def test_init_without_telemetry_is_rejected(sim):
    run = sim.run(["INIT", "STATUS"])
    assert run.responses[0]["status"] == "REJECTED"
    assert run.error_code(0) == "NO_TELEMETRY"
    assert run.final_status["mode"] == "BOOT"
    assert run.mode_changes() == []


@pytest.mark.req("REQ-BOOT-003")
def test_init_succeeds_after_telemetry_arrives(sim):
    run = sim.run(["INIT", telemetry(1), "INIT"])
    assert [r["status"] for r in run.responses] == ["REJECTED", "OK", "OK"]
    assert run.final_mode == "NOMINAL"


@pytest.mark.req("REQ-BOOT-004", "REQ-MODE-005")
@pytest.mark.parametrize(
    "frame, rule",
    [
        (telemetry(1, battery_v=6.5), "BATTERY_LOW"),
        (telemetry(1, temp_c=75), "TEMPERATURE_HIGH"),
        (telemetry(1, temp_c=-30), "TEMPERATURE_LOW"),
        (telemetry(1, attitude_deg=30), "ATTITUDE_EXCEEDED"),
        (telemetry(1, sensors="INVALID"), "SENSOR_INVALID"),
    ],
    ids=["battery", "temp_high", "temp_low", "attitude", "sensors"],
)
def test_init_with_violating_telemetry_enters_safe_mode(sim, frame, rule):
    run = sim.run([frame, "INIT", "STATUS"])
    assert run.responses[1]["data"]["active_violations"] == [rule]
    assert run.mode_changes() == [("BOOT", "SAFE_MODE")]
    status = run.final_status
    assert status["mode"] == "SAFE_MODE"
    assert status["last_safe_mode_reason"] == rule


@pytest.mark.req("REQ-BOOT-004", "REQ-TLM-005")
def test_init_with_stale_telemetry_enters_safe_mode(sim):
    run = sim.run([telemetry(1), "TICK 11", "INIT", "STATUS"])
    assert run.final_status["mode"] == "SAFE_MODE"
    assert run.final_status["active_violations"] == ["TELEMETRY_STALE"]


@pytest.mark.req("REQ-BOOT-001")
def test_safety_rules_are_not_evaluated_in_boot(sim):
    run = sim.run([telemetry(1, battery_v=1.0), "TICK 3600", "STATUS"])
    status = run.final_status
    assert status["mode"] == "BOOT"
    assert status["active_violations"] == []
    assert run.events_named("SAFETY_VIOLATION") == []


@pytest.mark.req("REQ-BOOT-005", "REQ-MODE-007")
def test_injected_self_test_failure_enters_fault(sim):
    run = sim.run(["FAULT_INJECT SELF_TEST_FAILURE", telemetry(1), "INIT", "STATUS"])
    assert run.responses[2]["data"]["self_test"] == "FAILED"
    assert run.mode_changes() == [("BOOT", "FAULT")]
    status = run.final_status
    assert status["mode"] == "FAULT"
    assert status["fault_reason"] == "SELF_TEST_FAILURE"
    assert run.events_named("SELF_TEST_FAILED")[0]["severity"] == "ERROR"


@pytest.mark.negative
@pytest.mark.req("REQ-BOOT-005", "REQ-CMD-004")
def test_self_test_failure_can_only_be_armed_in_boot(sim):
    run = sim.run([*NOMINAL_BOOT, "FAULT_INJECT SELF_TEST_FAILURE", "STATUS"])
    assert run.error_code(2) == "NOT_PERMITTED_IN_MODE"
    assert run.final_status["mode"] == "NOMINAL"
