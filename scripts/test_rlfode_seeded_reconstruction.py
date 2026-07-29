#!/usr/bin/env python3
"""WFLOP IMPLEMENTATION FACT DECLARATION

Implementation unit: admission checks for declared seeded FQFODE reconstruction
Paper DOI: 10.3390/math13182935
Public author code URL: unavailable as recorded in docs/source-dossiers/S04.json
Method evidence tier: M3_DECLARED_COMPLETION
Controlling contract: shared/contracts/fqfode_seeded_training_reconstruction_contract.json
Claim boundary: deterministic reconstruction checks only; no author-policy claim
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


ALGORITHM = "fqfode_seeded_training_declared_reconstruction_v1"


def run(
    binary: Path,
    cases: Path,
    models: Path,
    workers: int,
    output: Path,
    case_id: str = "WS2tn50",
) -> dict:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            ALGORITHM,
            "--problem",
            "fode_e0_common",
            "--cases",
            str(cases),
            "--case",
            case_id,
            "--physical-fes",
            "480",
            "--seed",
            "20260728",
            "--workers",
            str(workers),
            "--rlfode-models",
            str(models),
            "--output",
            str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--models", type=Path, required=True)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="fqfode-admission-") as temp:
        root = Path(temp)
        serial = run(
            args.binary,
            args.cases,
            args.models,
            1,
            root / "serial.json",
        )
        parallel = run(
            args.binary,
            args.cases,
            args.models,
            20,
            root / "parallel.json",
        )
        d80 = run(
            args.binary,
            args.cases,
            args.models,
            20,
            root / "d80.json",
            "WS2tn80",
        )

    required_equal = [
        "best_expected_power_kw",
        "best_layout_1based",
        "physical_fes",
        "training_physical_fes",
        "inference_physical_fes",
        "policy_interactions",
        "policy_updates",
        "offline_training_physical_fes",
        "pretrained_artifact_hash",
        "learned_state_hash",
    ]
    for field in required_equal:
        if serial[field] != parallel[field]:
            raise SystemExit(f"worker invariance failed for {field}")
    if serial["physical_fes"] != 480:
        raise SystemExit("formal physical FES is not exact")
    if serial["training_physical_fes"] != 0:
        raise SystemExit("offline training leaked into the formal FES ledger")
    if serial["inference_physical_fes"] != 480:
        raise SystemExit("formal inference FES is not exact")
    if serial["offline_training_physical_fes"] <= 0:
        raise SystemExit("offline training receipt is missing")
    if serial["policy_updates"] != serial["policy_interactions"] - 6:
        raise SystemExit("paper generation-k Q-update schedule drifted")
    if serial["effective_semantics_id"] != ALGORITHM:
        raise SystemExit("unexpected FQFODE reconstruction semantics")
    if not serial["pretrained_artifact_hash"].startswith("fnv1a64:"):
        raise SystemExit("frozen Q-table artifact hash is missing")
    if serial["learned_state_hash"] == serial["pretrained_artifact_hash"]:
        raise SystemExit("formal online Q updates did not change table state")
    if d80["physical_fes"] != 480 or d80["generations"] <= 0:
        raise SystemExit("80-turbine FQFODE did not search to exact FES")
    if d80["initial_population"] != 3:
        raise SystemExit("80-turbine FQFODE initial population drifted from FODE")

    guarded = subprocess.run(
        [
            str(args.binary),
            "--algorithm",
            "rlfode",
            "--cases",
            str(args.cases),
            "--case",
            "WS1tn10",
            "--physical-fes",
            "480",
            "--workers",
            "1",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if guarded.returncode == 0 or "intentionally blocked" not in guarded.stderr:
        raise SystemExit("original rlfode identifier was not guarded")

    print(
        "rlfode_seeded_reconstruction_pass "
        f"artifact_hash={serial['pretrained_artifact_hash']} "
        f"online_hash={serial['learned_state_hash']} "
        "workers=1,20"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
