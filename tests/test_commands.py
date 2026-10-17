"""Command processor: recognition, validation, permissions, robustness."""

import pytest

from simharness import EXIT_OK, NOMINAL_BOOT, telemetry

RESPONSE_KEYS = {"response_seq", "command", "status", "mode", "sim_time_s"}

# The documented permission table (docs/state-machine.md). This is the
# SPECIFICATION the implementation is checked against, written independently
# of the C++ command table.
PERMISSIONS = {
    "HELP": {"BOOT", "NOMINAL", "SAFE_MODE", "FAULT"},
    "STATUS": {"BOOT", "NOMINAL", "SAFE_MODE", "FAULT"},
    "GET_TELEMETRY": {"BOOT", "NOMINAL", "SAFE_MODE", "FAULT"},
    "TELEMETRY": {"BOOT", "NOMINAL", "SAFE_MODE"},
    "TICK": {"BOOT", "NOMINAL", "SAFE_MODE", "FAULT"},
    "INIT": {"BOOT"},
    "ENTER_SAFE_MODE": {"NOMINAL", "SAFE_MODE"},
    "EXIT_SAFE_MODE": {"SAFE_MODE"},
    "PAYLOAD": {"NOMINAL"},
    "RESET": {"NOMINAL", "SAFE_MODE", "FAULT"},
    "FAULT_INJECT": {"BOOT", "NOMINAL", "SAFE_MODE"},
}

# A syntactically valid invocation of each command.
SAMPLE = {
    "HELP": "HELP",
    "STATUS": "STATUS",
    "GET_TELEMETRY": "GET_TELEMETRY",
    "TELEMETRY": telemetry(100),
    "TICK": "TICK 1",
    "INIT": "INIT",
    "ENTER_SAFE_MODE": "ENTER_SAFE_MODE",
    "EXIT_SAFE_MODE": "EXIT_SAFE_MODE",
    "PAYLOAD": "PAYLOAD OFF",
    "RESET": "RESET CONFIRM",
    "FAULT_INJECT": "FAULT_INJECT INTERNAL_ERROR",
}

MODES = ["BOOT", "NOMINAL", "SAFE_MODE", "FAULT"]


@pytest.mark.req("REQ-CMD-001")
def test_every_command_line_gets_exactly_one_structured_response(sim):
    lines = ["STATUS", "", "   ", "# a comment", "HELP", "NOPE", telemetry(1), "INIT", "GET_TELEMETRY"]
    run = sim.run(lines)
    assert len(run.responses) == 6
    assert [r["response_seq"] for r in run.responses] == [1, 2, 3, 4, 5, 6]
    for response in run.responses:
        assert RESPONSE_KEYS <= set(response)
        assert response["status"] in {"OK", "REJECTED"}
        assert ("data" in response) == (response["status"] == "OK")
        assert ("error" in response) == (response["status"] == "REJECTED")


@pytest.mark.req("REQ-CMD-001", "REQ-CMD-004")
def test_help_documents_every_command_and_its_modes(sim):
    run = sim.run(["HELP"])
    commands = {c["name"]: c for c in run.last["data"]["commands"]}
    assert set(commands) == set(PERMISSIONS)
    for name, entry in commands.items():
        assert set(entry["permitted_modes"]) == PERMISSIONS[name], name
        assert entry["usage"].startswith(name)
        assert entry["description"]


@pytest.mark.req("REQ-CMD-004")
@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("command", sorted(PERMISSIONS))
def test_permission_matrix(sim, reach_mode, command, mode):
    setup = reach_mode(mode)
    run = sim.run([*setup, SAMPLE[command]])
    before_mode = run.responses[len(setup) - 1]["mode"] if setup else "BOOT"
    assert before_mode == mode, "precondition: setup did not reach the mode"
    response = run.last
    if mode in PERMISSIONS[command]:
        assert response.get("error", {}).get("code") != "NOT_PERMITTED_IN_MODE", response
    else:
        assert response["status"] == "REJECTED"
        assert response["error"]["code"] == "NOT_PERMITTED_IN_MODE"
        assert response["mode"] == mode, "a refused command must not change the mode"


@pytest.mark.negative
@pytest.mark.req("REQ-CMD-002")
@pytest.mark.parametrize("line", ["FOO", "STATUSS", "STAT", "rm -rf /", "$(reboot)", "'; DROP TABLE"])
def test_unknown_commands_are_rejected_and_logged(sim, line):
    run = sim.run([*NOMINAL_BOOT, line, "STATUS"])
    response = run.responses[2]
    assert response["status"] == "REJECTED"
    assert response["error"]["code"] == "UNKNOWN_COMMAND"
    assert response["command"] == line.split()[0]
    rejected = run.events_named("COMMAND_REJECTED")
    assert rejected[-1]["details"]["code"] == "UNKNOWN_COMMAND"
    assert run.final_status["mode"] == "NOMINAL"


INVALID_ARGUMENTS = [
    "TICK",
    "TICK 1 2",
    "TICK abc",
    "TICK 1.5",
    "TICK -1",
    "TICK 0",
    "TICK 3601",
    "TICK 99999999999999999999999",
    "PAYLOAD",
    "PAYLOAD MAYBE",
    "PAYLOAD ON OFF",
    "RESET",
    "RESET NOW",
    "STATUS now",
    "HELP me",
    "ENTER_SAFE_MODE please",
    "FAULT_INJECT",
    "FAULT_INJECT COSMIC_RAY",
]


@pytest.mark.negative
@pytest.mark.req("REQ-CMD-003")
@pytest.mark.parametrize("line", INVALID_ARGUMENTS)
def test_invalid_arguments_are_rejected_without_state_change(sim, line):
    run = sim.run([*NOMINAL_BOOT, "STATUS", line, "STATUS"])
    before = run.responses[2]["data"]
    response = run.responses[3]
    after = run.final_status
    assert response["status"] == "REJECTED"
    assert response["error"]["code"] == "INVALID_ARGUMENT"
    for key in ("mode", "sim_time_s", "payload", "active_violations", "telemetry_available"):
        assert after[key] == before[key], key
    assert after["counters"]["commands_rejected"] == before["counters"]["commands_rejected"] + 1


@pytest.mark.req("REQ-CMD-006")
@pytest.mark.parametrize("line", ["status", "Status", "sTaTuS", "  STATUS  ", "\tSTATUS"])
def test_command_names_are_case_insensitive_and_whitespace_tolerant(sim, line):
    run = sim.run([line])
    assert run.last["status"] == "OK"
    assert run.last["command"] == "STATUS"


@pytest.mark.req("REQ-CMD-006")
def test_blank_lines_and_comments_produce_no_response(sim):
    run = sim.run(["", "# INIT", "   # ENTER_SAFE_MODE", "\t", "STATUS"])
    assert len(run.responses) == 1
    assert run.final_status["mode"] == "BOOT"


@pytest.mark.req("REQ-CMD-006")
def test_crlf_line_endings_are_accepted(sim):
    run = sim.run(b"STATUS\r\nHELP\r\n")
    assert [r["status"] for r in run.responses] == ["OK", "OK"]


@pytest.mark.negative
@pytest.mark.req("REQ-CMD-005")
@pytest.mark.parametrize(
    "raw",
    [b"\x00STATUS", b"STA\x07TUS", b"STATUS\x1b[31m", b"\xff\xfe\xfd", "STATUS é".encode(), b"\x7fSTATUS"],
    ids=["nul", "bell", "ansi-escape", "invalid-utf8", "non-ascii", "del"],
)
def test_malformed_bytes_are_rejected_without_crash(sim, raw):
    run = sim.run(raw + b"\nSTATUS\n")
    assert run.exit_code == EXIT_OK
    assert run.responses[0]["error"]["code"] == "MALFORMED_INPUT"
    assert run.responses[0]["command"] is None
    assert run.final_status["mode"] == "BOOT"


@pytest.mark.boundary
@pytest.mark.req("REQ-CMD-005")
@pytest.mark.parametrize("length, expected", [(256, "OK"), (257, "MALFORMED_INPUT")])
def test_max_command_length_boundary(sim, length, expected):
    # "TICK 000...01" is a valid TICK 1 command of exactly `length` bytes.
    line = "TICK " + "0" * (length - 6) + "1"
    assert len(line) == length
    run = sim.run([line])
    actual = run.last["status"] if run.last["status"] == "OK" else run.last["error"]["code"]
    assert actual == expected


@pytest.mark.req("REQ-CMD-005")
def test_very_large_line_does_not_crash(sim):
    run = sim.run(b"A" * 1_000_000 + b"\nSTATUS\n")
    assert run.exit_code == EXIT_OK
    assert run.responses[0]["error"]["code"] == "MALFORMED_INPUT"
    assert run.last["status"] == "OK"


@pytest.mark.req("REQ-CMD-007")
def test_repeated_status_is_consistent(sim):
    run = sim.run([*NOMINAL_BOOT, "STATUS", "STATUS", "STATUS"])
    snapshots = [r["data"] for r in run.responses[2:]]
    for snapshot in snapshots:
        snapshot["counters"].pop("commands_accepted")
    assert snapshots[0] == snapshots[1] == snapshots[2]


@pytest.mark.req("REQ-CMD-007")
def test_enter_safe_mode_is_idempotent(sim):
    run = sim.run([*NOMINAL_BOOT, "ENTER_SAFE_MODE", "ENTER_SAFE_MODE", "ENTER_SAFE_MODE", "STATUS"])
    assert [r["data"]["changed"] for r in run.responses[2:5]] == [True, False, False]
    assert run.final_status["counters"]["safe_mode_entries"] == 1
    assert run.mode_changes() == [("BOOT", "NOMINAL"), ("NOMINAL", "SAFE_MODE")]
    assert len(run.events_named("SAFE_MODE_ALREADY_ACTIVE")) == 2


@pytest.mark.req("REQ-CMD-007", "REQ-CMD-001")
def test_long_command_stream(sim):
    run = sim.run(["STATUS"] * 5000)
    assert run.exit_code == EXIT_OK
    assert len(run.responses) == 5000
    assert run.responses[-1]["response_seq"] == 5000
