"""Python verification harness for the CubeSat avionics simulator."""

from .build import REPO_ROOT, BuildError, locate_simulator
from .simulator import (
    EXIT_CONFIG_ERROR,
    EXIT_INPUT_ERROR,
    EXIT_INTERNAL_ERROR,
    EXIT_OK,
    EXIT_USAGE,
    NOMINAL_BOOT,
    ProtocolError,
    Simulator,
    SimRun,
    telemetry,
)

__all__ = [
    "REPO_ROOT",
    "BuildError",
    "locate_simulator",
    "EXIT_CONFIG_ERROR",
    "EXIT_INPUT_ERROR",
    "EXIT_INTERNAL_ERROR",
    "EXIT_OK",
    "EXIT_USAGE",
    "NOMINAL_BOOT",
    "ProtocolError",
    "Simulator",
    "SimRun",
    "telemetry",
]
