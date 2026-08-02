#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0341 H5 semantic and numerical validator
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/tao_l0341.hpp
Controlling contract:
shared/contracts/core99_l0341_tao_3d_mdpso_2020.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_CASES = [
    f"l0341_{design}_{farm}_{wind}"
    for design in ("uniform", "nonuniform")
    for farm, winds in (
        ("wfa", ("a", "b", "c")),
        ("wfb", ("c",)),
        ("wfc", ("c",)),
    )
    for wind in winds
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        text=True,
        capture_output=True,
        check=True,
    )
    return json.loads(completed.stdout)


def relative_error(observed: float, target: float) -> float:
    return abs(observed - target) / target


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    cases = call(args.binary, ["--mode", "list-cases"])["paper_case_ids"]
    require(cases == EXPECTED_CASES, "L0341 paper case registry mismatch")
    uniform = call(
        args.binary,
        ["--mode", "inspect", "--case", "l0341_uniform_wfa_c"],
    )
    nonuniform = call(
        args.binary,
        ["--mode", "inspect", "--case", "l0341_nonuniform_wfc_c"],
    )
    require(
        uniform["decision_dimension"] == 51
        and uniform["paper_population"] == 1020
        and uniform["paper_generations"] == 15000,
        "L0341 uniform MDPSO contract mismatch",
    )
    require(
        nonuniform["decision_dimension"] == 180
        and nonuniform["paper_population"] == 3600
        and nonuniform["paper_generations"] == 20000,
        "L0341 nonuniform MDPSO contract mismatch",
    )

    diagnostics: dict[str, dict] = {}
    for speed, direction in ((2, 0), (8, 0), (8, 30), (12, 0), (25, 0)):
        key = f"u{speed}_q{direction}"
        diagnostics[key] = call(
            args.binary,
            [
                "--mode", "diagnostic",
                "--speed-mps", str(speed),
                "--direction-degrees", str(direction),
            ],
        )["evaluation"]
    require(
        diagnostics["u2_q0"]["expected_power_mw"] == 0.0
        and diagnostics["u25_q0"]["expected_power_mw"] == 0.0,
        "L0341 cut-in/cut-out diagnostic mismatch",
    )
    # Table 3 is used as a scale and direction-response check. Exact replay is
    # impossible because C, k0, I0 and the analytic MM100 curve are omitted.
    targets = {
        "u8_q0": 6.86152,
        "u8_q30": 15.53209,
        "u12_q0": 21.45397,
    }
    errors = {
        key: relative_error(
            diagnostics[key]["expected_power_mw"], target
        )
        for key, target in targets.items()
    }
    require(
        all(value < 0.20 for value in errors.values()),
        f"L0341 diagnostic scale exceeds declared 20% boundary: {errors}",
    )
    require(
        diagnostics["u8_q30"]["expected_power_mw"]
        > diagnostics["u8_q0"]["expected_power_mw"],
        "L0341 diagnostic direction response mismatch",
    )

    with tempfile.TemporaryDirectory(prefix="l0341-h5-") as directory:
        outputs = {}
        for workers in (1, 4):
            output = Path(directory) / f"workers-{workers}.json"
            subprocess.run(
                [
                    args.binary,
                    "--mode", "optimize",
                    "--case", "l0341_uniform_wfa_a",
                    "--workers", str(workers),
                    "--generations", "3",
                    "--population-override", "64",
                    "--seed", "341341",
                    "--output", str(output),
                ],
                check=True,
            )
            outputs[workers] = json.loads(output.read_text())
    require(
        outputs[1]["physical_fes"] == 256
        and outputs[4]["physical_fes"] == 256,
        "L0341 physical-FES accounting mismatch",
    )
    require(
        outputs[1]["scientific_hash"] == outputs[4]["scientific_hash"],
        "L0341 one/multicore scientific hash mismatch",
    )
    require(
        outputs[4]["observed_workers"] >= 2,
        "L0341 multicore backend was not observed",
    )
    best = outputs[4]["best_evaluation"]
    initial = outputs[4]["initial_best"]
    require(
        best["feasible"]
        and math.isfinite(best["expected_power_mw"])
        and best["expected_power_mw"] + 1.0e-9
        >= initial["expected_power_mw"],
        "L0341 optimization is infeasible, non-finite or regressed",
    )

    receipt = {
        "status": "pass",
        "corpus_id": "L0341",
        "paper_case_count": len(cases),
        "uniform_reference": {
            "dimension": uniform["decision_dimension"],
            "population": uniform["paper_population"],
            "generations": uniform["paper_generations"],
        },
        "nonuniform_reference": {
            "dimension": nonuniform["decision_dimension"],
            "population": nonuniform["paper_population"],
            "generations": nonuniform["paper_generations"],
        },
        "diagnostic_table3_relative_errors": errors,
        "smoke_physical_fes": outputs[4]["physical_fes"],
        "observed_smoke_workers": outputs[4]["observed_workers"],
        "scientific_hash": outputs[4]["scientific_hash"],
        "claim_boundary":
            "paper-equation academic reproduction with declared missing-"
            "parameter completions; not author code or numerical replay",
    }
    output = ROOT / "evidence/core99/h5/L0341_local_h5.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
