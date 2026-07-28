#!/usr/bin/env python3
"""Audit the non-redistributed official BDE problem assets without copying data."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

import numpy as np
from scipy.io import loadmat


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (
    ROOT
    / ".source-cache/official/BDE-WindFarm_code/code"
)
EXPECTED_HASHES = {
    "main/main_D.m": "6e95a7869dd6b924ddf6e3ebf4277189f26f1634b65e387f142cf9a02f502e09",
    "main/main_S.m": "76109224bd946ce7ec11baef73505f4fe2051da2e2eeac9886160d22e19c8282",
    "main/BDE.m": "0a0bf2845857ce91c2d1284233b2ab1963b081d7a2e46ca8314d7b2e32c2d58d",
    "WindFarmOptimization/gene_NA_loc.m": "91fc345b6910a2df011630ac7cd2d24886879ec9edf977e4e4423af95ab3d7b7",
    "WindFarmOptimization/windscenarios/3speed_12direction.mat": "cbdb427c7909a36d5860a24bbb4ed4b0a8f9f23a3e6fa8117e3334e84fa9d732",
    "WindFarmOptimization/windscenarios/3speed_12direction_uniform.mat": "b9723cab0ab719941a7aaf0b30030cdf26012e9b9aa7179e6e2a828cec045fb9",
    "WindFarmOptimization/windscenarios/4speed_12direction.mat": "543998a16b8ec16ef6cc72303de44b88a44571defc1b5cc7cde1893fe7bee6bd",
    "WindFarmOptimization/windscenarios/6speed_12direction.mat": "155d267b26697032c5703c010bdfe878bad9a80717586483b5a9f91440a01bca",
}
SCENARIO_SHAPES = {
    "3speed_12direction.mat": (12, 3),
    "3speed_12direction_uniform.mat": (12, 3),
    "4speed_12direction.mat": (12, 4),
    "6speed_12direction.mat": (12, 6),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_literal(text: str, literal: str, source: str) -> None:
    if literal not in text:
        raise RuntimeError(f"{source} lacks frozen literal: {literal}")


def unavailable_cells(path: Path) -> list[int]:
    text = path.read_text()
    tail = text.split("case 13", 1)[1]
    definitions = [
        line.strip()
        for line in tail.splitlines()
        if line.strip().startswith("NA_loc_array =")
    ]
    if not definitions:
        raise RuntimeError("case 13 unavailable-cell definition is absent")
    expression = definitions[-1].split("[", 1)[1].rsplit("]", 1)[0]
    cells: list[int] = []
    for raw_token in expression.split(","):
        token = raw_token.strip()
        if re.fullmatch(r"\d+:\d+", token):
            first, last = (int(value) for value in token.split(":"))
            cells.extend(range(first, last + 1))
        elif re.fullmatch(r"\d+", token):
            cells.append(int(token))
        else:
            raise RuntimeError(f"unsupported case-13 token: {token}")
    if len(cells) != len(set(cells)):
        raise RuntimeError("case-13 unavailable-cell mask contains duplicates")
    if not cells or min(cells) < 1 or max(cells) > 28 * 28:
        raise RuntimeError("case-13 unavailable-cell mask is out of range")
    return cells


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--receipt", type=Path)
    arguments = parser.parse_args()
    source = arguments.source.resolve()

    observed_hashes: dict[str, str] = {}
    for relative, expected in EXPECTED_HASHES.items():
        path = source / relative
        if not path.is_file():
            raise RuntimeError(f"missing official BDE asset: {relative}")
        observed = sha256(path)
        if observed != expected:
            raise RuntimeError(
                f"official BDE asset hash changed: {relative} {observed}"
            )
        observed_hashes[relative] = observed

    main_standard = (source / "main/main_S.m").read_text()
    main_diverse = (source / "main/main_D.m").read_text()
    for text, label, rows in (
        (main_standard, "main_S.m", 21),
        (main_diverse, "main_D.m", 28),
    ):
        require_literal(text, f"rows = {rows};", label)
        require_literal(text, f"cols = {rows};", label)
        require_literal(text, "cell_width= 77.0 * 3;", label)
        require_literal(text, "turbine_num = [30,35,40, ];", label)
        require_literal(text, "n_speeds = [3,3,4,6];", label)
        require_literal(text, "n_directions = [12,12,12,12];", label)
        require_literal(text, "unifrom = [0,1,0,0];", label)
    require_literal(main_standard, "NA_type_list = 0;", "main_S.m")
    require_literal(main_diverse, "NA_type_list = 13;", "main_D.m")

    cells = unavailable_cells(
        source / "WindFarmOptimization/gene_NA_loc.m"
    )
    scenarios = []
    scenario_root = source / "WindFarmOptimization/windscenarios"
    for filename, expected_shape in SCENARIO_SHAPES.items():
        payload = loadmat(scenario_root / filename, squeeze_me=True)
        theta = np.atleast_1d(payload["theta"]).astype(float)
        velocity = np.atleast_1d(payload["velocity"]).astype(float)
        probability = np.asarray(payload["f_theta_v"], dtype=float).reshape(
            expected_shape
        )
        if theta.size != expected_shape[0]:
            raise RuntimeError(f"{filename}: direction count mismatch")
        if velocity.size != expected_shape[1]:
            raise RuntimeError(f"{filename}: speed count mismatch")
        if not np.isfinite(probability).all() or np.any(probability < 0.0):
            raise RuntimeError(f"{filename}: invalid joint probabilities")
        probability_sum = float(probability.sum())
        if abs(probability_sum - 1.0) > 5e-6:
            raise RuntimeError(
                f"{filename}: joint probabilities sum to {probability_sum}"
            )
        scenarios.append(
            {
                "source_file": filename,
                "directions": int(theta.size),
                "speeds": int(velocity.size),
                "joint_probability_sum": probability_sum,
                "sha256": observed_hashes[
                    "WindFarmOptimization/windscenarios/" + filename
                ],
            }
        )

    receipt = {
        "schema_version": 1,
        "audit_id": "bde2025_official_source_problem_20260729",
        "source_authority": "official_homepage_archive",
        "redistribution": "source arrays remain outside the public repository",
        "verified_hashes": observed_hashes,
        "source_executed_profiles": {
            "standard": {
                "grid": [21, 21],
                "cell_width_m": 231.0,
                "turbine_counts": [30, 35, 40],
                "unavailable_cells": 0,
            },
            "daegwallyeong": {
                "grid": [28, 28],
                "cell_width_m": 231.0,
                "turbine_counts": [30, 35, 40],
                "unavailable_cells": len(cells),
                "available_cells": 28 * 28 - len(cells),
            },
        },
        "source_wind_profiles": scenarios,
        "paper_source_boundary": {
            "paper_scenarios": 6,
            "source_driver_scenarios": 4,
            "missing_from_archive": [
                "WS5 eight-speed twelve-direction array",
                "WS6 eight-speed sixteen-direction array",
            ],
            "spacing_conflict": (
                "paper reports 250 m for Daegwallyeong; official main_D.m "
                "executes 77*3 = 231 m"
            ),
            "admissible_identity": "bde2025_source_replay_ws1_ws4_v1",
            "prohibited_identity": "complete six-scenario paper reproduction",
        },
    }
    if arguments.receipt:
        arguments.receipt.parent.mkdir(parents=True, exist_ok=True)
        temporary = arguments.receipt.with_suffix(
            arguments.receipt.suffix + ".tmp"
        )
        temporary.write_text(json.dumps(receipt, indent=2) + "\n")
        temporary.replace(arguments.receipt)
    print(
        "bde_source_problem_audit_pass "
        f"scenarios={len(scenarios)} unavailable_cells={len(cells)} "
        f"available_cells={28 * 28 - len(cells)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
