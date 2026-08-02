#!/usr/bin/env python3
"""Validate T80 paper cases, declared completions and CPU-HPC replay."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
PUBLISHED_MEAN_EFFICIENCY = {
    "t80_case1_s1_small": 94.59,
    "t80_case1_s1_medium": 96.41,
    "t80_case1_s1_large": 97.38,
    "t80_case1_s2_small": 79.89,
    "t80_case1_s2_medium": 84.97,
    "t80_case1_s2_large": 88.33,
    "t80_case1_s3_small": 90.50,
    "t80_case1_s3_medium": 93.15,
    "t80_case1_s3_large": 94.92,
    "t80_case1_s4_small": 85.68,
    "t80_case1_s4_medium": 90.20,
    "t80_case1_s4_large": 92.35,
    "t80_case2_new_jersey": 75.57,
}


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
        timeout=600,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument(
        "--receipt",
        type=Path,
        default=ROOT / "evidence/core99/h5/T80_local_h5.json",
    )
    arguments = parser.parse_args()

    cases = invoke(arguments.binary, "--mode", "list-cases")["paper_case_ids"]
    if cases != list(PUBLISHED_MEAN_EFFICIENCY):
        raise RuntimeError("T80 paper case registry mismatch")
    inspections = {}
    for case_id in cases:
        item = invoke(
            arguments.binary, "--mode", "inspect", "--case", case_id
        )
        if (
            item["paper_population_completion"] != 100
            or item["paper_generations"] != 200
            or item["paper_mcts_simulations_completion"] != 200
            or item["paper_repeats"] != 10
        ):
            raise RuntimeError(f"{case_id}: T80 budget mismatch")
        inspections[case_id] = item
    if (
        inspections["t80_case1_s1_small"]["candidate_count"] != 441
        or inspections["t80_case1_s1_small"]["turbine_count"] != 60
        or inspections["t80_case2_new_jersey"]["candidate_count"] != 600
        or inspections["t80_case2_new_jersey"]["turbine_count"] != 99
        or inspections["t80_case2_new_jersey"]["wind_state_count"] != 64
    ):
        raise RuntimeError("T80 native problem dimensions mismatch")

    common = [
        "--mode", "optimize",
        "--case", "t80_case1_s1_medium",
        "--seed", "808080",
        "--population", "12",
        "--generations", "3",
        "--mcts-simulations", "8",
    ]
    one = invoke(arguments.binary, *common, "--workers", "1")["runs"][0]
    four = invoke(arguments.binary, *common, "--workers", "4")["runs"][0]
    if one["scientific_hash"] != four["scientific_hash"]:
        raise RuntimeError("T80 one/multicore scientific replay mismatch")
    if four["observed_workers"] < 2:
        raise RuntimeError("T80 multicore work not observed")
    if one["physical_fes"] != four["physical_fes"]:
        raise RuntimeError("T80 physical FES changed with scheduling")
    if (
        not four["best_evaluation"]["feasible"]
        or not math.isfinite(
            four["best_evaluation"]["conversion_efficiency_percent"]
        )
        or four["best_evaluation"]["conversion_efficiency_percent"]
        < four["initial_best"]["conversion_efficiency_percent"] - 1.0e-12
    ):
        raise RuntimeError("T80 feasibility or retained-best mismatch")

    receipt = {
        "schema_version": 1,
        "corpus_id": "T80",
        "doi": "10.1016/j.enconman.2021.115047",
        "status": "H5_pass",
        "case_count": len(cases),
        "paper_settings": inspections,
        "published_mean_efficiency_percent":
            PUBLISHED_MEAN_EFFICIENCY,
        "bounded_replay": {
            "one_worker": one,
            "four_workers": four,
            "scientific_hash_identical": True,
            "physical_fes_identical": True,
            "speedup": {
                "population_evaluation":
                    one["population_evaluation_seconds"]
                    / max(four["population_evaluation_seconds"], 1.0e-15),
                "mcts_relocation":
                    one["mcts_relocation_seconds"]
                    / max(four["mcts_relocation_seconds"], 1.0e-15),
                "genetic_operator":
                    one["genetic_operator_seconds"]
                    / max(four["genetic_operator_seconds"], 1.0e-15),
                "end_to_end":
                    one["end_to_end_seconds"]
                    / max(four["end_to_end_seconds"], 1.0e-15),
            },
        },
        "claim_boundary": (
            "Academic paper/predecessor reconstruction; New Jersey is a "
            "figure-derived proxy; published means are references, not "
            "exact-value gates."
        ),
    }
    arguments.receipt.parent.mkdir(parents=True, exist_ok=True)
    arguments.receipt.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
