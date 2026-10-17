"""Meta-tests: prove that the verification framework can actually fail."""

import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from simharness import REPO_ROOT

FAILING_CHECK = Path(__file__).parent / "selfcheck" / "deliberately_failing_check.py"


def test_deliberately_failing_check_makes_pytest_fail(sim_binary, tmp_path):
    junit = tmp_path / "selfcheck.xml"
    env = dict(os.environ, CUBESAT_SIM_BIN=str(sim_binary))
    result = subprocess.run(
        [sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", str(FAILING_CHECK),
         f"--junitxml={junit}"],
        cwd=REPO_ROOT,
        env=env,
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    assert result.returncode == 1, f"expected pytest exit code 1 (tests failed)\n{result.stdout}\n{result.stderr}"
    assert "1 failed" in result.stdout
    assert "assert 'SAFE_MODE' == 'NOMINAL'" in result.stdout
    suite = ET.parse(junit).getroot()
    suite = suite if suite.tag == "testsuite" else suite.find("testsuite")
    assert suite.get("failures") == "1"
    assert suite.get("tests") == "1"
