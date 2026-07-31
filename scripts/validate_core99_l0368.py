#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0368 role, receipt and HPC validator
Paper DOI: 10.1016/j.enconman.2021.114610
Cited same-author public asset DOI: 10.17632/bvrdgykzwy.1
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/liu_l0368.hpp
Claim boundary: flexible academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


METHOD = "l0368_matlab_lineage_real_ga_declared_v1"
PROBLEM = "l0368_seabed_engineering_capital_power_proxy_v1"
PROTOCOL = "l0368_native_s1_s5_w1_w4_single_run_v1"


def call(binary: Path, *arguments: str) -> dict | list:
    completed = subprocess.run(
        [str(binary), *arguments], check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def paper_power_mw(speed_mps: float) -> float:
    """Independent transcription of the paper's uncapped Eq. 2."""
    if speed_mps < 4.0 or speed_mps > 20.0:
        return 0.0
    swept_area_m2 = 0.25 * math.pi * 100.0**2
    return 0.5 * 1.225 * swept_area_m2 * speed_mps**3 * 0.4 / 1.0e6


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    cases = call(binary, "--action", "list-cases")
    assert isinstance(cases, list) and len(cases) == 20
    assert [case["case_id"] for case in cases] == [
        f"L0368_S{terrain}W{wind}"
        for wind in range(1, 5) for terrain in range(1, 6)
    ]
    assert [case["paper_turbine_anchor"] for case in cases[:5]] == [
        18, 15, 19, 16, 17
    ]
    assert cases[0]["paper_total_power_anchor_mw"] == 56.82
    assert cases[-1]["paper_capital_power_anchor_million_gbp_per_mw"] == 9.94

    # Validate the evaluator against equations transcribed independently here,
    # rather than accepting serial/parallel agreement as sufficient evidence.
    wind_expected = {
        1: paper_power_mw(12.0),
        2: paper_power_mw(12.0),
        3: 0.30 * paper_power_mw(12.0) + 0.70 * paper_power_mw(16.0),
        4: sum(
            weight * paper_power_mw(speed)
            for speed, weight in zip(
                (4.0, 6.0, 8.0, 10.0, 12.0),
                (0.30, 0.35, 0.25, 0.08, 0.02),
                strict=True,
            )
        ),
    }
    for wind, expected_power_mw in wind_expected.items():
        single = call(
            binary,
            "--action", "evaluate-layout",
            "--case", f"L0368_S1W{wind}",
            "--layout-points", "1000,1000",
        )["evaluation"]
        assert math.isclose(
            single["expected_power_mw"], expected_power_mw,
            rel_tol=2.0e-14, abs_tol=1.0e-14,
        )
        assert math.isclose(
            single["no_wake_power_mw"], expected_power_mw,
            rel_tol=2.0e-14, abs_tol=1.0e-14,
        )
        assert math.isclose(single["efficiency_percent"], 100.0, abs_tol=1.0e-12)
        direct_cost = (
            single["wind_turbine_cost_gbp"]
            + single["support_structure_cost_gbp"]
            + single["cable_substation_port_cost_gbp"]
        )
        assert math.isclose(
            single["initial_capital_cost_gbp"],
            direct_cost / (1.0 - 0.043 - 0.174),
            rel_tol=2.0e-14,
        )
        assert math.isclose(
            single["capital_power_proxy_gbp_per_mw"],
            single["initial_capital_cost_gbp"] / expected_power_mw,
            rel_tol=2.0e-14,
        )
    common = (
        "--case", "L0368_S1W1", "--seed", "36801",
        "--population", "20", "--generations", "2", "--elite-count", "2",
    )
    serial = call(binary, *common, "--workers", "1")
    parallel = call(binary, *common, "--workers", "4")
    for payload in (serial, parallel):
        assert payload["method_semantic_id"] == METHOD
        assert payload["problem_semantic_id"] == PROBLEM
        assert payload["protocol_semantic_id"] == PROTOCOL
        assert payload["physical_fes"] == 60
        evaluation = payload["best_evaluation"]
        assert evaluation["feasible"] is True
        assert 1 <= evaluation["turbine_count"] <= 25
        assert evaluation["expected_power_mw"] > 0
        assert evaluation["initial_capital_cost_gbp"] > 0
        assert math.isfinite(evaluation["capital_power_proxy_gbp_per_mw"])
        if evaluation["turbine_count"] > 1:
            assert evaluation["minimum_distance_m"] >= 500.0 - 1e-7
    for key in ("scientific_hash", "best_layout", "best_evaluation", "physical_fes"):
        assert serial[key] == parallel[key], key
    assert parallel["observed_workers"] >= 2
    print("L0368 independent H5 validation passed")


if __name__ == "__main__":
    main()
