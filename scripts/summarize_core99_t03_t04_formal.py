#!/usr/bin/env python3
"""Validate and summarize the completed T03/T04 paper-native campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stats(values: list[float]) -> dict[str, float]:
    return {
        "minimum": min(values),
        "median": statistics.median(values),
        "maximum": max(values),
        "mean": statistics.fmean(values),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest_path = args.input_root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = manifest["records"]
    if manifest["observation_count"] != 550 or len(records) != 550:
        raise RuntimeError("T03/T04 observation count is not 550")
    if manifest["paper_case_count"] != 22 or manifest["repeats"] != 25:
        raise RuntimeError("T03/T04 case/repeat contract drift")

    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    seen: set[tuple[str, str, int]] = set()
    reused = 0
    for record in records:
        identity = (
            record["corpus_id"],
            record["problem_id"],
            int(record["repeat"]),
        )
        if identity in seen:
            raise RuntimeError(f"duplicate formal observation: {identity}")
        seen.add(identity)
        path = args.input_root / record["result"]
        if sha256(path) != record["result_sha256"]:
            raise RuntimeError(f"result hash mismatch: {path}")
        result = json.loads(path.read_text(encoding="utf-8"))
        if result["problem_id"] != record["problem_id"]:
            raise RuntimeError(f"problem identity mismatch: {path}")
        if result["seed"] != record["seed"]:
            raise RuntimeError(f"seed mismatch: {path}")
        if result["requested_workers"] != record["workers_per_optimization"]:
            raise RuntimeError(f"worker policy mismatch: {path}")
        if result["physical_fes"] <= 0:
            raise RuntimeError(f"non-positive physical FES: {path}")
        reused += bool(record["reused"])
        grouped[(record["corpus_id"], record["problem_id"])].append(result)

    if len(grouped) != 22 or len(seen) != 550:
        raise RuntimeError("T03/T04 grouped cardinality drift")
    case_receipts: list[dict[str, Any]] = []
    all_feasible = 0
    total_end_to_end = 0.0
    for (corpus_id, problem_id), results in sorted(grouped.items()):
        if len(results) != 25:
            raise RuntimeError(f"{problem_id}: formal repeats are not 25")
        if len({result["seed"] for result in results}) != 25:
            raise RuntimeError(f"{problem_id}: duplicate seeds")
        if corpus_id == "T03":
            objective_field = "best_expected_power_kw"
            expected_fes = 12120
        else:
            objective_field = "best_farm_efficiency"
            expected_fes = 25000 if "case2" in problem_id else 15000
        if any(result["physical_fes"] != expected_fes for result in results):
            raise RuntimeError(f"{problem_id}: physical FES drift")
        feasible = sum(
            result["best_constraint_violation"] <= 1.0e-10
            for result in results
        )
        all_feasible += feasible
        end_to_end = [
            float(result["end_to_end_seconds"]) for result in results
        ]
        total_end_to_end += sum(end_to_end)
        case_receipts.append(
            {
                "corpus_id": corpus_id,
                "problem_id": problem_id,
                "algorithm_id": results[0]["algorithm_id"],
                "physical_fes_per_run": expected_fes,
                "workers_per_optimization": results[0][
                    "requested_workers"
                ],
                "repeats": 25,
                "feasible_runs": feasible,
                "objective_field": objective_field,
                "objective": stats(
                    [float(result[objective_field]) for result in results]
                ),
                "end_to_end_seconds": stats(end_to_end),
                "evaluator_seconds": stats(
                    [
                        float(result["evaluator_seconds"])
                        for result in results
                    ]
                ),
                "algorithm_seconds": stats(
                    [
                        float(result["algorithm_seconds"])
                        for result in results
                    ]
                ),
            }
        )
    receipt = {
        "schema_version": 1,
        "status": "complete",
        "campaign_id": manifest["campaign_id"],
        "source_commit": manifest["source_commit"],
        "manifest_sha256": sha256(manifest_path),
        "paper_count": 2,
        "paper_case_count": 22,
        "observation_count": 550,
        "repeats_per_case": 25,
        "reused_observations": reused,
        "feasible_observations": all_feasible,
        "summed_optimization_wall_seconds": total_end_to_end,
        "execution_policies": manifest["execution_policies"],
        "claim_boundary": manifest["claim_boundary"],
        "cases": case_receipts,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "core99_t03_t04_formal_summary_pass "
        f"observations={len(seen)} cases={len(grouped)} "
        f"feasible={all_feasible} wall={total_end_to_end:.6f}"
    )


if __name__ == "__main__":
    main()
