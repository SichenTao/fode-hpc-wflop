#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T68 role, anchor, completion and schedule validator
Paper DOI: 10.1109/TSTE.2016.2614266
Public source: target code and machine-readable FINO3/cable/control arrays were
not located; the validator uses the paper's Tables I--III and Figure-6 stack
contract independently of the C++ implementation.
Missing information and deterministic completion:
hpc/core99_cpp/include/core99/hou_t68.hpp
Independence: this script reconstructs the digitized wind-mass total, paper
role dimensions/protocol, Table-II theta=0 anchors, MPPT/zero-dispatch
equivalence and worker schedule identity through the executable JSON API.
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


METHOD_ID = "t68_zhan_apso_offshore_codesign_declared_v1"
PROBLEM_ID = "t68_fino3_layout_dispatch_lpc_5role_declared_v1"
PROTOCOL_ID = "t68_native_10plus4x20_repeat_declared_v1"


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
    assert listing["role_count"] == 5
    assert listing["formal_target_runs"] == 90
    expected = {
        "direction_only": (1, 15, 100, 10),
        "scenario_i_spacing": (16, 30, 50, 20),
        "scenario_ii_spacing_direction": (17, 35, 70, 20),
        "scenario_iii_pitch": (4800, 100, 120, 20),
        "scenario_iv_codesign": (4817, 120, 230, 20),
    }
    for role, receipt in expected.items():
        inspected = invoke(binary, "--mode", "inspect", "--case", role)
        observed = (
            inspected["dimensions"],
            inspected["population_size"],
            inspected["maximum_iterations"],
            inspected["paper_repeats"],
        )
        assert observed == receipt
        assert inspected["wind_state_count"] == 60
        assert close(inspected["wind_probability_sum"], 1.0)

    # Independent paper Table-II anchor values; the C++ source cannot change
    # these without this test failing.
    anchor = invoke(binary, "--mode", "evaluate", "--case", "direction_only")
    evaluation = anchor["evaluation"]
    for field, paper_value in {
        "gross_energy_gwh": 1972.9,
        "cable_loss_gwh": 34.24,
        "cable_cost_mdkk": 345.25,
        "lpc_dkk_per_mwh": 178.14,
    }.items():
        assert close(evaluation[field], paper_value)
    assert evaluation["feasible"] is True
    assert close(evaluation["minimum_spacing_m"], 7.0 * 126.0)

    second = invoke(
        binary, "--mode", "evaluate", "--case", "scenario_ii_spacing_direction"
    )["evaluation"]
    third = invoke(
        binary, "--mode", "evaluate", "--case", "scenario_iii_pitch"
    )["evaluation"]
    for field in (
        "gross_energy_gwh", "cable_loss_gwh", "net_energy_gwh",
        "cable_cost_mdkk", "lpc_dkk_per_mwh",
    ):
        assert close(second[field], third[field])
    assert third["pitch_penalty_mdkk"] == 0.0

    common = (
        "--mode", "optimize",
        "--case", "direction_only",
        "--population", "6",
        "--iterations", "2",
        "--unchanged-iterations", "2",
        "--seed", "68019",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    for payload in (serial, parallel):
        assert payload["method_semantic_id"] == METHOD_ID
        assert payload["problem_semantic_id"] == PROBLEM_ID
        assert payload["protocol_semantic_id"] == PROTOCOL_ID
        assert payload["physical_fes"] > 0
        assert math.isfinite(payload["best_evaluation"]["lpc_dkk_per_mwh"])
    assert serial["physical_fes"] == parallel["physical_fes"]
    assert serial["scientific_hash"] == parallel["scientific_hash"]
    assert serial["best_decision"] == parallel["best_decision"]
    assert serial["best_evaluation"] == parallel["best_evaluation"]
    assert parallel["observed_workers"] >= 2

    print(json.dumps({
        "status": "pass",
        "paper_problem_roles": 5,
        "formal_target_runs": 90,
        "maximum_dimensions": 4817,
        "table_ii_anchor": True,
        "mppt_zero_dispatch_equivalence": True,
        "schedule_independent": True,
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
