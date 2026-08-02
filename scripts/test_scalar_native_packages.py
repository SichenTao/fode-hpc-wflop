#!/usr/bin/env python3
"""Execute one bounded native-case smoke for every scalar target paper."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/scalar_problem_package_registry.tsv"
PROTOCOLS = ROOT / "docs/paper_experiment_protocols.tsv"
COMPLETION = ROOT / "docs/paper_package_completion.tsv"


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--workers", type=int, default=2)
    args = parser.parse_args()
    protocols = {row["corpus_id"]: row for row in rows(PROTOCOLS)}
    completion = {row["corpus_id"]: row for row in rows(COMPLETION)}
    receipts: list[dict] = []
    for row in rows(REGISTRY):
        contract_path = ROOT / row["case_contract"]
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        first_case = contract["cases"][0]
        dimension = int(first_case["turbine_count"])
        # 18D covers the largest LSHADE-family initialization; the fixed
        # PSO/GA families require at most 120 initial layouts.
        budget = max(600, 18 * dimension + 1)
        command = [
            args.binary,
            "--algorithm", row["target_algorithm"].lower().replace("-", ""),
            "--problem", row["problem_id"],
            "--cases", str(contract_path),
            "--case", first_case["case_id"],
            "--paper-protocol", protocols[row["corpus_id"]]["paper_protocol_id"],
            "--physical-fes", str(budget),
            "--seed", "20260730",
            "--workers", str(args.workers),
        ]
        algorithm_aliases = {
            "a-lshade": "alshade",
            "ms-shade": "msshade",
        }
        command[2] = algorithm_aliases.get(
            row["target_algorithm"].lower(), command[2]
        )
        completed = subprocess.run(
            command, check=True, capture_output=True, text=True
        )
        receipt = json.loads(completed.stdout)
        if receipt["physical_fes"] != budget:
            raise RuntimeError(
                f"{row['corpus_id']}: physical FES mismatch "
                f"{receipt['physical_fes']} != {budget}"
            )
        if receipt["case_id"] != first_case["case_id"]:
            raise RuntimeError(f"{row['corpus_id']}: case identity mismatch")
        if receipt["problem_id"] != row["problem_id"]:
            raise RuntimeError(f"{row['corpus_id']}: problem identity mismatch")
        if receipt["problem_semantics_id"] != row["problem_semantic_id"]:
            raise RuntimeError(f"{row['corpus_id']}: semantic identity mismatch")
        if (
            receipt["effective_semantics_id"]
            != completion[row["corpus_id"]]["method_semantic_id"]
        ):
            raise RuntimeError(
                f"{row['corpus_id']}: method semantic identity mismatch"
            )
        if len(receipt["best_layout_1based"]) != dimension:
            raise RuntimeError(f"{row['corpus_id']}: layout dimension mismatch")
        receipts.append(
            {
                "corpus_id": row["corpus_id"],
                "algorithm": command[2],
                "problem": row["problem_id"],
                "case": first_case["case_id"],
                "budget": budget,
            }
        )
    print(
        "scalar_native_package_smoke_pass "
        f"papers={len(receipts)} workers={args.workers}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
