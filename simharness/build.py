"""Locate or build the simulator executable for the test harness.

Order of precedence:
  1. ``CUBESAT_SIM_BIN`` environment variable (used by CI after its explicit
     build stage, and by the seeded-defect demo to test a different build).
  2. Otherwise configure + build with CMake into ``<repo>/build``. CMake
     builds are incremental, so this always tests the CURRENT sources and
     never silently reuses a stale binary.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BINARY_NAME = "cubesat_sim"


class BuildError(RuntimeError):
    pass


def _run(cmd: list, cwd: Path) -> None:
    completed = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise BuildError(
            f"command failed ({completed.returncode}): {' '.join(cmd)}\n"
            f"--- stdout ---\n{completed.stdout}\n--- stderr ---\n{completed.stderr}"
        )


def build_simulator(build_dir: Path = REPO_ROOT / "build") -> Path:
    if shutil.which("cmake") is None:
        raise BuildError("cmake not found on PATH; see README 'Installation'")
    _run(["cmake", "-S", str(REPO_ROOT), "-B", str(build_dir)], REPO_ROOT)
    _run(["cmake", "--build", str(build_dir), "--target", BINARY_NAME], REPO_ROOT)
    binary = build_dir / BINARY_NAME
    if not binary.is_file():
        raise BuildError(f"build finished but {binary} does not exist")
    return binary


def locate_simulator() -> Path:
    override = os.environ.get("CUBESAT_SIM_BIN")
    if override:
        binary = Path(override).resolve()
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise BuildError(f"CUBESAT_SIM_BIN={override} is not an executable file")
        return binary
    return build_simulator()
