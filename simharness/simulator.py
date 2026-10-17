"""Thin, strict wrapper around the compiled ``cubesat_sim`` executable.

The harness never re-implements flight-computer logic. It only:
  * starts the real C++ binary with ``subprocess`` (argument lists, never a
    shell, so command text cannot be interpreted by a shell),
  * feeds it a command script on stdin, and
  * parses stdout (one JSON response per command) and the JSON-lines log.

All commands of one ``run()`` call go to ONE process, so state (mode,
telemetry, clock) persists across them. Each ``run()`` starts a fresh process.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Union

DEFAULT_TIMEOUT_S = 20

# Exit codes documented in include/cubesat/cli.hpp and the README.
EXIT_OK = 0
EXIT_USAGE = 2
EXIT_CONFIG_ERROR = 3
EXIT_INPUT_ERROR = 4
EXIT_INTERNAL_ERROR = 5


class ProtocolError(AssertionError):
    """The simulator produced output that violates the documented contract."""


def telemetry(
    seq: int,
    battery_v: Any = 7.6,
    temp_c: Any = 22.5,
    attitude_deg: Any = 2.0,
    comm: str = "UP",
    sensors: str = "VALID",
) -> str:
    """Build a TELEMETRY command line. Defaults are a nominal frame."""
    return (
        f"TELEMETRY seq={seq} battery_v={battery_v} temp_c={temp_c} "
        f"attitude_deg={attitude_deg} comm={comm} sensors={sensors}"
    )


# Commands that take a fresh simulator from BOOT to NOMINAL.
NOMINAL_BOOT: List[str] = [telemetry(1), "INIT"]


@dataclass
class SimRun:
    """Everything observed from one simulator process."""

    args: List[str]
    exit_code: int
    stdout: str
    stderr: str
    responses: List[Dict[str, Any]]
    events: List[Dict[str, Any]]
    commands: List[str] = field(default_factory=list)

    # -- responses ---------------------------------------------------------
    @property
    def last(self) -> Dict[str, Any]:
        if not self.responses:
            raise ProtocolError("simulator produced no responses")
        return self.responses[-1]

    @property
    def final_mode(self) -> str:
        return self.last["mode"]

    @property
    def final_status(self) -> Dict[str, Any]:
        """Data of the last response, which must be a successful STATUS."""
        last = self.last
        if last.get("command") != "STATUS" or last.get("status") != "OK":
            raise ProtocolError(f"last response is not a successful STATUS: {last}")
        return last["data"]

    def error_code(self, index: int) -> Optional[str]:
        response = self.responses[index]
        return response["error"]["code"] if response["status"] == "REJECTED" else None

    # -- diagnostic log ----------------------------------------------------
    def event_names(self) -> List[str]:
        return [e["event"] for e in self.events]

    def events_named(self, name: str) -> List[Dict[str, Any]]:
        return [e for e in self.events if e["event"] == name]

    def events_after_command(self, command_count: int) -> List[Dict[str, Any]]:
        """Log entries emitted after the first ``command_count`` responses.

        Every processed command ends with exactly one COMMAND_ACCEPTED or
        COMMAND_REJECTED entry, which lets us split the log by command.
        """
        if command_count <= 0:
            return list(self.events)
        seen = 0
        for index, entry in enumerate(self.events):
            if entry["event"] in ("COMMAND_ACCEPTED", "COMMAND_REJECTED"):
                seen += 1
                if seen == command_count:
                    return self.events[index + 1 :]
        return []

    def mode_changes(self) -> List[tuple]:
        return [(e["details"]["from"], e["details"]["to"]) for e in self.events_named("MODE_CHANGE")]


def _parse_json_lines(text: str, stream: str) -> List[Dict[str, Any]]:
    records = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ProtocolError(f"{stream} line {number} is not valid JSON: {line!r} ({exc})") from exc
        if not isinstance(record, dict):
            raise ProtocolError(f"{stream} line {number} is not a JSON object: {line!r}")
        records.append(record)
    return records


class Simulator:
    """Runs the compiled simulator binary."""

    def __init__(self, binary: Path, default_config: Path, timeout_s: int = DEFAULT_TIMEOUT_S):
        self.binary = Path(binary)
        self.default_config = Path(default_config)
        self.timeout_s = timeout_s

    def run_raw(
        self, args: Sequence[str], stdin: Union[bytes, str] = b""
    ) -> subprocess.CompletedProcess:
        """Run the binary with arbitrary arguments; no output parsing."""
        data = stdin.encode("utf-8") if isinstance(stdin, str) else stdin
        return subprocess.run(
            [str(self.binary), *args],
            input=data,
            capture_output=True,
            timeout=self.timeout_s,
            check=False,
        )

    def run(
        self,
        commands: Union[Iterable[str], str, bytes],
        config: Optional[Path] = None,
        extra_args: Sequence[str] = (),
    ) -> SimRun:
        """Send a command script to ONE simulator process and parse results.

        ``commands`` may be a list of lines, a pre-joined string, or raw bytes
        (for malformed-input tests). Logs are captured from stderr.
        """
        if isinstance(commands, (str, bytes)):
            payload = commands
            command_list: List[str] = []
        else:
            command_list = list(commands)
            payload = "".join(line + "\n" for line in command_list)

        args = ["--config", str(config or self.default_config), *extra_args]
        completed = self.run_raw(args, payload)
        stdout = completed.stdout.decode("utf-8", errors="replace")
        stderr = completed.stderr.decode("utf-8", errors="replace")
        if completed.returncode < 0:
            raise ProtocolError(
                f"simulator was killed by signal {-completed.returncode} (crash)\n{stderr}"
            )
        responses = _parse_json_lines(stdout, "stdout")
        events = (
            _parse_json_lines(stderr, "stderr")
            if completed.returncode == EXIT_OK and "--log-file" not in extra_args
            else []
        )
        return SimRun(
            args=args,
            exit_code=completed.returncode,
            stdout=stdout,
            stderr=stderr,
            responses=responses,
            events=events,
            commands=command_list,
        )
