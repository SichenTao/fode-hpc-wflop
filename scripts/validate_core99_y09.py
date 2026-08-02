#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y09 independent CLI/identity/schedule validation
Paper/DOI: Li et al.; 10.1016/j.renene.2025.124386
Public source/data, missing information, paper/patent conflicts, completion,
semantic IDs, production backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/li_y09.hpp
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


EXPECTED_CASES = {
    "Y09_west_five_only",
    "Y09_west_multi",
    "Y09_west_fifteen_only",
    "Y09_northwest_multi",
    "Y09_southwest_multi",
    "Y09_fatigue_008_multi",
    "Y09_fatigue_012_multi",
    "Y09_fatigue_016_multi",
    "Y09_cost_020_multi",
    "Y09_cost_030_multi",
    "Y09_cost_040_multi",
    "Y09_cost_050_multi",
}
METHOD = "y09_ternary_variable_mutation_ga_declared_v1"
PROBLEM = "y09_multitype_mqi_fatigue_lcoe_declared_v1"
PROTOCOL = "y09_native_12case_single_run_declared_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def invoke(binary: Path, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, text=True, capture_output=True
    )
    return json.loads(completed.stdout)


def run_to_file(binary: Path, case: str, workers: int, output: Path) -> dict:
    subprocess.run(
        [
            str(binary), "--case", case, "--seed", "9091701",
            "--workers", str(workers), "--population", "20",
            "--maximum-generations", "2", "--disable-convergence",
            "--output", str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def validate_result(payload: dict, case: str, workers: int) -> None:
    require(payload["case_id"] == case, "case identity")
    require(payload["method_semantic_id"] == METHOD, "method identity")
    require(payload["problem_semantic_id"] == PROBLEM, "problem identity")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol identity")
    require(payload["requested_workers"] == workers, "requested workers")
    require(payload["physical_fes"] == 60, "physical FES")
    require(payload["best_evaluation"]["feasible"] is True, "feasibility")
    require(len(payload["best_layout"]) == 100, "ternary layout length")
    require(payload["parallel_regions"] > 0, "parallel work receipt")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    listed = invoke(binary, ["--action", "list-cases"])
    cases = {item["case_id"] for item in listed["paper_native_cases"]}
    require(cases == EXPECTED_CASES, "complete twelve-case matrix")
    require(listed["population"] == 100, "first-party patent population")
    require(abs(listed["crossover_rate"] - 0.08) < 1e-15, "crossover")
    require(abs(listed["mutation_rate"] - 0.01) < 1e-15, "mutation")

    with tempfile.TemporaryDirectory(prefix="core99-y09-") as directory:
        root = Path(directory)
        serial = run_to_file(binary, "Y09_west_multi", 1, root / "w1.json")
        parallel = run_to_file(binary, "Y09_west_multi", 4, root / "w4.json")
        validate_result(serial, "Y09_west_multi", 1)
        validate_result(parallel, "Y09_west_multi", 4)
        for key in (
            "generations", "physical_fes", "convergence_reason",
            "best_evaluation", "best_layout", "scientific_hash",
        ):
            require(serial[key] == parallel[key], f"schedule identity {key}")
        for case, allowed in (
            ("Y09_west_five_only", {0, 1}),
            ("Y09_west_fifteen_only", {0, 2}),
        ):
            payload = run_to_file(binary, case, 4, root / f"{case}.json")
            validate_result(payload, case, 4)
            require(set(payload["best_layout"]) <= allowed, f"{case} encoding")
    print("core99_y09_validation_pass native_cases=12 schedule_identity=pass")


if __name__ == "__main__":
    main()
