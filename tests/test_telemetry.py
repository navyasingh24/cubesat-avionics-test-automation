"""Telemetry ingestion and validation."""

import pytest

from simharness import NOMINAL_BOOT, telemetry

FIELDS = {
    "seq": "seq=2",
    "battery_v": "battery_v=7.5",
    "temp_c": "temp_c=20",
    "attitude_deg": "attitude_deg=1",
    "comm": "comm=UP",
    "sensors": "sensors=VALID",
}


@pytest.mark.req("REQ-TLM-001")
def test_valid_frame_is_stored_and_reported(sim):
    run = sim.run([telemetry(7, battery_v=7.25, temp_c=-3.5, attitude_deg=4.75, comm="DOWN"),
                   "GET_TELEMETRY", "TICK 3", "GET_TELEMETRY"])
    assert run.responses[0]["data"]["accepted_seq"] == 7
    frame = run.responses[1]["data"]["frame"]
    assert frame == {
        "seq": 7, "battery_v": 7.25, "temp_c": -3.5, "attitude_deg": 4.75,
        "comm": "DOWN", "sensors": "VALID", "received_at_s": 0,
    }
    assert run.responses[1]["data"]["age_s"] == 0
    assert run.responses[3]["data"]["age_s"] == 3


@pytest.mark.req("REQ-TLM-001")
def test_get_telemetry_without_frame_returns_null(sim):
    run = sim.run(["GET_TELEMETRY"])
    assert run.last["data"] == {"frame": None, "age_s": None}


@pytest.mark.negative
@pytest.mark.req("REQ-TLM-002")
@pytest.mark.parametrize("missing", sorted(FIELDS))
def test_frame_missing_a_field_is_rejected(sim, missing):
    line = "TELEMETRY " + " ".join(v for k, v in FIELDS.items() if k != missing)
    run = sim.run([*NOMINAL_BOOT, line, "STATUS"])
    response = run.responses[2]
    assert response["error"]["code"] == "MISSING_FIELD"
    assert missing in response["error"]["message"]
    counters = run.final_status["counters"]
    assert counters["frames_rejected"] == 1
    assert counters["frames_accepted"] == 1
    assert run.events_named("TELEMETRY_REJECTED")[0]["details"]["code"] == "MISSING_FIELD"


@pytest.mark.negative
@pytest.mark.req("REQ-TLM-002")
def test_empty_telemetry_command_is_rejected(sim):
    run = sim.run(["TELEMETRY"])
    assert run.error_code(0) == "MISSING_FIELD"


@pytest.mark.negative
@pytest.mark.req("REQ-TLM-003")
@pytest.mark.parametrize(
    "override, code",
    [
        ("battery_v=abc", "INVALID_ARGUMENT"),
        ("battery_v=nan", "INVALID_ARGUMENT"),
        ("battery_v=inf", "INVALID_ARGUMENT"),
        ("battery_v=-inf", "INVALID_ARGUMENT"),
        ("battery_v=1e999", "INVALID_ARGUMENT"),
        ("battery_v=0x7", "INVALID_ARGUMENT"),
        ("battery_v=7.4V", "INVALID_ARGUMENT"),
        ("battery_v=", "INVALID_ARGUMENT"),
        ("battery_v=7,4", "INVALID_ARGUMENT"),
        ("temp_c=--5", "INVALID_ARGUMENT"),
        ("attitude_deg=.5", "INVALID_ARGUMENT"),
        ("comm=up", "INVALID_ARGUMENT"),
        ("sensors=OK", "INVALID_ARGUMENT"),
        ("seq=1.5", "INVALID_ARGUMENT"),
        ("seq=-1", "INVALID_ARGUMENT"),
        ("seq=9007199254740992", "INVALID_ARGUMENT"),
        ("battery_v=-0.01", "OUT_OF_RANGE"),
        ("battery_v=20.01", "OUT_OF_RANGE"),
        ("temp_c=-150.1", "OUT_OF_RANGE"),
        ("temp_c=150.1", "OUT_OF_RANGE"),
        ("attitude_deg=-0.1", "OUT_OF_RANGE"),
        ("attitude_deg=180.1", "OUT_OF_RANGE"),
    ],
)
def test_invalid_values_are_rejected(sim, override, code):
    key = override.split("=")[0]
    parts = [override if part.startswith(key + "=") else part for part in FIELDS.values()]
    run = sim.run([*NOMINAL_BOOT, "TELEMETRY " + " ".join(parts), "STATUS"])
    assert run.error_code(2) == code
    status = run.final_status
    assert status["mode"] == "NOMINAL", "a rejected frame must not affect the state"
    assert status["counters"]["frames_rejected"] == 1


@pytest.mark.boundary
@pytest.mark.req("REQ-TLM-003")
@pytest.mark.parametrize(
    "field, value",
    [("battery_v", 0), ("battery_v", 20), ("temp_c", -150), ("temp_c", 150),
     ("attitude_deg", 0), ("attitude_deg", 180)],
)
def test_plausibility_limits_are_inclusive(sim, field, value):
    run = sim.run([telemetry(1, **{field: value})])
    assert run.last["status"] == "OK"


@pytest.mark.negative
@pytest.mark.req("REQ-TLM-003")
@pytest.mark.parametrize(
    "line",
    [
        telemetry(2) + " extra=1",
        telemetry(2) + " battery_v=7.0",
        "TELEMETRY seq=2 battery_v=7.5 temp_c=20 attitude_deg=1 comm=UP sensors",
        "TELEMETRY seq=2 =7.5 temp_c=20 attitude_deg=1 comm=UP sensors=VALID",
    ],
    ids=["unknown-field", "duplicate-field", "no-equals", "empty-key"],
)
def test_structurally_invalid_frames_are_rejected(sim, line):
    run = sim.run([*NOMINAL_BOOT, line])
    assert run.error_code(2) == "INVALID_ARGUMENT"


@pytest.mark.negative
@pytest.mark.req("REQ-TLM-004")
def test_sequence_numbers_must_strictly_increase(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(5), telemetry(5), telemetry(4), telemetry(0), telemetry(6), "STATUS"])
    assert [run.responses[i]["status"] for i in range(2, 7)] == ["OK", "REJECTED", "REJECTED", "REJECTED", "OK"]
    assert run.error_code(3) == "STALE_SEQUENCE"
    counters = run.final_status["counters"]
    assert counters["frames_accepted"] == 3
    assert counters["frames_rejected"] == 3


@pytest.mark.boundary
@pytest.mark.req("REQ-TLM-005")
@pytest.mark.parametrize("age, expected_mode", [(9, "NOMINAL"), (10, "NOMINAL"), (11, "SAFE_MODE")])
def test_telemetry_stale_boundary(sim, age, expected_mode):
    run = sim.run([*NOMINAL_BOOT, f"TICK {age}", "STATUS"])
    status = run.final_status
    assert status["mode"] == expected_mode
    assert ("TELEMETRY_STALE" in status["active_violations"]) == (expected_mode == "SAFE_MODE")


@pytest.mark.req("REQ-TLM-005")
def test_fresh_frame_clears_stale_violation(sim):
    run = sim.run([*NOMINAL_BOOT, "TICK 11", telemetry(2), "STATUS"])
    status = run.final_status
    assert status["active_violations"] == []
    assert status["mode"] == "SAFE_MODE", "recovery still needs EXIT_SAFE_MODE"
    cleared = run.events_named("SAFETY_CLEARED")
    assert cleared and cleared[0]["details"]["rule"] == "TELEMETRY_STALE"
