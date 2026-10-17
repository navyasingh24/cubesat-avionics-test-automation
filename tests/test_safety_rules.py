"""Safety-rule boundary values, hysteresis and combined violations.

Demo thresholds (config/simulation_config.json; invented, not real limits):
  battery   trip < 6.8 V      clear >= 7.0 V
  temp high trip > 60.0 C     clear <= 55.0 C
  temp low  trip < -20.0 C    clear >= -15.0 C
  attitude  trip > 15.0 deg   clear <= 10.0 deg
Trip comparisons are strict, so a value exactly AT the limit is acceptable.
"""

import pytest

from simharness import NOMINAL_BOOT, telemetry


def _mode_after_frame(sim, **fields):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, **fields), "STATUS"])
    return run.final_status


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-001")
@pytest.mark.parametrize(
    "volts, expected_mode", [(7.6, "NOMINAL"), (6.81, "NOMINAL"), (6.8, "NOMINAL"), (6.79, "SAFE_MODE"), (0.0, "SAFE_MODE")]
)
def test_battery_boundary(sim, volts, expected_mode):
    status = _mode_after_frame(sim, battery_v=volts)
    assert status["mode"] == expected_mode
    assert ("BATTERY_LOW" in status["active_violations"]) == (expected_mode == "SAFE_MODE")


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-002")
@pytest.mark.parametrize("celsius, expected_mode", [(59.99, "NOMINAL"), (60.0, "NOMINAL"), (60.01, "SAFE_MODE")])
def test_temperature_high_boundary(sim, celsius, expected_mode):
    status = _mode_after_frame(sim, temp_c=celsius)
    assert status["mode"] == expected_mode
    assert ("TEMPERATURE_HIGH" in status["active_violations"]) == (expected_mode == "SAFE_MODE")


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-002")
@pytest.mark.parametrize("celsius, expected_mode", [(-19.99, "NOMINAL"), (-20.0, "NOMINAL"), (-20.01, "SAFE_MODE")])
def test_temperature_low_boundary(sim, celsius, expected_mode):
    status = _mode_after_frame(sim, temp_c=celsius)
    assert status["mode"] == expected_mode
    assert ("TEMPERATURE_LOW" in status["active_violations"]) == (expected_mode == "SAFE_MODE")


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-003")
@pytest.mark.parametrize("degrees, expected_mode", [(14.99, "NOMINAL"), (15.0, "NOMINAL"), (15.01, "SAFE_MODE")])
def test_attitude_boundary(sim, degrees, expected_mode):
    status = _mode_after_frame(sim, attitude_deg=degrees)
    assert status["mode"] == expected_mode
    assert ("ATTITUDE_EXCEEDED" in status["active_violations"]) == (expected_mode == "SAFE_MODE")


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-006")
@pytest.mark.parametrize(
    "field, rule, trip, between, clear",
    [
        ("battery_v", "BATTERY_LOW", 6.7, 6.99, 7.0),
        ("temp_c", "TEMPERATURE_HIGH", 61, 55.01, 55.0),
        ("temp_c", "TEMPERATURE_LOW", -21, -15.01, -15.0),
        ("attitude_deg", "ATTITUDE_EXCEEDED", 16, 10.01, 10.0),
    ],
)
def test_hysteresis_clears_only_at_recover_threshold(sim, field, rule, trip, between, clear):
    run = sim.run([
        *NOMINAL_BOOT,
        telemetry(2, **{field: trip}),
        telemetry(3, **{field: between}),
        "STATUS",
        telemetry(4, **{field: clear}),
        "STATUS",
    ])
    assert run.responses[2]["data"]["active_violations"] == [rule]
    assert run.responses[4]["data"]["active_violations"] == [rule], "must stay active inside the hysteresis band"
    assert run.final_status["active_violations"] == []
    assert [e["details"]["rule"] for e in run.events_named("SAFETY_CLEARED")] == [rule]


@pytest.mark.req("REQ-SAF-004")
def test_invalid_sensor_frame_raises_sensor_invalid_only(sim):
    # The battery value in an INVALID frame is garbage and must not be used.
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=1.0, sensors="INVALID"), "STATUS",
                   telemetry(3), "STATUS"])
    assert run.responses[3]["data"]["active_violations"] == ["SENSOR_INVALID"]
    assert run.responses[3]["data"]["mode"] == "SAFE_MODE"
    assert run.final_status["active_violations"] == []
    assert all(e["details"]["rule"] != "BATTERY_LOW" for e in run.events_named("SAFETY_VIOLATION"))


@pytest.mark.req("REQ-SAF-004")
def test_invalid_sensor_frame_cannot_clear_existing_violation(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=6.0), telemetry(3, battery_v=7.5, sensors="INVALID"),
                   "STATUS"])
    assert set(run.final_status["active_violations"]) == {"BATTERY_LOW", "SENSOR_INVALID"}


@pytest.mark.boundary
@pytest.mark.req("REQ-SAF-005")
@pytest.mark.parametrize("silence_s, expected_mode", [(60, "NOMINAL"), (61, "SAFE_MODE")])
def test_comm_timeout_boundary(sim, silence_s, expected_mode):
    # Telemetry stays fresh (a DOWN frame every 10 s) while the link is down.
    commands = [*NOMINAL_BOOT]
    seq = 2
    elapsed = 0
    while elapsed + 10 <= silence_s:
        commands += [telemetry(seq, comm="DOWN"), "TICK 10"]
        seq += 1
        elapsed += 10
    if silence_s > elapsed:
        commands += [telemetry(seq, comm="DOWN"), f"TICK {silence_s - elapsed}"]
    run = sim.run([*commands, "STATUS"])
    status = run.final_status
    assert status["sim_time_s"] == silence_s
    assert status["mode"] == expected_mode
    assert ("COMM_TIMEOUT" in status["active_violations"]) == (expected_mode == "SAFE_MODE")
    assert len(run.events_named("COMM_LINK_DOWN")) == 1
    assert status["counters"]["faults"] == 0


@pytest.mark.req("REQ-SAF-005")
def test_brief_comm_drop_is_only_a_warning(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, comm="DOWN"), "TICK 5", telemetry(3, comm="UP"), "STATUS"])
    status = run.final_status
    assert status["mode"] == "NOMINAL"
    assert status["comm_link"] == "UP"
    assert run.events_named("COMM_LINK_DOWN")[0]["severity"] == "WARNING"
    assert run.events_named("COMM_LINK_RESTORED")
    assert run.events_named("SAFETY_VIOLATION") == []


@pytest.mark.req("REQ-SAF-005", "REQ-REC-001")
def test_comm_timeout_recovers_when_link_returns(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, comm="DOWN"), "TICK 10", telemetry(3, comm="DOWN"), "TICK 51",
                   telemetry(4, comm="DOWN"), "STATUS", telemetry(5, comm="UP"), "EXIT_SAFE_MODE", "STATUS"])
    assert run.responses[7]["data"]["active_violations"] == ["COMM_TIMEOUT"]
    assert run.final_status["mode"] == "NOMINAL"


@pytest.mark.req("REQ-SAF-007")
def test_multiple_simultaneous_violations_are_all_reported(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=6.0, temp_c=70, attitude_deg=30), "STATUS"])
    status = run.final_status
    assert status["active_violations"] == ["BATTERY_LOW", "TEMPERATURE_HIGH", "ATTITUDE_EXCEEDED"]
    assert len(run.events_named("SAFETY_VIOLATION")) == 3
    assert status["last_safe_mode_reason"] == "BATTERY_LOW"
    assert len(run.events_named("MODE_CHANGE")) == 2, "one BOOT->NOMINAL and ONE NOMINAL->SAFE_MODE"


@pytest.mark.req("REQ-SAF-007", "REQ-MODE-003")
def test_every_violation_must_clear_before_recovery(sim):
    run = sim.run([
        *NOMINAL_BOOT,
        telemetry(2, battery_v=6.0, temp_c=70),
        telemetry(3, battery_v=7.2, temp_c=70),
        "EXIT_SAFE_MODE",
        telemetry(4, battery_v=7.2, temp_c=50),
        "EXIT_SAFE_MODE",
        "STATUS",
    ])
    assert run.error_code(4) == "ACTIVE_VIOLATIONS"
    assert run.responses[6]["status"] == "OK"
    assert run.final_status["mode"] == "NOMINAL"


@pytest.mark.req("REQ-CFG-001")
def test_custom_thresholds_from_config_are_applied(sim, make_config):
    config = make_config({"thresholds.battery_min_v": 7.5, "thresholds.battery_recover_v": 7.8})
    run = sim.run([telemetry(1, battery_v=7.6), "INIT", telemetry(2, battery_v=7.49), "STATUS"], config=config)
    assert run.responses[1]["mode"] == "NOMINAL"
    assert run.final_status["active_violations"] == ["BATTERY_LOW"]


@pytest.mark.req("REQ-CFG-001")
def test_custom_timeouts_from_config_are_applied(sim, make_config):
    config = make_config({"timing.telemetry_timeout_s": 3, "timing.max_tick_s": 5})
    run = sim.run([*NOMINAL_BOOT, "TICK 3", "STATUS", "TICK 1", "TICK 6", "STATUS"], config=config)
    assert run.responses[3]["data"]["mode"] == "NOMINAL"
    assert run.error_code(5) == "INVALID_ARGUMENT"
    assert run.final_status["active_violations"] == ["TELEMETRY_STALE"]
