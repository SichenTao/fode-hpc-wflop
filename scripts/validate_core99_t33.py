#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T33 paper-case, official-data, objective
decomposition, result-scale and one/all-worker scientific oracle
Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
10.1016/j.ejor.2023.04.046
Dataset/source/missing/conflict/completion/HPC/claim facts:
hpc/core99_cpp/include/core99/cazzaro_t33.hpp.
Contract: shared/contracts/core99_t33_cazzaro_combined_2023.json.
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


AVAILABLE = (3196, 6974, 7090, 10398, 11478,
             11536, 14602, 19458, 20211, 21634)
FIXED = (42, 15, 8, 45, 40, 12, 35, 40, 36, 75)
LOW = (20, 49, 40, 85, 68, 79, 70, 94, 156, 117)
HIGH = (40, 99, 90, 170, 137, 158, 140, 188, 313, 235)
METHOD_ID = "t33_combined_layout_cable_vns_declared_v1"
PROBLEM_ID = "t33_official_synthetic10_low_high_joint_npv_v1"
PROTOCOL_ID = "t33_fixed_860_2064_cycles_25seed_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, *arguments: str) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        text=True,
        capture_output=True,
        timeout=30 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    return json.loads(completed.stdout)


def read_positions(path: Path) -> list[tuple[float, float, float, float, int]]:
    values = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        x, y, depth, foundation, zone = map(float, line.split())
        values.append((x, y, depth, foundation, round(zone)))
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data-root", required=True)
    args = parser.parse_args()
    root = Path(args.data_root)

    listing = execute(args.binary, "--mode", "list-cases")
    require(len(listing["cases"]) == 20, "T33 must expose 20 paper cases")
    expected_cases = [
        f"t33_official_{chr(ord('a') + site)}_{density}"
        for site in range(10)
        for density in ("low", "high")
    ]
    require(listing["cases"] == expected_cases, "T33 case order mismatch")

    for site in range(10):
        for density, turbines, cycles, seconds in (
            ("low", LOW[site], 860, 36000),
            ("high", HIGH[site], 2064, 86400),
        ):
            payload = execute(
                args.binary,
                "--mode", "inspect",
                "--data-root", str(root),
                "--case", f"{chr(ord('a') + site)}_{density}",
                "--workers", "4",
            )
            require(
                payload["available_positions"] == AVAILABLE[site],
                f"T33 site {site} decision-set count mismatch",
            )
            require(
                payload["fixed_turbines"] == FIXED[site],
                f"T33 site {site} fixed-turbine count mismatch",
            )
            require(payload["turbines"] == turbines, "T33 turbine count")
            require(sum(payload["zone_quotas"]) == turbines, "T33 quotas")
            require(payload["wind_states"] == 177, "T33 TNW states")
            require(payload["paper_fixed_cycles"] == cycles, "T33 cycles")
            require(
                payload["paper_time_limit_seconds"] == seconds,
                "T33 literal time",
            )
            routed = execute(
                args.binary,
                "--mode", "evaluate",
                "--data-root", str(root),
                "--case", f"{chr(ord('a') + site)}_{density}",
                "--workers", "4",
            )["evaluation"]
            require(
                routed["feasible"]
                and routed["spacing_violation_m"] == 0.0
                and routed["cable_crossings"] == 0,
                f"T33 site {site} {density} reference routing infeasible",
            )

    reference = execute(
        args.binary,
        "--mode", "evaluate",
        "--data-root", str(root),
        "--case", "a_low",
        "--workers", "4",
    )
    evaluation = reference["evaluation"]
    require(evaluation["feasible"], "T33 reference must be feasible")
    require(evaluation["cable_crossings"] == 0, "T33 cable crossing")
    require(evaluation["spacing_violation_m"] == 0.0, "T33 spacing")
    require(
        math.isclose(
            evaluation["lifetime_revenue_eur"],
            450.0 * evaluation["aep_mwh"],
            rel_tol=2e-15,
            abs_tol=1e-6,
        ),
        "T33 independent EPF equation mismatch",
    )
    require(
        math.isclose(
            evaluation["npv_eur"],
            evaluation["lifetime_revenue_eur"]
            - evaluation["foundation_cost_eur"]
            - evaluation["cable_cost_eur"],
            rel_tol=2e-15,
            abs_tol=1e-6,
        ),
        "T33 independent NPV equation mismatch",
    )
    # The reference is deliberately not an optimized replay.  Its cable
    # component should nevertheless share the paper's A-low physical scale
    # (combined Table B.6: 13 MEUR), rather than merely be finite.
    require(
        6.5e6 <= evaluation["cable_cost_eur"] <= 19.5e6,
        "T33 A-low cable scale is outside 50% of the paper result",
    )
    positions = read_positions(root / "site/A/availablePositions.txt")
    independent_foundation = sum(
        positions[index][3] for index in reference["positions"]
    )
    require(
        math.isclose(
            independent_foundation,
            evaluation["foundation_cost_eur"],
            rel_tol=2e-15,
            abs_tol=1e-6,
        ),
        "T33 independent official foundation sum mismatch",
    )

    with tempfile.TemporaryDirectory(prefix="core99-t33-h5-") as temporary:
        cache = Path(temporary) / "a.pair"
        outputs = {}
        for workers in (4, 1):
            outputs[workers] = execute(
                args.binary,
                "--mode", "optimize",
                "--data-root", str(root),
                "--case", "a_low",
                "--workers", str(workers),
                "--cycles", "2",
                "--seed", "330046",
                "--matrix-cache", str(cache),
            )
        parallel = outputs[4]
        serial = outputs[1]
        for payload, workers in ((parallel, 4), (serial, 1)):
            require(payload["problem_semantic_id"] == PROBLEM_ID, "problem ID")
            require(payload["method_semantic_id"] == METHOD_ID, "method ID")
            require(payload["protocol_semantic_id"] == PROTOCOL_ID, "protocol")
            require(payload["requested_workers"] == workers, "workers")
            require(payload["completed_vns_cycles"] == 2, "cycle count")
            require(payload["best"]["feasible"], "final feasibility")
            require(
                payload["best"]["npv_eur"] + 1e-6
                >= payload["initial"]["npv_eur"],
                "T33 best NPV regressed",
            )
        require(parallel["observed_workers"] >= 2, "parallel work unobserved")
        require(serial["observed_workers"] == 1, "serial worker mismatch")
        require(
            parallel["scientific_hash"] == serial["scientific_hash"],
            "T33 one/all-worker scientific trajectory mismatch",
        )
        require(
            parallel["best"] == serial["best"],
            "T33 one/all-worker objective mismatch",
        )

    print(
        "T33 H5 passed: 20 paper cases, official data, NPV/cable scale, "
        "feasibility and deterministic parallel trajectory"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
