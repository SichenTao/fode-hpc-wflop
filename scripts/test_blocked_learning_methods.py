#!/usr/bin/env python3
"""Ensure unavailable learned methods fail with explicit evidence boundaries."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


EXPECTED = {
    "alga": ("R1/R2", "attention training"),
    "taae": ("R1/R2", "Transformer checkpoint"),
    "rlpso": ("R2", "frozen policy"),
    "rlfode": ("R2", "pretrained Q-tables"),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    arguments = parser.parse_args()

    for algorithm, required in EXPECTED.items():
        completed = subprocess.run(
            [
                str(arguments.binary),
                "--algorithm",
                algorithm,
                "--case",
                "WS1tn10",
                "--physical-fes",
                "30",
                "--workers",
                "1",
                "--cases",
                str(arguments.cases),
            ],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if completed.returncode == 0:
            raise RuntimeError(
                f"{algorithm} executed despite its frozen missing assets"
            )
        for phrase in required:
            if phrase not in completed.stderr:
                raise RuntimeError(
                    f"{algorithm} rejection omitted evidence phrase {phrase!r}"
                )
    print(
        "blocked_learning_method_guard_pass "
        "identifiers=alga,taae,rlpso,rlfode"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
