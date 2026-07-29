#!/usr/bin/env python3
"""Audit paper-native scalar comparator coverage and bounded execution.

This is an admission audit, not a performance claim.  It proves that every
comparator named by a scalar/discrete paper protocol has an explicit evidence
record, resolves through the C++ registry, is compatible with the paper-native
problem, and either completes a deterministic bounded run or has an explicit
train-from-scratch lifecycle.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PAPERS = ROOT / "docs/scalar_problem_package_registry.tsv"
PROTOCOLS = ROOT / "docs/paper_experiment_protocols.tsv"
COMPARATORS = ROOT / "docs/scalar_comparator_registry.tsv"


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def run_text(command: list[str]) -> str:
    return subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--execute", action="store_true")
    args = parser.parse_args()

    protocol_by_corpus = {row["corpus_id"]: row for row in rows(PROTOCOLS)}
    paper_rows = rows(PAPERS)
    comparator_rows = rows(COMPARATORS)
    comparator_by_id = {row["algorithm_id"]: row for row in comparator_rows}
    if len(comparator_by_id) != len(comparator_rows):
        raise RuntimeError("duplicate algorithm_id in scalar comparator registry")

    binary_algorithms = set(run_text([args.binary, "--list-algorithms"]).splitlines())
    training_rows = run_text([args.binary, "--list-training"]).splitlines()
    training_algorithms = {
        line.split("\t")[1] for line in training_rows if "\t" in line
    }

    required: set[str] = set()
    pairs: list[tuple[dict[str, str], str]] = []
    for paper in paper_rows:
        protocol = protocol_by_corpus[paper["corpus_id"]]
        algorithms = [
            value.strip()
            for value in protocol["comparator_algorithms"].split(";")
            if value.strip()
        ]
        if paper["target_algorithm"].lower().replace("-", "") not in algorithms:
            algorithms.append(paper["target_algorithm"].lower().replace("-", ""))
        for algorithm in algorithms:
            required.add(algorithm)
            pairs.append((paper, algorithm))

    missing_evidence = sorted(required - set(comparator_by_id))
    extra_evidence = sorted(set(comparator_by_id) - required)
    missing_binary = sorted(required - binary_algorithms)
    if missing_evidence or extra_evidence or missing_binary:
        raise RuntimeError(
            "comparator coverage mismatch: "
            f"missing_evidence={missing_evidence}, "
            f"extra_evidence={extra_evidence}, "
            f"missing_binary={missing_binary}"
        )

    completed_pairs = 0
    trained_pairs = 0
    for paper, algorithm in pairs:
        evidence = comparator_by_id[algorithm]
        decision = json.loads(
            run_text(
                [
                    args.binary,
                    "--explain-compatibility",
                    algorithm,
                    paper["problem_id"],
                ]
            )
        )
        if not decision["compatible"]:
            raise RuntimeError(
                f"{paper['corpus_id']} comparator {algorithm} is incompatible "
                f"with {paper['problem_id']}: {decision['reason']}"
            )

        status = evidence["bounded_execution_status"]
        if status == "training_required":
            if algorithm not in training_algorithms:
                raise RuntimeError(
                    f"{algorithm}: training required but no lifecycle is registered"
                )
            trained_pairs += 1
            continue
        if status != "executable":
            raise RuntimeError(f"{algorithm}: unsupported execution status {status}")
        if not args.execute:
            continue

        contract_path = ROOT / paper["case_contract"]
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        case = contract["cases"][0]
        dimension = int(case["turbine_count"])
        budget = max(600, 18 * dimension + 1)
        command = [
            args.binary,
            "--algorithm",
            algorithm,
            "--problem",
            paper["problem_id"],
            "--cases",
            str(contract_path),
            "--case",
            case["case_id"],
            "--paper-protocol",
            protocol_by_corpus[paper["corpus_id"]]["paper_protocol_id"],
            "--physical-fes",
            str(budget),
            "--seed",
            "20260730",
            "--workers",
            str(args.workers),
        ]
        receipt = json.loads(run_text(command))
        if int(receipt["physical_fes"]) != budget:
            raise RuntimeError(
                f"{paper['corpus_id']}/{algorithm}: physical FES mismatch"
            )
        if receipt["algorithm_id"] != algorithm:
            raise RuntimeError(
                f"{paper['corpus_id']}/{algorithm}: algorithm identity mismatch"
            )
        if receipt["problem_semantics_id"] != paper["problem_semantic_id"]:
            raise RuntimeError(
                f"{paper['corpus_id']}/{algorithm}: problem semantic mismatch"
            )
        completed_pairs += 1

    print(
        "scalar_comparator_package_audit_pass "
        f"papers={len(paper_rows)} algorithms={len(required)} "
        f"paper_algorithm_pairs={len(pairs)} "
        f"executed_pairs={completed_pairs} "
        f"training_required_pairs={trained_pairs}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
