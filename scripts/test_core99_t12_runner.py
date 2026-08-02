#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 formal-runner scenario-boundary regression test.
Paper/DOI: Wilson et al.; 10.1016/j.renene.2018.03.052.
Purpose: prove that the campaign's one-based paper scenario identifiers are
passed unchanged to the C++ executable and recorded unchanged in receipts.
Claim boundary: runner integration test; no optimization-quality claim.
Last evidence-audit date: 2026-08-02
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
from argparse import Namespace
import json
from pathlib import Path
from tempfile import TemporaryDirectory

from run_core99_t12_admission import SCENARIOS, run_role


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    if SCENARIOS != (1, 2, 3, 4, 5):
        raise RuntimeError("T12 campaign scenarios are not one-based")

    with TemporaryDirectory(prefix="core99-t12-runner-") as directory:
        role_arguments = Namespace(
            binary=binary,
            seed=20260731,
            workers=4,
            physical_fes=1,
            source_commit="t12-runner-one-based-test",
        )
        row, reused = run_role(
            role_arguments,
            Path(directory),
            "t12_goldman_lattice",
            1,
        )

    if reused:
        raise RuntimeError("T12 integration test unexpectedly reused output")
    if row["problem_id"] != "t12_windflo_s1":
        raise RuntimeError("T12 runner changed the problem scenario")
    if row["campaign_scenario"] != 1:
        raise RuntimeError("T12 runner changed the campaign scenario")
    print(json.dumps({
        "status": "pass",
        "scenario": row["campaign_scenario"],
        "problem_id": row["problem_id"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
