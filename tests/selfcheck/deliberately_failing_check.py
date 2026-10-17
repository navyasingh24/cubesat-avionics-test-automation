"""DELIBERATELY FAILING CHECK - never part of the normal suite.

This file's name does not match ``test_*.py``, so ordinary ``pytest`` runs
never collect it. tests/test_selfcheck.py runs it explicitly in a separate
pytest process to prove that a wrong expectation makes pytest exit non-zero
and record the failure in JUnit XML (i.e. failures cannot be silently lost).
"""

from simharness import NOMINAL_BOOT, telemetry


def test_wrong_expectation_low_battery_stays_nominal(sim):
    run = sim.run([*NOMINAL_BOOT, telemetry(2, battery_v=6.0), "STATUS"])
    # WRONG on purpose: the simulator correctly enters SAFE_MODE.
    assert run.final_status["mode"] == "NOMINAL"
