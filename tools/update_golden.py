#!/usr/bin/env python3
"""Regenerate golden regression outputs from tests/golden/*.script.

Only run this after an INTENTIONAL behaviour change, then review the diff
(`git diff tests/golden`) before committing: the golden files are the
reference that tests/test_regression.py compares against.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from simharness import REPO_ROOT, locate_simulator  # noqa: E402
from simharness.simulator import Simulator  # noqa: E402

GOLDEN_DIR = REPO_ROOT / "tests" / "golden"


def main() -> int:
    sim = Simulator(locate_simulator(), REPO_ROOT / "config" / "simulation_config.json")
    for script in sorted(GOLDEN_DIR.glob("*.script")):
        run = sim.run(script.read_bytes())
        if run.exit_code != 0:
            print(f"ERROR: {script.name} exited with {run.exit_code}", file=sys.stderr)
            return 1
        script.with_suffix(".stdout.jsonl").write_text(run.stdout, encoding="utf-8")
        script.with_suffix(".log.jsonl").write_text(run.stderr, encoding="utf-8")
        print(f"updated {script.stem}: {len(run.responses)} responses, {len(run.events)} log entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
