#!/usr/bin/env python3
"""Pretty-print simulator JSON responses as a readable table.

Usage:
  responses:    ./build/cubesat_sim --config config/simulation_config.json --script my.txt 2>/dev/null \
                    | python3 tools/format_session.py
  mode changes: python3 tools/format_session.py --mode-changes < reports/demo_mission.log.jsonl
"""

from __future__ import annotations

import json
import sys


def summarize(response: dict) -> str:
    if response.get("status") == "REJECTED":
        return f"{response['error']['code']}: {response['error']['message']}"
    data = response.get("data", {})
    if "active_violations" in data and response["command"] in ("TELEMETRY", "TICK", "INIT"):
        violations = ",".join(data["active_violations"]) or "none"
        return f"violations={violations}"
    if response["command"] == "STATUS":
        return (f"payload={data['payload']} violations={','.join(data['active_violations']) or 'none'} "
                f"safe_mode_reason={data['last_safe_mode_reason']}")
    if response["command"] == "HELP":
        return f"{len(data['commands'])} commands"
    return json.dumps(data)[:70]


def print_mode_changes() -> int:
    for line in sys.stdin:
        if not line.strip():
            continue
        entry = json.loads(line)
        if entry.get("event") != "MODE_CHANGE":
            continue
        d = entry["details"]
        print(f"  t={entry['sim_time_s']:>4}s  {d['from']:>9} -> {d['to']:<9} ({d['reason']})")
    return 0


def main() -> int:
    if sys.argv[1:] == ["--mode-changes"]:
        return print_mode_changes()
    print(f"{'#':>3}  {'COMMAND':<15} {'RESULT':<9} {'MODE':<10} {'T(s)':>5}  DETAIL")
    print("-" * 100)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        response = json.loads(line)
        if "response_seq" not in response:
            print(f"ERROR: {response.get('error', response)}")
            continue
        print(f"{response['response_seq']:>3}  {str(response['command']):<15} {response['status']:<9} "
              f"{response['mode']:<10} {response['sim_time_s']:>5}  {summarize(response)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
