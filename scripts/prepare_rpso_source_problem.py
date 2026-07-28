#!/usr/bin/env python3
"""Generate the local-only RPSO paper-problem manifest from official assets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.io import loadmat


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (
    ROOT / ".source-cache/official/RPSO_Wind_Code/RPSO_Wind"
)
DEFAULT_OUTPUT = (
    ROOT / ".source-cache/generated/rpso_source_problem/benchmark_cases.json"
)
SCENARIOS = {
    "3speed_12direction.mat": (
        "WS1",
        "cbdb427c7909a36d5860a24bbb4ed4b0a8f9f23a3e6fa8117e3334e84fa9d732",
    ),
    "3speed_12direction_uniform.mat": (
        "WS2",
        "b9723cab0ab719941a7aaf0b30030cdf26012e9b9aa7179e6e2a828cec045fb9",
    ),
    "4speed_12direction.mat": (
        "WS3",
        "543998a16b8ec16ef6cc72303de44b88a44571defc1b5cc7cde1893fe7bee6bd",
    ),
    "6speed_12direction.mat": (
        "WS4",
        "155d267b26697032c5703c010bdfe878bad9a80717586483b5a9f91440a01bca",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return digest


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

    cases = []
    source_hashes: dict[str, str] = {}
    for filename, (scenario_id, expected_hash) in SCENARIOS.items():
        path = scenario_root / filename
        observed_hash = sha256(path)
        if observed_hash != expected_hash:
            raise RuntimeError(
                f"RPSO source scenario hash changed: {filename}"
            )
        source_hashes[filename] = observed_hash
        payload = loadmat(path, squeeze_me=True)
        theta = np.atleast_1d(payload["theta"]).astype(float)
        velocity = np.atleast_1d(payload["velocity"]).astype(float)
        probability = np.asarray(payload["f_theta_v"], dtype=float)
        probability = probability.reshape((theta.size, velocity.size))
        if abs(float(probability.sum()) - 1.0) > 5e-6:
            raise RuntimeError(f"{filename}: probabilities do not sum to one")
        for turbine_count in (30, 35, 40):
            case = {
                "case_id": f"RPSO-{scenario_id}-tn{turbine_count}",
                "semantics_id": "rpso2024_source_problem_ws1_ws4_v1",
                "rows": 21,
                "cols": 21,
                "cell_width": 231.0,
                "turbine_count": turbine_count,
                "wind_directions_rad": theta.tolist(),
                "wind_speeds_mps": velocity.tolist(),
                "joint_probabilities": probability.tolist(),
                "unavailable_cells_1based": [],
                "source_windscenario": filename,
                "source_windscenario_sha256": observed_hash,
            }
            case["case_hash"] = canonical_hash(case)
            cases.append(case)

    manifest = {
        "schema_version": 1,
        "problem_id": "rpso2024_source_problem_ws1_ws4",
        "semantics_id": "rpso2024_source_problem_ws1_ws4_v1",
        "case_count": len(cases),
        "source_archive_policy": (
            "generated locally from official no-license source; do not publish"
        ),
        "cross_archive_identity": (
            "the four wind files are byte-identical to the corresponding "
            "official BDE archive files"
        ),
        "source_hashes": source_hashes,
        "cases": cases,
    }
    manifest["collection_hash"] = canonical_hash(cases)
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = arguments.output.with_suffix(arguments.output.suffix + ".tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(arguments.output)
    print(
        "rpso_source_problem_prepare_pass "
        f"cases={len(cases)} collection_hash={manifest['collection_hash']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
