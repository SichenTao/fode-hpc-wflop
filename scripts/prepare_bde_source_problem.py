#!/usr/bin/env python3
"""Generate the local-only BDE WS1-WS4 source-replay case manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.io import loadmat

from audit_bde_source_problem import (
    DEFAULT_SOURCE,
    SCENARIO_SHAPES,
    sha256,
    unavailable_cells,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = (
    ROOT / ".source-cache/generated/bde_source_replay/benchmark_cases.json"
)
SCENARIO_IDS = {
    "3speed_12direction.mat": "WS1",
    "3speed_12direction_uniform.mat": "WS2",
    "4speed_12direction.mat": "WS3",
    "6speed_12direction.mat": "WS4",
}


def canonical_hash(payload: object) -> str:
    encoded = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode()
    return hashlib.sha256(encoded).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    scenario_root = source / "WindFarmOptimization/windscenarios"
    terrain_mask = unavailable_cells(
        source / "WindFarmOptimization/gene_NA_loc.m"
    )

    wind_profiles: dict[str, dict[str, object]] = {}
    for filename, shape in SCENARIO_SHAPES.items():
        path = scenario_root / filename
        payload = loadmat(path, squeeze_me=True)
        theta = np.atleast_1d(payload["theta"]).astype(float)
        velocity = np.atleast_1d(payload["velocity"]).astype(float)
        probabilities = np.asarray(
            payload["f_theta_v"], dtype=float
        ).reshape(shape)
        wind_profiles[filename] = {
            "wind_directions_rad": theta.tolist(),
            "wind_speeds_mps": velocity.tolist(),
            "joint_probabilities": probabilities.tolist(),
            "source_windscenario_sha256": sha256(path),
        }

    cases = []
    terrains = (
        ("S", 21, 21, []),
        ("D", 28, 28, terrain_mask),
    )
    for terrain_id, rows, cols, unavailable in terrains:
        for filename, scenario_id in SCENARIO_IDS.items():
            wind = wind_profiles[filename]
            for turbine_count in (30, 35, 40):
                case = {
                    "case_id": (
                        f"BDE-{terrain_id}-{scenario_id}-tn{turbine_count}"
                    ),
                    "semantics_id": "bde2025_source_replay_ws1_ws4_v1",
                    "terrain_profile": (
                        "standard" if terrain_id == "S"
                        else "daegwallyeong_source_mask"
                    ),
                    "rows": rows,
                    "cols": cols,
                    "cell_width": 231.0,
                    "turbine_count": turbine_count,
                    "wind_directions_rad": wind["wind_directions_rad"],
                    "wind_speeds_mps": wind["wind_speeds_mps"],
                    "joint_probabilities": wind["joint_probabilities"],
                    "unavailable_cells_1based": unavailable,
                    "source_windscenario": filename,
                    "source_windscenario_sha256": (
                        wind["source_windscenario_sha256"]
                    ),
                }
                case["case_hash"] = canonical_hash(case)
                cases.append(case)

    collection = {
        "schema_version": 1,
        "problem_id": "bde2025_source_replay_ws1_ws4",
        "semantics_id": "bde2025_source_replay_ws1_ws4_v1",
        "case_count": len(cases),
        "source_archive_policy": (
            "generated locally from official no-license source; do not publish"
        ),
        "paper_source_boundary": {
            "included": "official-source WS1 through WS4 and 231 m spacing",
            "excluded": "paper-only WS5 and WS6 and unadjudicated 250 m spacing",
        },
        "cases": cases,
    }
    collection["collection_hash"] = canonical_hash(cases)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = arguments.output.with_suffix(arguments.output.suffix + ".tmp")
    temporary.write_text(json.dumps(collection, indent=2) + "\n")
    temporary.replace(arguments.output)
    print(
        "bde_source_problem_prepare_pass "
        f"cases={len(cases)} collection_hash={collection['collection_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
