#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T78 role, paper-anchor and schedule validator
Paper DOI: 10.1016/j.apenergy.2020.114896
Public source: no target source or machine-readable target arrays were located.
Independence: this validator fixes the paper's dimensions, cases, repeats,
Figure-5 wind-state count, Table-2 reference energy/noise, Eq.21 compensation
and worker-schedule identity through the executable JSON API.
Missing information and declared completion:
hpc/core99_cpp/include/core99/wu_t78.hpp
Claim boundary: academic reconstruction validation, not author-array replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
from pathlib import Path


METHOD_ID = "t78_traditional_pso_declared_v1"
PROBLEM_ID = "t78_fino3_noise_layout_two_case_declared_v1"
PROTOCOL_ID = "t78_native_2x10_repeat_declared_v1"


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def close(left: float, right: float, tolerance: float = 1.0e-10) -> bool:
    return abs(left - right) <= tolerance * max(1.0, abs(left), abs(right))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    assert listing["paper_case_roles"] == [
        "strict_noise_control", "economic_compensation"
    ]
    assert listing["formal_target_runs"] == 20
    for case in listing["paper_case_roles"]:
        inspected = invoke(binary, "--mode", "inspect", "--case", case)
        assert inspected["dimensions"] == 160
        assert inspected["population_size"] == 100
        assert inspected["maximum_iterations"] == 200
        assert inspected["paper_repeats"] == 10
        assert inspected["wind_state_count"] == 60
        assert close(inspected["wind_probability_sum"], 1.0)

    strict = invoke(
        binary, "--mode", "evaluate", "--case", "strict_noise_control"
    )["evaluation"]
    economic = invoke(
        binary, "--mode", "evaluate", "--case", "economic_compensation"
    )["evaluation"]
    assert close(strict["annual_energy_gwh"], 4015.17)
    assert close(strict["maximum_l10_dba"], 48.60)
    assert close(strict["excess_noise_dba"], 3.60)
    assert strict["feasible"] is False
    assert economic["feasible"] is True
    assert close(strict["hard_noise_violation_dba"], 3.60)
    assert close(economic["hard_noise_violation_dba"], 0.0)
    assert close(economic["noise_penalty_gwh"], 0.036)

    common = (
        "--mode", "optimize",
        "--case", "economic_compensation",
        "--population", "6",
        "--iterations", "2",
        "--seed", "78019",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    for payload in (serial, parallel):
        assert payload["method_semantic_id"] == METHOD_ID
        assert payload["problem_semantic_id"] == PROBLEM_ID
        assert payload["protocol_semantic_id"] == PROTOCOL_ID
        assert payload["physical_fes"] == 18
        assert math.isfinite(payload["best_evaluation"]["objective_gwh"])
    assert serial["physical_fes"] == parallel["physical_fes"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["best_decision"] == parallel["best_decision"]
    assert serial["best_evaluation"] == parallel["best_evaluation"]
    assert parallel["observed_workers"] >= 2

    print(json.dumps({
        "status": "pass",
        "paper_problem_roles": 2,
        "formal_target_runs": 20,
        "reference_energy_anchor": True,
        "reference_noise_anchor": True,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
