#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0590 H5 semantic, training and replay
validator
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/sun_l0590.hpp
Controlling contract:
shared/contracts/core99_l0590_sun_ann_height_2023.json
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
    args = parser.parse_args()
    cases = call(args.binary, ["--mode", "list-cases"])["paper_case_ids"]
    require(
        cases == [
            "l0590_e1", "l0590_e2", "l0590_e3", "l0590_e4",
            "l0590_c1", "l0590_c2", "l0590_c3", "l0590_c4",
        ],
        "L0590 paper-case registry mismatch",
    )
    with tempfile.TemporaryDirectory(prefix="l0590-h5-") as directory:
        root = Path(directory)
        training: dict[int, dict] = {}
        weight_hashes: dict[int, str] = {}
        for workers in (1, 4):
            weights = root / f"weights-{workers}.bin"
            training[workers] = call(
                args.binary,
                [
                    "--mode", "train",
                    "--weights", str(weights),
                    "--workers", str(workers),
                    "--maximum-epochs", "500",
                    "--sample-count", "8192",
                    "--target-mse", "1e-7",
                    "--seed", "590590",
                ],
            )
            weight_hashes[workers] = hashlib.sha256(
                weights.read_bytes()
            ).hexdigest()
        require(
            training[1]["scientific_hash"]
            == training[4]["scientific_hash"],
            "one/multicore training scientific hash mismatch",
        )
        require(
            weight_hashes[1] == weight_hashes[4],
            "one/multicore weight payload mismatch",
        )
        require(
            training[4]["observed_workers"] >= 2,
            "multicore training backend was not observed",
        )
        require(
            math.isfinite(training[4]["test_mse"])
            and training[4]["test_mse"] < 0.005,
            "surrogate test MSE outside H5 tolerance",
        )
        weights = root / "weights-4.bin"
        inspection = {}
        for case_id in cases:
            inspection[case_id] = call(
                args.binary,
                [
                    "--mode", "inspect",
                    "--case", case_id,
                    "--weights", str(weights),
                ],
            )
        require(
            inspection["l0590_e4"]["paper_generation_limit"] == 838,
            "E4 paper generation limit mismatch",
        )
        require(
            inspection["l0590_c4"]["paper_generation_limit"] == 1017,
            "C4 paper generation limit mismatch",
        )
        require(
            inspection["l0590_e2"]["optimizes_height"]
            and not inspection["l0590_e2"]["optimizes_layout"],
            "E2 decision-variable contract mismatch",
        )
        require(
            inspection["l0590_e3"]["optimizes_layout"]
            and not inspection["l0590_e3"]["optimizes_height"],
            "E3 decision-variable contract mismatch",
        )
        require(
            inspection["l0590_c4"]["minimizes_cost"],
            "C4 objective direction mismatch",
        )
        aligned = {}
        for case_id in ("l0590_e1", "l0590_c1"):
            aligned[case_id] = call(
                args.binary,
                [
                    "--mode", "evaluate-aligned",
                    "--case", case_id,
                    "--weights", str(weights),
                ],
            )["evaluation"]
        require(
            aligned["l0590_e1"]["feasible"],
            "aligned paper layout infeasible",
        )
        require(
            abs(
                aligned["l0590_e1"]["total_power_kw"]
                - aligned["l0590_c1"]["total_power_kw"]
            ) < 1e-9,
            "E1/C1 physical alias mismatch",
        )
        require(
            aligned["l0590_e1"]["minimum_spacing_m"] >= 385.0,
            "aligned spacing constraint mismatch",
        )
        outputs = {}
        for workers in (1, 4):
            outputs[workers] = call(
                args.binary,
                [
                    "--mode", "optimize",
                    "--case", "l0590_e4",
                    "--weights", str(weights),
                    "--workers", str(workers),
                    "--generations", "3",
                    "--seed", "590591",
                ],
            )
        require(
            outputs[1]["scientific_hash"]
            == outputs[4]["scientific_hash"],
            "one/multicore GA scientific hash mismatch",
        )
        require(
            outputs[4]["observed_workers"] >= 2,
            "multicore population evaluator was not observed",
        )
        require(
            outputs[4]["physical_fes"] == 256,
            "physical-FES accounting mismatch",
        )
        require(
            outputs[4]["best_evaluation"]["feasible"],
            "final E4 layout is infeasible",
        )
        require(
            outputs[4]["best_evaluation"]["objective"] + 1e-9
            >= outputs[4]["initial_best"]["objective"],
            "E4 smoke optimization regressed",
        )
        receipt = {
            "status": "pass",
            "corpus_id": "L0590",
            "paper_case_count": len(cases),
            "training_semantic_id":
                "l0590_mlp_3_5_6_1_from_scratch_v1",
            "problem_semantic_id":
                "l0590_shiren_3d_ann_layout_height_v1",
            "method_semantic_id": "l0590_real_ga_completed_v1",
            "training_sample_count": 8192,
            "training_epochs": training[4]["epochs"],
            "training_test_mse": training[4]["test_mse"],
            "paper_mse_target": 1e-6,
            "paper_mse_target_reached":
                training[4]["validation_mse"] <= 1e-6,
            "training_scientific_hash":
                training[4]["scientific_hash"],
            "weight_sha256": weight_hashes[4],
            "observed_training_workers":
                training[4]["observed_workers"],
            "smoke_physical_fes": outputs[4]["physical_fes"],
            "optimization_scientific_hash":
                outputs[4]["scientific_hash"],
            "observed_optimization_workers":
                outputs[4]["observed_workers"],
            "aligned_total_power_kw":
                aligned["l0590_e1"]["total_power_kw"],
            "claim_boundary":
                "academic declared reproduction using source-backed "
                "equations and deterministic completions; not author "
                "data, weights, exact GA, cost curve or numerical replay",
        }
    output = ROOT / "evidence/core99/h5/L0590_local_h5.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
