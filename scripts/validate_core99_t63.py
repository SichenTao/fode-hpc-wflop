#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T63 proxy, geometry, MIP-lifecycle H5
Paper/DOI: Wind Farm Layout Optimization on Complex Terrains - Integrating a
CFD Wake Model with Mixed-Integer Programming;
10.1016/j.apenergy.2016.06.085
Public source/missing/reconstruction: hpc/core99_cpp/include/core99/kuo_t63.hpp
Controlling contract: shared/contracts/core99_t63_kuo_2016.json
Claim boundary: semantic/proxy validation, not author CFD numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--proxy", required=True)
    args = parser.parse_args()
    proxy = Path(args.proxy)
    payload = proxy.read_bytes()
    assert len(payload) == 1664
    assert hashlib.sha256(payload).hexdigest() == (
        "643fbafaca90e0e6c9dd8a271b1abea55"
        "a7b05d6bf64b12fc7c19e4fdc8ca51e"
    )
    assert payload[:8] == b"T63PXY1\0"
    grid, sectors = struct.unpack_from("<II", payload, 8)
    assert (grid, sectors) == (20, 12)
    elevation = struct.unpack_from("<400f", payload, 16)
    probability = struct.unpack_from("<12f", payload, 16 + 400 * 4)
    assert 100.0 <= min(elevation) <= max(elevation) <= 600.0
    assert abs(sum(probability) - 1.0) < 1.0e-6
    assert probability[9] > 0.30

    inspect = call(
        args.binary,
        ["--mode", "inspect", "--proxy", str(proxy)],
    )
    expected_speed = 6.0 * ((inspect["corner_elevation_m"] + 77 - 139) / 50) ** 0.16
    assert abs(inspect["cell0_speed_mps"] - expected_speed) < 1.0e-12
    assert inspect["grid_size"] == 20 and inspect["turbines"] == 20

    run = call(
        args.binary,
        [
            "--mode", "optimize",
            "--proxy", str(proxy),
            "--workers", "2",
            "--relaxation", "0.2",
            "--maximum-iterations", "1",
            "--mip-time-limit-seconds", "1",
        ],
    )
    layout = run["final_layout"]
    assert len(layout) == 20 and len(set(layout)) == 20
    for index, left in enumerate(layout):
        left_row, left_column = divmod(left, 20)
        for right in layout[index + 1 :]:
            right_row, right_column = divmod(right, 20)
            distance = 140.0 * math.hypot(
                left_row - right_row, left_column - right_column
            )
            assert distance >= 400.0 - 1.0e-12
    assert run["iterations"] == 1
    assert run["cfd_locations"] == 20
    assert run["cfd_simulations"] == 20 * 12
    assert 0.0 < run["final_true_objective"] <= run["no_wake_upper_bound"]
    assert 0.0 < run["layout_efficiency"] <= 1.0
    assert run["observed_workers"] >= 1
    assert run["history"][0]["new_cfd_locations"] == 20
    print(json.dumps(
        {
            "status": "pass",
            "proxy_sha256": hashlib.sha256(payload).hexdigest(),
            "west_probability": probability[9],
            "corner_speed_mps": inspect["cell0_speed_mps"],
            "mip_status": run["history"][0]["mip_status"],
            "layout_efficiency": run["layout_efficiency"],
            "scientific_hash": run["scientific_hash"],
        },
        sort_keys=True,
    ))


if __name__ == "__main__":
    main()
