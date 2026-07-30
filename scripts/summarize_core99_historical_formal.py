#!/usr/bin/env python3
"""Validate and summarize the T01/T02 paper-native formal campaign."""

from __future__ import annotations

import argparse
import hashlib
import json
import statistics
from collections import defaultdict
from datetime import UTC, datetime
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--campaign-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    manifest_path = args.campaign_root / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    expected = (
        manifest["paper_case_count"] * manifest["repeats"]
    )
    if manifest["observation_count"] != expected:
        raise RuntimeError("manifest observation cardinality mismatch")
    groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
    seen = set()
    total_wall = 0.0
    for record in manifest["records"]:
        key = (
            record["corpus_id"],
            record["problem_id"],
            record["repeat"],
        )
        if key in seen:
            raise RuntimeError(f"duplicate observation: {key}")
        seen.add(key)
        result_path = args.campaign_root / record["result"]
        if sha256(result_path) != record["result_sha256"]:
            raise RuntimeError(f"result hash mismatch: {result_path}")
        result = json.loads(result_path.read_text(encoding="utf-8"))
        if result["algorithm_id"] != record["algorithm_id"]:
            raise RuntimeError(f"algorithm mismatch: {result_path}")
        if result["problem_id"] != record["problem_id"]:
            raise RuntimeError(f"problem mismatch: {result_path}")
        if result["seed"] != record["seed"]:
            raise RuntimeError(f"seed mismatch: {result_path}")
        if result["requested_workers"] != record["workers"]:
            raise RuntimeError(f"worker mismatch: {result_path}")
        groups[(record["corpus_id"], record["problem_id"])].append(result)
        total_wall += result["end_to_end_seconds"]
    if len(seen) != expected:
        raise RuntimeError("validated observation cardinality mismatch")

    summaries = []
    for (corpus_id, problem_id), values in sorted(groups.items()):
        if len(values) != manifest["repeats"]:
            raise RuntimeError(f"{problem_id}: repeat count mismatch")
        summaries.append(
            {
                "corpus_id": corpus_id,
                "problem_id": problem_id,
                "repeats": len(values),
                "physical_fes": sorted(
                    {value["physical_fes"] for value in values}
                ),
                "best_objective": {
                    "min": min(value["best_objective"] for value in values),
                    "median": statistics.median(
                        value["best_objective"] for value in values
                    ),
                    "max": max(value["best_objective"] for value in values),
                },
                "best_expected_power_kw": {
                    "min": min(
                        value["best_expected_power_kw"] for value in values
                    ),
                    "median": statistics.median(
                        value["best_expected_power_kw"] for value in values
                    ),
                    "max": max(
                        value["best_expected_power_kw"] for value in values
                    ),
                },
                "best_turbine_count": {
                    "min": min(
                        value["best_turbine_count"] for value in values
                    ),
                    "median": statistics.median(
                        value["best_turbine_count"] for value in values
                    ),
                    "max": max(
                        value["best_turbine_count"] for value in values
                    ),
                },
                "end_to_end_seconds": {
                    "min": min(
                        value["end_to_end_seconds"] for value in values
                    ),
                    "median": statistics.median(
                        value["end_to_end_seconds"] for value in values
                    ),
                    "max": max(
                        value["end_to_end_seconds"] for value in values
                    ),
                },
            }
        )
    receipt = {
        "schema_version": 1,
        "receipt_type": "core99_t01_t02_formal_quality",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "host": args.host,
        "source_commit": args.source_commit,
        "campaign_manifest_sha256": sha256(manifest_path),
        "paper_count": len({key[0] for key in groups}),
        "paper_case_count": len(groups),
        "repeats_per_case": manifest["repeats"],
        "observation_count": len(seen),
        "workers_per_optimization": manifest["workers_per_optimization"],
        "summed_optimization_wall_seconds": total_wall,
        "case_summaries": summaries,
        "claim_boundary": manifest["claim_boundary"],
    }
    canonical = json.dumps(receipt, sort_keys=True).encode()
    receipt["receipt_sha256"] = hashlib.sha256(canonical).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "core99_historical_formal_summary_pass "
        f"papers={receipt['paper_count']} "
        f"cases={receipt['paper_case_count']} "
        f"observations={receipt['observation_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
