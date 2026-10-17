"""Shared pytest fixtures for the system-level verification suite.

Every test drives the REAL compiled ``cubesat_sim`` executable through
``simharness.Simulator``. The binary is located (CUBESAT_SIM_BIN) or built
with CMake once per test session; see simharness/build.py.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable, Dict, List

import pytest

from simharness import NOMINAL_BOOT, REPO_ROOT, Simulator, locate_simulator
from simharness.requirements import load_requirements

DEFAULT_CONFIG = REPO_ROOT / "config" / "simulation_config.json"

# Commands that bring a fresh simulator into each mode. Tests verify the
# resulting mode before relying on it (see reach_mode fixture).
MODE_SETUP: Dict[str, List[str]] = {
    "BOOT": [],
    "NOMINAL": list(NOMINAL_BOOT),
    "SAFE_MODE": [*NOMINAL_BOOT, "ENTER_SAFE_MODE"],
    "FAULT": [*NOMINAL_BOOT, "FAULT_INJECT INTERNAL_ERROR"],
}


def pytest_collection_modifyitems(config: pytest.Config, items: List[pytest.Item]) -> None:
    """Reject requirement IDs that are not in the catalogue (typo protection)."""
    known = set(load_requirements())
    for item in items:
        for marker in item.iter_markers("req"):
            unknown = set(marker.args) - known
            if unknown:
                raise pytest.UsageError(f"{item.nodeid}: unknown requirement IDs {sorted(unknown)}")


@pytest.fixture(autouse=True)
def _record_requirements(request: pytest.FixtureRequest, record_property: Callable) -> None:
    ids = sorted({rid for marker in request.node.iter_markers("req") for rid in marker.args})
    if ids:
        record_property("requirements", ",".join(ids))


@pytest.fixture(scope="session")
def sim_binary() -> Path:
    return locate_simulator()


@pytest.fixture(scope="session")
def sim(sim_binary: Path) -> Simulator:
    return Simulator(sim_binary, DEFAULT_CONFIG)


@pytest.fixture(scope="session")
def default_config() -> Dict[str, Any]:
    with open(DEFAULT_CONFIG, encoding="utf-8") as handle:
        return json.load(handle)


@pytest.fixture
def make_config(tmp_path: Path, default_config: Dict[str, Any]) -> Callable[..., Path]:
    """Write a patched copy of the default config into an isolated tmp dir.

    Usage: make_config({"thresholds.battery_min_v": 7.5}) or
           make_config(raw='{"not": "valid"')
    """
    counter = {"n": 0}

    def factory(patch: Dict[str, Any] = None, raw: str = None) -> Path:
        counter["n"] += 1
        path = tmp_path / f"config_{counter['n']}.json"
        if raw is not None:
            path.write_text(raw, encoding="utf-8")
            return path
        config = json.loads(json.dumps(default_config))
        for dotted, value in (patch or {}).items():
            node = config
            *parents, leaf = dotted.split(".")
            for key in parents:
                node = node[key]
            if value == "__DELETE__":
                del node[leaf]
            else:
                node[leaf] = value
        path.write_text(json.dumps(config), encoding="utf-8")
        return path

    return factory


@pytest.fixture
def reach_mode() -> Callable[[str], List[str]]:
    """Return setup commands for a mode (callers assert the mode reached)."""

    def factory(mode: str) -> List[str]:
        return list(MODE_SETUP[mode])

    return factory

