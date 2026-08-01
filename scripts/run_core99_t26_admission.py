#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T26 CPU/CUDA H6, training and 26-run campaign
Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
Public source provenance, Missing facts, Reconstruction, semantic IDs,
production backend, controlling Contract and Claim boundary:
hpc/t26_libtorch/include/core99/li_t26.hpp.
Resource rule: measure one versus all Waffle CPU cores on paper-population
work, probe CUDA without displacing other work, then train once from scratch
and execute one native plus 25 explicitly labelled robustness seeds only on
the fastest admitted complete target backend.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import time


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def invoke(
    binary: Path,
    arguments: list[str],
    output: Path,
    source_commit: str,
    timeout: float,
    artifact: Path | None = None,
) -> dict:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        artifact_ok = artifact is None or (
            artifact.exists()
            and previous.get("artifact_sha256") == sha256(artifact)
        )
        if previous.get("source_commit") == source_commit and artifact_ok:
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    completed = subprocess.run(
        [str(binary), *arguments], text=True, capture_output=True, timeout=timeout
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(completed.stdout)
    payload.update(
        {
            "source_commit": source_commit,
            "binary_sha256": sha256(binary),
            "command_arguments": arguments,
            "runner_wall_seconds": time.monotonic() - started,
            "completed_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    if artifact is not None and artifact.exists():
        payload["artifact_sha256"] = sha256(artifact)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n",
                         encoding="utf-8")
    temporary.replace(output)
    return payload


def train(
    args: argparse.Namespace,
    root: Path,
    backend: str,
    name: str,
    iterations: int,
    batch: int,
    workers: int,
    timeout: float,
) -> tuple[dict, Path]:
    artifact = root / "artifacts" / f"{name}.pt"
    payload = invoke(
        args.binary,
        ["--action", "train", "--backend", backend, "--artifact", str(artifact),
         "--iterations", str(iterations), "--batch-size", str(batch),
         "--workers", str(workers), "--seed", "26001"],
        root / "training" / f"{name}.json", args.source_commit, timeout, artifact,
    )
    return payload, artifact


def optimize(
    args: argparse.Namespace,
    root: Path,
    artifact: Path,
    backend: str,
    workers: int,
    seed: int,
    generations: int,
    population: int,
    name: str,
    timeout: float,
) -> dict:
    return invoke(
        args.binary,
        ["--action", "optimize", "--backend", backend, "--artifact", str(artifact),
         "--generations", str(generations), "--population", str(population),
         "--workers", str(workers), "--seed", str(seed)],
        root / "optimization" / f"{name}.json", args.source_commit, timeout, artifact,
    )


def h6(args: argparse.Namespace, root: Path) -> tuple[str, Path]:
    _, artifact = train(args, root, "cpu", "h6-cpu", 100, 256,
                        args.total_workers, 1800)
    observations: dict[int, list[dict]] = {1: [], args.total_workers: []}
    for workers in observations:
        for observation in range(args.h6_observations):
            observations[workers].append(optimize(
                args, root / "h6", artifact, "cpu", workers, 26001,
                2, 300, f"o{observation + 1:02d}-w{workers:02d}", 1800,
            ))
    one = statistics.median(item["seconds"] for item in observations[1])
    all_core = statistics.median(
        item["seconds"] for item in observations[args.total_workers]
    )
    one_science = {(item["final_aep_gwh"], item["final_fitness"])
                   for item in observations[1]}
    all_science = {(item["final_aep_gwh"], item["final_fitness"])
                   for item in observations[args.total_workers]}
    if one_science != all_science:
        raise RuntimeError("T26 one/all-core H6 science differs")
    if not all_core < one:
        raise RuntimeError("T26 all-core CPU implementation did not accelerate")
    selected = "cpu"
    try:
        train(args, root / "backend-selection", "cuda", "cuda-probe",
              20, 256, args.total_workers, 1800)
        cuda = optimize(args, root / "backend-selection", artifact, "cuda",
                        args.total_workers, 26001, 2, 300, "cuda", 1800)
        cpu = optimize(args, root / "backend-selection", artifact, "cpu",
                       args.total_workers, 26001, 2, 300, "cpu", 1800)
        if abs(cuda["final_aep_gwh"] - cpu["final_aep_gwh"]) > 0.05:
            raise RuntimeError("T26 CUDA and CPU objective differ")
        if cuda["seconds"] < cpu["seconds"]:
            selected = "cuda"
    except (RuntimeError, subprocess.TimeoutExpired):
        selected = "cpu"
    summary = {
        "schema_version": 1,
        "corpus_id": "T26",
        "source_commit": args.source_commit,
        "one_worker_median_seconds": one,
        "all_worker_median_seconds": all_core,
        "all_vs_one_speedup": one / all_core,
        "workers": args.total_workers,
        "observations": args.h6_observations,
        "objective_identity": True,
        "selected_backend": selected,
    }
    path = root / "h6" / "summary.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return selected, artifact


def formal(args: argparse.Namespace, root: Path, backend: str) -> None:
    training, artifact = train(
        args, root / "formal", backend, "paper-profile", 10000, 1024,
        args.total_workers, 43200,
    )
    if training["iterations"] != 10000:
        raise RuntimeError("T26 paper-profile training is incomplete")
    seeds = [("native", 26001)] + [("robustness", 26100 + index) for index in range(25)]
    for label, seed in seeds:
        result = optimize(
            args, root / "formal", artifact, backend, args.total_workers,
            seed, 1000, 300, f"{label}-seed{seed}", 172800,
        )
        if result["physical_fes"] != 300300:
            raise RuntimeError(f"T26 formal FES differs for seed {seed}")
    manifest = {
        "schema_version": 1,
        "corpus_id": "T26",
        "source_commit": args.source_commit,
        "backend": backend,
        "artifact": str(artifact),
        "artifact_sha256": sha256(artifact),
        "native_runs": 1,
        "declared_robustness_runs": 25,
        "formal_runs": 26,
        "population": 300,
        "generations": 1000,
        "physical_fes_per_run": 300300,
    }
    (root / "formal" / "formal-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=3)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    selected = "cpu"
    if args.stage in ("h6", "all"):
        selected, _ = h6(args, args.output_root)
    elif (args.output_root / "h6" / "summary.json").exists():
        selected = json.loads((args.output_root / "h6" / "summary.json").read_text())["selected_backend"]
    if args.stage in ("formal", "all"):
        formal(args, args.output_root, selected)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
