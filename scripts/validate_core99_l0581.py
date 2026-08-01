#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0581 H5 semantic, accuracy and deterministic-HPC gate.
Paper/DOI, public source, missing assets, conflicts, reconstruction,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/varela_l0581.hpp.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


METHOD = "l0581_adaptive_sparse_colored_forward_ad_v1"
PROTOCOL = "l0581_accuracy_8_sizes_plus_10_paired_starts_v1"
SIZES = [38, 63, 95, 133, 177, 228, 285, 349]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments], check=True, text=True, capture_output=True,
        timeout=30 * 60,
    )
    return json.loads(completed.stdout)


def gradient_science(payload: dict) -> dict:
    ignored = {"requested_workers", "observed_workers", "seconds"}
    return {key: value for key, value in payload.items() if key not in ignored}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    roles = execute(args.binary, ["--action", "list-roles"])
    require(roles["protocol_semantic_id"] == PROTOCOL, "L0581 protocol")
    require(roles["accuracy_sizes"] == SIZES, "L0581 accuracy sizes")
    require(roles["optimization_turbines"] == 95, "L0581 optimizer farm")
    require(roles["optimization_wind_states"] == 12, "L0581 wind states")
    require(roles["paired_random_starts"] == 10, "L0581 paired starts")
    require(roles["required_optimization_runs"] == 20,
            "L0581 paired formal run count")

    geometry = {}
    accuracy = {}
    for turbines in SIZES:
        description = execute(args.binary, [
            "--action", "describe", "--turbines", str(turbines),
        ])
        require(description["turbines"] == turbines, "L0581 farm size")
        require(4.9 < description["actual_minimum_initial_spacing_d"] < 5.1,
                "L0581 geometry conflict contract")
        geometry[str(turbines)] = description[
            "actual_minimum_initial_spacing_d"
        ]
        row = execute(args.binary, [
            "--action", "accuracy", "--turbines", str(turbines),
            "--threshold", "1e-12", "--workers", "20",
        ])
        require(row["method_semantic_id"] == METHOD, "L0581 method")
        require(row["dense_colors"] == 2 * turbines,
                "L0581 dense colors")
        require(0 < row["sparse_colors"] < row["dense_colors"],
                "L0581 sparse compression")
        require(row["requested_workers"] == 20
                and row["observed_workers"] > 1,
                "L0581 accuracy all-core participation")
        require(math.isfinite(row["maximum_scaled_error"])
                and row["maximum_scaled_error"] <= 1e-8,
                "L0581 sparse accuracy")
        accuracy[str(turbines)] = {
            "color_fraction": row["color_fraction"],
            "maximum_scaled_error": row["maximum_scaled_error"],
        }

    common = [
        "--action", "gradient", "--turbines", "95",
        "--direction", "270", "--threshold", "1e-12",
    ]
    for mode in ("dense", "sparse"):
        serial = execute(args.binary, [*common, "--mode", mode,
                                       "--workers", "1"])
        parallel = execute(args.binary, [*common, "--mode", mode,
                                         "--workers", "20"])
        require(gradient_science(serial) == gradient_science(parallel),
                f"L0581 {mode} one/all-core science")
        require(parallel["observed_workers"] > 1,
                f"L0581 {mode} all-core participation")

    paired = {}
    for mode in ("dense", "sparse"):
        result = execute(args.binary, [
            "--action", "optimize", "--mode", mode,
            "--seed", "2023058101", "--workers", "20", "--smoke",
        ])
        require(result["method_semantic_id"] == METHOD, "L0581 method")
        require(result["protocol_semantic_id"] == PROTOCOL,
                "L0581 protocol")
        require(result["observed_workers"] > 1,
                "L0581 optimizer all-core participation")
        require(result["final_wake_loss_percent"]
                <= result["initial_wake_loss_percent"] + 1e-10,
                "L0581 optimizer increased wake loss")
        paired[mode] = result
    require(paired["dense"]["initial_wake_loss_percent"]
            == paired["sparse"]["initial_wake_loss_percent"],
            "L0581 paired starts differ")

    print(json.dumps({
        "status": "pass",
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "paper_geometry_actual_minimum_spacing_d": geometry,
        "accuracy_at_threshold_1e-12": accuracy,
        "deterministic_hpc": "pass",
        "paired_common_start": "pass",
        "claim_boundary": (
            "flexible equation/algorithm/protocol reproduction using "
            "same-author FLOWFarm lineage; not target source or numeric replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
