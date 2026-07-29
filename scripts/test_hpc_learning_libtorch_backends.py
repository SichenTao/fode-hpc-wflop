#!/usr/bin/env python3
"""Exercise real LibTorch CPU/CUDA/hybrid training and artifact replay."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import tempfile
from pathlib import Path


def execute(
    binary: str,
    method: str,
    backend: str,
    artifact: Path,
    *,
    replay: bool,
) -> dict:
    command = [
        binary,
        "--method",
        method,
        "--backend",
        backend,
        "--samples",
        "32",
        "--epochs",
        "2",
        "--batch-size",
        "16",
        "--workers",
        "2",
        "--seed",
        "20260730",
        "--artifact-in" if replay else "--artifact-out",
        str(artifact),
    ]
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    checked = 0
    with tempfile.TemporaryDirectory(
        prefix="plan003-libtorch-"
    ) as directory:
        root = Path(directory)
        for method in ("taae", "alga", "rlpso"):
            results: dict[str, dict] = {}
            for backend in ("cpu", "gpu", "hybrid"):
                artifact = root / f"{method}-{backend}.pt"
                trained = execute(
                    args.binary, method, backend, artifact, replay=False
                )
                replayed = execute(
                    args.binary, method, backend, artifact, replay=True
                )
                digest = hashlib.sha256(artifact.read_bytes()).hexdigest()
                if len(digest) != 64 or artifact.stat().st_size <= 0:
                    raise RuntimeError(f"{method}/{backend}: empty artifact")
                expected_device = "cpu" if backend == "cpu" else "cuda:0"
                if trained["actual_training_device"] != expected_device:
                    raise RuntimeError(
                        f"{method}/{backend}: actual device mismatch"
                    )
                if trained["optimizer_steps"] != 4:
                    raise RuntimeError(
                        f"{method}/{backend}: optimizer did not execute"
                    )
                if trained["training_physical_fes"] != 0:
                    raise RuntimeError(
                        f"{method}/{backend}: hidden physical FES"
                    )
                for field in (
                    "method_semantic_id",
                    "backend_id",
                    "training_schema_id",
                    "loss_contract_id",
                    "model_parameter_hash",
                    "canonical_inference_hash",
                ):
                    if trained[field] != replayed[field]:
                        raise RuntimeError(
                            f"{method}/{backend}: replay changed {field}"
                        )
                if trained["attributed_fraction"] < 0.95:
                    raise RuntimeError(
                        f"{method}/{backend}: stage attribution below 95%"
                    )
                results[backend] = trained
                checked += 1
            cpu = results["cpu"]
            for backend in ("gpu", "hybrid"):
                candidate = results[backend]
                maximum_absolute_error = max(
                    abs(left - right)
                    for left, right in zip(
                        cpu["canonical_inference_values"],
                        candidate["canonical_inference_values"],
                        strict=True,
                    )
                )
                if maximum_absolute_error > 1.0e-8:
                    raise RuntimeError(
                        f"{method}/{backend}: inference max abs error "
                        f"{maximum_absolute_error} exceeds 1e-8"
                    )
                for field in (
                    "canonical_inference_sum",
                    "canonical_inference_l2",
                    "final_loss",
                ):
                    if not math.isclose(
                        cpu[field],
                        candidate[field],
                        rel_tol=1.0e-7,
                        abs_tol=1.0e-9,
                    ):
                        raise RuntimeError(
                            f"{method}/{backend}: {field} outside tolerance"
                        )
    print(
        "hpc_learning_libtorch_backends_pass "
        f"executions={checked} methods=3 backends=cpu,gpu,hybrid "
        "artifact_replay=exact"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
