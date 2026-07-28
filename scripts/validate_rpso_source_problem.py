#!/usr/bin/env python3
"""Validate the RPSO source problem independently of the blocked RL method."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from validate_bde_source_replay import evaluate


def run(
    binary: Path,
    cases: Path,
    case_id: str,
    workers: int,
    output: Path,
) -> dict[str, object]:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            "cgpso",
            "--problem",
            "rpso2024_source_problem_ws1_ws4",
            "--cases",
            str(cases),
            "--case",
            case_id,
            "--seed",
            "20260729",
            "--physical-fes",
            "100",
            "--workers",
            str(workers),
            "--output",
            str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=4)
    arguments = parser.parse_args()
    manifest = json.loads(arguments.cases.read_text())
    cases_by_id = {case["case_id"]: case for case in manifest["cases"]}
    case_ids = ["RPSO-WS1-tn30", "RPSO-WS4-tn40"]

    with tempfile.TemporaryDirectory(prefix="rpso-source-problem-") as temporary:
        temporary_root = Path(temporary)
        for case_id in case_ids:
            serial = run(
                arguments.binary,
                arguments.cases,
                case_id,
                1,
                temporary_root / f"{case_id}-serial.json",
            )
            parallel = run(
                arguments.binary,
                arguments.cases,
                case_id,
                arguments.workers,
                temporary_root / f"{case_id}-parallel.json",
            )
            for field in (
                "effective_semantics_id",
                "problem_id",
                "problem_semantics_id",
                "physical_fes",
                "best_expected_power_kw",
                "best_layout_1based",
            ):
                if serial[field] != parallel[field]:
                    raise RuntimeError(
                        f"{case_id}: serial/parallel mismatch in {field}"
                    )
            if serial["problem_semantics_id"] != (
                "rpso2024_source_problem_ws1_ws4_v1"
            ):
                raise RuntimeError(f"{case_id}: wrong problem semantics")
            oracle = evaluate(
                cases_by_id[case_id],
                [int(value) for value in serial["best_layout_1based"]],
            )
            cpp = float(serial["best_expected_power_kw"])
            tolerance = 1.0e-9 * max(1.0, abs(oracle))
            if abs(cpp - oracle) > tolerance:
                raise RuntimeError(
                    f"{case_id}: C++ {cpp} differs from oracle {oracle}"
                )
    print(
        "rpso_source_problem_validation_pass "
        f"cases={len(case_ids)} workers=1,{arguments.workers} "
        "probe_algorithm=cgpso physical_fes_per_run=100"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
