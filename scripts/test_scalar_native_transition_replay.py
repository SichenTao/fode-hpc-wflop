#!/usr/bin/env python3
"""Check schedule-independent bounded transitions for scalar target methods."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGES = ROOT / "docs/scalar_problem_package_registry.tsv"
PROTOCOLS = ROOT / "docs/paper_experiment_protocols.tsv"


def rows(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def execute(binary: str, paper: dict[str, str], protocol: str, workers: int) -> dict:
    contract_path = ROOT / paper["case_contract"]
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    case = contract["cases"][0]
    dimension = int(case["turbine_count"])
    budget = max(600, 18 * dimension + 1)
    aliases = {"A-LSHADE": "alshade", "MS-SHADE": "msshade"}
    algorithm = aliases.get(
        paper["target_algorithm"], paper["target_algorithm"].lower()
    )
    command = [
        binary,
        "--algorithm",
        algorithm,
        "--problem",
        paper["problem_id"],
        "--cases",
        str(contract_path),
        "--case",
        case["case_id"],
        "--paper-protocol",
        protocol,
        "--physical-fes",
        str(budget),
        "--seed",
        "20260730",
        "--workers",
        str(workers),
    ]
    output = subprocess.run(
        command, check=True, capture_output=True, text=True
    ).stdout
    return json.loads(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--parallel-workers", type=int, default=2)
    args = parser.parse_args()
    protocols = {
        row["corpus_id"]: row["paper_protocol_id"] for row in rows(PROTOCOLS)
    }
    stable_fields = (
        "algorithm_id",
        "effective_semantics_id",
        "problem_id",
        "problem_semantics_id",
        "case_id",
        "seed",
        "physical_fes",
        "generations",
        "initial_population",
        "final_population",
        "best_expected_power_kw",
        "best_layout_1based",
    )
    checked = 0
    for paper in rows(PACKAGES):
        serial = execute(
            args.binary, paper, protocols[paper["corpus_id"]], 1
        )
        parallel = execute(
            args.binary,
            paper,
            protocols[paper["corpus_id"]],
            args.parallel_workers,
        )
        for field in stable_fields:
            if serial[field] != parallel[field]:
                raise RuntimeError(
                    f"{paper['corpus_id']}: worker-invariant field {field} "
                    f"differs: {serial[field]!r} != {parallel[field]!r}"
                )
        checked += 1
    print(
        "scalar_native_transition_replay_pass "
        f"papers={checked} workers=1,{args.parallel_workers}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
