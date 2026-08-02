#!/usr/bin/env python3
"""Validate the T74 paper/source contract and deterministic CPU-HPC replay."""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


PUBLISHED_SIGA = {
    "t74_case1_small": 83.6,
    "t74_case1_medium": 88.2,
    "t74_case1_large": 91.1,
    "t74_case2_small": 94.1,
    "t74_case2_medium": 96.2,
    "t74_case2_large": 97.1,
    "t74_case3_small": 79.6,
    "t74_case3_medium": 84.7,
    "t74_case3_large": 88.2,
    "t74_case4_small": 89.2,
    "t74_case4_medium": 92.5,
    "t74_case4_large": 94.5,
    "t74_case5_small": 84.9,
    "t74_case5_medium": 89.4,
    "t74_case5_large": 92.0,
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
        default=ROOT / "evidence/core99/h5/T74_local_h5.json",
    )
    arguments = parser.parse_args()

    cases = invoke(arguments.binary, "--mode", "list-cases")["paper_case_ids"]
    if list(PUBLISHED_SIGA) != cases:
        raise RuntimeError("T74 paper case ordering mismatch")
    inspections = {}
    for case_id in cases:
        item = invoke(
            arguments.binary, "--mode", "inspect", "--case", case_id
        )
        if (
            item["candidate_count"] != 441
            or item["turbine_count"] != 60
            or item["paper_population"] != 100
            or item["paper_generations"] != 200
            or item["paper_monte_carlo_layouts"] != 10000
            or item["paper_repeats"] != 30
        ):
            raise RuntimeError(f"T74 paper settings mismatch for {case_id}")
        inspections[case_id] = item

    common = [
        "--mode",
        "optimize",
        "--case",
        "t74_case1_medium",
        "--variant",
        "paper_probability",
        "--seed",
        "747474",
        "--monte-carlo-layouts",
        "300",
        "--generations",
        "8",
    ]
    one = invoke(arguments.binary, *common, "--workers", "1")["runs"][0]
    all_cores = invoke(
        arguments.binary, *common, "--workers", "4"
    )["runs"][0]
    if one["scientific_hash"] != all_cores["scientific_hash"]:
        raise RuntimeError("T74 deterministic multicore replay mismatch")
    if all_cores["observed_workers"] < 2:
        raise RuntimeError("T74 multicore work was not observed")
    if one["physical_fes"] != 1100 or all_cores["physical_fes"] != 1100:
        raise RuntimeError("T74 physical FES accounting mismatch")
    if (
        not math.isfinite(all_cores["best_evaluation"]["efficiency_percent"])
        or all_cores["best_evaluation"]["efficiency_percent"]
        < all_cores["initial_best"]["efficiency_percent"] - 1.0e-12
    ):
        raise RuntimeError("T74 best-history semantic mismatch")

    source = invoke(
        arguments.binary,
        *common,
        "--variant",
        "source_normal_threshold",
        "--workers",
        "4",
    )["runs"][0]
    if (
        source["method_semantic_id"]
        != "t74_siga_source_normal_threshold_v1"
    ):
        raise RuntimeError("T74 source conflict identity was not preserved")

    receipt = {
        "schema_version": 1,
        "corpus_id": "T74",
        "doi": "10.1016/j.apenergy.2019.04.084",
        "status": "H5_pass",
        "case_count": len(cases),
        "paper_settings": inspections,
        "published_siga_mean_efficiency_percent": PUBLISHED_SIGA,
        "bounded_replay": {
            "one_worker": one,
            "four_workers": all_cores,
            "source_conflict_variant": source,
            "scientific_hash_identical": True,
            "speedup": {
                "monte_carlo_truth": one["monte_carlo_truth_seconds"]
                / max(all_cores["monte_carlo_truth_seconds"], 1.0e-15),
                "population_truth": one["population_truth_seconds"]
                / max(all_cores["population_truth_seconds"], 1.0e-15),
                "algorithm": one["algorithm_seconds"]
                / max(all_cores["algorithm_seconds"], 1.0e-15),
                "end_to_end": one["end_to_end_seconds"]
                / max(all_cores["end_to_end_seconds"], 1.0e-15),
            },
        },
        "claim_boundary": (
            "Academic paper/source reconstruction; published Table 4 values "
            "are scale references, not exact-value acceptance targets."
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
