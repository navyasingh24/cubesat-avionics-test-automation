"""Command-line interface contract: options, exit codes, streams, log format."""

import json
import random
import re
from pathlib import Path

import pytest

from simharness import (
    EXIT_CONFIG_ERROR,
    EXIT_INPUT_ERROR,
    EXIT_OK,
    EXIT_USAGE,
    NOMINAL_BOOT,
    REPO_ROOT,
    telemetry,
)

LOG_KEYS = {"log_seq", "sim_time_s", "severity", "component", "event", "mode", "message", "details"}
SEVERITIES = {"INFO", "WARNING", "ERROR", "CRITICAL"}
MODES = {"BOOT", "NOMINAL", "SAFE_MODE", "FAULT"}


def _project_version() -> str:
    cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    return re.search(r"project\([^)]*VERSION\s+([0-9.]+)", cmake, re.S).group(1)


@pytest.mark.req("REQ-CLI-001")
def test_help_exits_zero_and_documents_usage(sim):
    result = sim.run_raw(["--help"])
    assert result.returncode == EXIT_OK
    text = result.stdout.decode()
    assert "Usage:" in text and "--config" in text and "Exit codes" in text
    assert "NOT flight software" in text


@pytest.mark.req("REQ-CLI-001")
def test_version_matches_cmake_project_version(sim):
    result = sim.run_raw(["--version"])
    assert result.returncode == EXIT_OK
    assert result.stdout.decode().strip() == f"cubesat_sim {_project_version()}"


@pytest.mark.negative
@pytest.mark.req("REQ-CLI-001")
@pytest.mark.parametrize(
    "args, message",
    [
        ([], "--config <file> is required"),
        (["--bogus"], "unknown option '--bogus'"),
        (["--config"], "--config requires a file path"),
        (["--script"], "--script requires"),
        (["--config", "a.json", "--validate-config", "b.json"], "cannot be combined"),
        (["config.json"], "unknown option 'config.json'"),
    ],
)
def test_usage_errors_exit_2(sim, args, message):
    result = sim.run_raw(args)
    assert result.returncode == EXIT_USAGE
    assert message in result.stderr.decode()
    assert result.stdout == b"", "usage errors must not write to stdout"


@pytest.mark.req("REQ-CLI-001", "REQ-CFG-001")
def test_validate_config_reports_loaded_values(sim, default_config):
    result = sim.run_raw(["--validate-config", str(REPO_ROOT / "config" / "simulation_config.json")])
    assert result.returncode == EXIT_OK
    response = json.loads(result.stdout)
    assert response["status"] == "OK"
    assert response["config"] == default_config


CONFIG_DEFECTS = [
    ({"raw": ""}, "empty file"),
    ({"raw": "not json"}, "not JSON"),
    ({"raw": "[1, 2, 3]"}, "array root"),
    ({"raw": '{"schema_version": 1, "schema_version": 1}'}, "duplicate key"),
    ({"patch": {"thresholds.battery_min_v": "__DELETE__"}}, "missing key"),
    ({"patch": {"thresholds.battery_max_v": 9.0}}, "unknown key"),
    ({"patch": {"surprise": True}}, "unknown top-level key"),
    ({"patch": {"thresholds.temp_max_c": "60"}}, "string instead of number"),
    ({"patch": {"thresholds.temp_max_c": None}}, "null instead of number"),
    ({"patch": {"timing.comm_timeout_s": 0}}, "zero timeout"),
    ({"patch": {"timing.comm_timeout_s": -5}}, "negative timeout"),
    ({"patch": {"timing.telemetry_timeout_s": 2.5}}, "fractional timeout"),
    ({"patch": {"timing.max_tick_s": 86401}}, "timeout above one day"),
    ({"patch": {"schema_version": 2}}, "unsupported schema"),
    ({"patch": {"thresholds.battery_recover_v": 6.0}}, "recover below trip"),
    ({"patch": {"thresholds.temp_max_recover_c": 65.0}}, "temp recover above max"),
    ({"patch": {"thresholds.temp_min_recover_c": -25.0}}, "temp recover below min"),
    ({"patch": {"thresholds.temp_min_recover_c": 56.0, "thresholds.temp_max_recover_c": 55.0}}, "empty temp band"),
    ({"patch": {"thresholds.attitude_recover_deg": 20.0}}, "attitude recover above max"),
    ({"patch": {"thresholds.battery_min_v": 25.0, "thresholds.battery_recover_v": 26.0}}, "implausible battery"),
    ({"patch": {"thresholds.attitude_max_deg": 200.0}}, "implausible attitude"),
    ({"patch": {"limits.max_command_length": 10}}, "command length too small"),
    ({"patch": {"limits.max_command_length": 1000000}}, "command length too large"),
    ({"patch": {"thresholds": []}}, "thresholds not an object"),
]


@pytest.mark.negative
@pytest.mark.req("REQ-CFG-002")
@pytest.mark.parametrize("defect, description", CONFIG_DEFECTS, ids=[d for _, d in CONFIG_DEFECTS])
def test_invalid_configuration_is_rejected(sim, make_config, defect, description):
    config = make_config(**defect)
    run = sim.run(["STATUS"], config=config)
    assert run.exit_code == EXIT_CONFIG_ERROR, description
    assert len(run.responses) == 1, "only the error object, no command responses"
    assert run.responses[0]["status"] == "ERROR"
    assert run.responses[0]["error"]["code"] == "CONFIG_INVALID"
    assert run.responses[0]["error"]["message"]
    validate = sim.run_raw(["--validate-config", str(config)])
    assert validate.returncode == EXIT_CONFIG_ERROR


@pytest.mark.negative
@pytest.mark.req("REQ-CFG-002")
def test_missing_config_file(sim, tmp_path):
    run = sim.run(["STATUS"], config=tmp_path / "nope.json")
    assert run.exit_code == EXIT_CONFIG_ERROR
    assert run.responses[0]["error"]["code"] == "CONFIG_NOT_FOUND"


@pytest.mark.negative
@pytest.mark.req("REQ-CFG-002")
def test_config_path_is_a_directory(sim, tmp_path):
    run = sim.run(["STATUS"], config=tmp_path)
    assert run.exit_code == EXIT_CONFIG_ERROR


@pytest.mark.req("REQ-CLI-001")
def test_script_file_gives_same_result_as_stdin(sim, tmp_path):
    commands = [*NOMINAL_BOOT, telemetry(2, battery_v=6.1), "STATUS"]
    script = tmp_path / "script.txt"
    script.write_text("\n".join(commands) + "\n", encoding="utf-8")
    from_file = sim.run([], extra_args=["--script", str(script)])
    from_stdin = sim.run(commands)
    assert from_file.stdout == from_stdin.stdout
    assert from_file.stderr == from_stdin.stderr


@pytest.mark.negative
@pytest.mark.req("REQ-CLI-003")
def test_missing_script_file_exits_4(sim, tmp_path):
    run = sim.run([], extra_args=["--script", str(tmp_path / "missing.txt")])
    assert run.exit_code == EXIT_INPUT_ERROR
    assert run.responses[0]["error"]["code"] == "INPUT_NOT_FOUND"


@pytest.mark.negative
@pytest.mark.req("REQ-CLI-003")
def test_unwritable_log_file_exits_4(sim, tmp_path):
    run = sim.run(["STATUS"], extra_args=["--log-file", str(tmp_path / "no" / "such" / "dir" / "log.jsonl")])
    assert run.exit_code == EXIT_INPUT_ERROR
    assert run.responses[0]["error"]["code"] == "LOG_FILE_ERROR"


@pytest.mark.req("REQ-CLI-002")
def test_stdout_contains_only_responses_and_stderr_only_logs(sim):
    run = sim.run([*NOMINAL_BOOT, "BAD", telemetry(1), "EXIT_SAFE_MODE", "STATUS"])
    for line in run.stdout.splitlines():
        record = json.loads(line)
        assert "response_seq" in record and "log_seq" not in record
    for line in run.stderr.splitlines():
        record = json.loads(line)
        assert "log_seq" in record and "response_seq" not in record


@pytest.mark.req("REQ-CLI-002", "REQ-LOG-001")
def test_log_file_option_moves_logs_out_of_stderr(sim, tmp_path):
    log_path = tmp_path / "sim.log.jsonl"
    run = sim.run([*NOMINAL_BOOT, "STATUS"], extra_args=["--log-file", str(log_path)])
    assert run.exit_code == EXIT_OK
    assert run.stderr == ""
    entries = [json.loads(line) for line in log_path.read_text(encoding="utf-8").splitlines()]
    assert entries[0]["event"] == "POWER_ON"
    assert entries[-1]["event"] == "SESSION_END"


@pytest.mark.req("REQ-LOG-001")
def test_every_log_entry_has_required_fields(sim):
    run = sim.run([*NOMINAL_BOOT, "PAYLOAD ON", telemetry(2, battery_v=6.0), "BAD", "TICK 20",
                   telemetry(3, sensors="INVALID"), "FAULT_INJECT INTERNAL_ERROR", "RESET CONFIRM"])
    assert run.events, "no log entries"
    previous_time = 0
    for expected_seq, entry in enumerate(run.events, start=1):
        assert set(entry) == LOG_KEYS, entry
        assert entry["log_seq"] == expected_seq
        assert entry["severity"] in SEVERITIES
        assert entry["mode"] in MODES
        assert isinstance(entry["details"], dict)
        assert entry["component"] and entry["event"] and entry["message"]
        assert entry["sim_time_s"] >= previous_time
        previous_time = entry["sim_time_s"]
    names = set(run.event_names())
    for required in ("POWER_ON", "MODE_CHANGE", "COMMAND_ACCEPTED", "COMMAND_REJECTED", "SAFETY_VIOLATION",
                     "FAULT_INJECTED", "RESET_REQUESTED", "SESSION_END"):
        assert required in names, required


@pytest.mark.req("REQ-LOG-001")
def test_log_entries_never_contain_host_paths_or_wall_clock(sim):
    run = sim.run([*NOMINAL_BOOT, "STATUS"])
    assert str(Path.home()) not in run.stderr
    assert not re.search(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}", run.stderr), "logs must use simulated time only"


@pytest.mark.req("REQ-CLI-001")
def test_empty_input_is_a_valid_empty_session(sim):
    run = sim.run([])
    assert run.exit_code == EXIT_OK
    assert run.responses == []
    assert run.event_names() == ["POWER_ON", "SESSION_END"]


@pytest.mark.negative
@pytest.mark.req("REQ-CMD-005", "REQ-CLI-002")
@pytest.mark.parametrize("seed", [11, 22, 33])
def test_random_binary_input_never_crashes(sim, seed):
    rng = random.Random(seed)
    garbage = bytes(rng.randrange(256) for _ in range(20000))
    run = sim.run(garbage)
    assert run.exit_code == EXIT_OK
    for response in run.responses:
        assert response["status"] in {"OK", "REJECTED"}
