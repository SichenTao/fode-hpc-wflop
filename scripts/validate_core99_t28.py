#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T28 independent published-AEP validator
Paper DOI: 10.5194/wes-10-1661-2025
Public source/data: WINDFLOWER v1.0.0, DOI 10.5281/zenodo.13946931.
Missing/conflicts/resolution/HPC/claim boundary:
hpc/t28_libtorch/include/core99/nguyen_t28.hpp.
Independence boundary: invokes the production binary but compares only against
the independently published paper Table 1 AEP; it is never called from the
production objective or optimizer.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()
    def evaluate(objective: str, forecasts: int) -> dict:
        completed = subprocess.run(
            [
                args.binary, "--data", args.data, "--backend", "cpu",
                "--objective", objective, "--year", "2023",
                "--evaluation-year", "2023", "--iterations", "0",
                "--forecasts", str(forecasts), "--workers", "20",
                "--seed", "0", "--reserve-limit", "50",
            ],
            text=True, capture_output=True, timeout=300,
        )
        if completed.returncode:
            raise RuntimeError(completed.stderr or completed.stdout)
        return json.loads(completed.stdout)

    payload = evaluate("aep", 1)
    aep_reference = 919.78
    relative_error = abs(payload["aep_gwh"] - aep_reference) / aep_reference
    if relative_error > 0.01:
        raise RuntimeError(f"T28 base-layout AEP error {relative_error}")
    jerm = evaluate("jerm", 10)
    jerm_reference = 69.6735e6
    jerm_error = abs(jerm["objective_value"] - jerm_reference) / jerm_reference
    if jerm_error > 0.03:
        raise RuntimeError(f"T28 base-layout JERM error {jerm_error}")
    print(
        "core99_t28_h5_pass "
        f"native_aep_gwh={payload['aep_gwh']:.12g} "
        f"paper_aep_gwh={aep_reference:.12g} "
        f"aep_relative_error={relative_error:.9g} "
        f"native_jerm_eur={jerm['objective_value']:.12g} "
        f"paper_jerm_eur={jerm_reference:.12g} "
        f"jerm_relative_error={jerm_error:.9g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
