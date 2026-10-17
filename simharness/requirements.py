"""Loads the requirement catalogue used for test traceability."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict

from .build import REPO_ROOT

REQUIREMENTS_FILE = REPO_ROOT / "tests" / "requirements.json"


def load_requirements(path: Path = REQUIREMENTS_FILE) -> Dict[str, Dict[str, str]]:
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)["requirements"]
