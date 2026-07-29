#!/usr/bin/env python3
"""Validate artifact-driven target transitions and timing ledgers."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import torch


TIMING_FIELDS = {
    "corpus",
    "transfer",
    "forward",
    "loss",
    "backward",
    "gradient_aggregation",
    "optimizer",
    "serialization",
    "inference",
    "optimization_loop",
}


def invoke(
    binary: str,
    method: str,
    backend: str,
    artifact_option: str,
    artifact: Path,
) -> dict:
    result = subprocess.run(
        [
            binary,
            "--method",
            method,
            "--backend",
            backend,
            artifact_option,
            str(artifact),
            "--seed",
            "20260730",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    receipt = json.loads(result.stdout)
    if receipt["status"] != "pass":
        raise RuntimeError(f"{method}/{backend}: target executable failed")
    if receipt["artifact_driven_optimization_transition"] is not True:
        raise RuntimeError(f"{method}/{backend}: transition bridge absent")
    if set(receipt["timing_seconds"]) != TIMING_FIELDS:
        raise RuntimeError(f"{method}/{backend}: timing ledger mismatch")
    if backend == "hybrid":
        if not (
            receipt["pinned_async_transfer"] is True
            and receipt["hybrid_queue_capacity"] == 2
            and 0 < receipt["hybrid_queue_max_observed"] <= 2
            and receipt["explicit_synchronization"] is True
        ):
            raise RuntimeError(
                f"{method}/{backend}: hybrid transfer contract mismatch"
            )
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    arguments = parser.parse_args()
    backends = ["cpu"]
    if torch.cuda.is_available():
        backends.append("hybrid")
    records = []
    with tempfile.TemporaryDirectory(prefix="plan004-artifacts-") as directory:
        root = Path(directory)
        for backend in backends:
            for method in ("taae", "alga", "rlpso"):
                artifact = root / f"{method}-{backend}.pt"
                trained = invoke(
                    arguments.binary,
                    method,
                    backend,
                    "--artifact-out",
                    artifact,
                )
                replay = invoke(
                    arguments.binary,
                    method,
                    backend,
                    "--artifact-in",
                    artifact,
                )
                if trained["bridge_checksum"] != replay["bridge_checksum"]:
                    raise RuntimeError(
                        f"{method}/{backend}: artifact transition replay mismatch"
                    )
                records.append(
                    {
                        "method": method,
                        "backend": backend,
                        "artifact_sha256": hashlib.sha256(
                            artifact.read_bytes()
                        ).hexdigest(),
                        "artifact_bytes": artifact.stat().st_size,
                        "bridge_checksum": trained["bridge_checksum"],
                    }
                )
    print(
        json.dumps(
            {
                "status": "pass",
                "contract": "plan004_artifact_target_optimization_v1",
                "records": records,
                "exact_transition_replays": len(records),
                "timing_fields": sorted(TIMING_FIELDS),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
