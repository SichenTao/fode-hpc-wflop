#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0499 H5 semantic and numerical validator
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/wen_l0499.hpp
Controlling contract:
shared/contracts/core99_l0499_wen_uncertain_cvar_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_SHA256 = (
    "42db407bc52a0c4b72bd1820ae1a6f61aba49f138b2026291e6a48bd8bc95d68"
)


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()
    fixture_hash = hashlib.sha256(Path(args.data).read_bytes()).hexdigest()
    require(fixture_hash == FIXTURE_SHA256, "L0499 fixture hash mismatch")

    cases = call(
        args.binary,
        ["--mode", "list-cases", "--data", args.data],
    )["paper_case_ids"]
    require(len(cases) == 126, "L0499 paper case count mismatch")
    require(len(set(cases)) == 126, "L0499 duplicate case IDs")
    require(cases[0:3] == [
        "l0499_case_a_to",
        "l0499_case_a_so",
        "l0499_case_a_ro",
    ], "L0499 Case-A registry mismatch")
    require(
        cases[-1] == "l0499_case_b_station_41_ro",
        "L0499 station registry mismatch",
    )

    inspect_a = call(
        args.binary,
        [
            "--mode", "inspect", "--case", "l0499_case_a_so",
            "--data", args.data, "--workers", "4",
        ],
    )
    inspect_b = call(
        args.binary,
        [
            "--mode", "inspect",
            "--case", "l0499_case_b_station_41_ro",
            "--data", args.data, "--workers", "4",
        ],
    )
    require(inspect_a["candidate_count"] == 144, "Case-A grid mismatch")
    require(inspect_a["sector_count"] == 5, "Case-A sector mismatch")
    require(inspect_b["candidate_count"] == 100, "Case-B grid mismatch")
    require(inspect_b["sector_count"] == 12, "Case-B sector mismatch")
    require(inspect_b["station_index"] == 40, "Case-B station mismatch")
    require(
        inspect_b["observed_precomputation_workers"] >= 2,
        "L0499 precomputation did not activate multicore backend",
    )
    require(
        abs(inspect_a["wind_mean_sum"] - 1.0) < 1.0e-10
        and abs(inspect_b["wind_mean_sum"] - 1.0) < 1.0e-10,
        "L0499 wind means are not normalized",
    )

    layout = ",".join(str(index) for index in range(50))
    evaluations: dict[str, dict] = {}
    for objective in ("to", "so", "ro"):
        payload = call(
            args.binary,
            [
                "--mode", "evaluate",
                "--case", f"l0499_case_a_{objective}",
                "--data", args.data,
                "--workers", "4",
                "--indices", layout,
            ],
        )
        evaluations[objective] = payload["evaluation"]
    reference = evaluations["so"]
    require(reference["feasible"], "declared Case-A layout infeasible")
    require(
        100000.0 < reference["expected_aep_mwh"] < 265000.0,
        "Case-A AEP outside physical paper scale",
    )
    for objective in ("to", "ro"):
        require(
            evaluations[objective]["sector_power_kw"]
            == reference["sector_power_kw"],
            "objective variants changed the physical evaluator",
        )
    expected_cvar = (
        reference["expected_aep_mwh"]
        - 1.3998096020390416
        * reference["aep_standard_deviation_mwh"]
    )
    require(
        abs(reference["cvar_mwh"] - expected_cvar) < 1.0e-8,
        "independent CVaR identity mismatch",
    )
    require(
        abs(evaluations["to"]["objective"]
            - reference["expected_aep_mwh"]) < 1.0e-8,
        "TO objective mismatch",
    )
    require(
        abs(evaluations["so"]["objective"]
            - reference["cvar_mwh"]) < 1.0e-8,
        "SO objective mismatch",
    )
    require(
        abs(evaluations["ro"]["objective"]
            - min(reference["sector_power_kw"])) < 1.0e-8,
        "RO objective mismatch",
    )
    require(all(
        math.isfinite(value)
        for value in (
            reference["expected_aep_mwh"],
            reference["aep_standard_deviation_mwh"],
            reference["cvar_mwh"],
        )
    ), "non-finite L0499 evaluation")

    with tempfile.TemporaryDirectory(prefix="l0499-h5-") as directory:
        outputs = {}
        for workers in (1, 4):
            output = Path(directory) / f"workers-{workers}.json"
            subprocess.run(
                [
                    args.binary,
                    "--mode", "optimize",
                    "--case", "l0499_case_b_station_01_so",
                    "--data", args.data,
                    "--workers", str(workers),
                    "--max-physical-fes", "320",
                    "--seed", "499499",
                    "--output", str(output),
                ],
                check=True,
            )
            outputs[workers] = json.loads(output.read_text())
    require(
        outputs[1]["scientific_hash"] == outputs[4]["scientific_hash"],
        "one/multicore scientific hash mismatch",
    )
    require(
        outputs[4]["observed_workers"] >= 2,
        "multicore population evaluation was not observed",
    )
    require(
        outputs[4]["physical_fes"] == 320,
        "physical-FES accounting mismatch",
    )
    require(
        outputs[4]["best_evaluation"]["objective"] + 1.0e-9
        >= outputs[4]["initial_best"]["objective"],
        "GA objective regressed",
    )

    receipt = {
        "status": "pass",
        "corpus_id": "L0499",
        "fixture_sha256": fixture_hash,
        "paper_case_count": len(cases),
        "case_a_candidate_count": inspect_a["candidate_count"],
        "case_b_candidate_count": inspect_b["candidate_count"],
        "case_b_station_count": 41,
        "objective_variants": ["to", "so", "ro"],
        "smoke_physical_fes": 320,
        "scientific_hash": outputs[4]["scientific_hash"],
        "observed_smoke_workers": outputs[4]["observed_workers"],
        "case_a_fixed_layout_expected_aep_mwh":
            reference["expected_aep_mwh"],
        "case_a_fixed_layout_cvar_mwh": reference["cvar_mwh"],
        "claim_boundary":
            "academic declared reproduction using the versioned NDAWN "
            "proxy; not author data, code, exact GA or numerical replay",
    }
    output = ROOT / "evidence/core99/h5/L0499_local_h5.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
