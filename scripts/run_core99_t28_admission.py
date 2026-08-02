#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T28 Waffle H6 and paper-native campaign
Paper DOI: 10.5194/wes-10-1661-2025
Public source/data: WINDFLOWER v1.0.0, DOI 10.5281/zenodo.13946931.
Missing/conflicts/resolution/HPC/claim boundary:
hpc/t28_libtorch/include/core99/nguyen_t28.hpp and the frozen contract.
Resource rule: H6 measures one worker only as a limited reference and uses all
20 Waffle CPU workers for the production comparison. Formal optimization uses
CUDA when admitted and otherwise the all-core CPU backend.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import time


def invoke(
    binary: str,
    arguments: list[str],
    output: Path,
    source_commit: str,
    timeout: float,
) -> dict:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if previous.get("source_commit") == source_commit:
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    completed = subprocess.run(
        [binary, *arguments], text=True, capture_output=True, timeout=timeout
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(completed.stdout)
    payload.update(
        {
            "source_commit": source_commit,
            "command_arguments": arguments,
            "runner_wall_seconds": time.monotonic() - started,
            "completed_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def h6(args: argparse.Namespace, root: Path) -> None:
    measurements: dict[int, list[dict]] = {1: [], args.total_workers: []}
    for workers in measurements:
        for observation in range(args.h6_observations):
            measurements[workers].append(
                invoke(
                    args.binary,
                    [
                        "--data", args.data, "--backend", "cpu",
                        "--objective", "jerm", "--year", "2023",
                        "--evaluation-year", "2023", "--samples", "150",
                        "--forecasts", "10", "--iterations", "2",
                        "--evaluation-limit", "512", "--workers", str(workers),
                        "--seed", "0", "--reserve-limit", "50",
                    ],
                    root / "h6" / f"o{observation + 1:02d}-w{workers:02d}.json",
                    args.source_commit, 900,
                )
            )
    one = statistics.median(row["seconds"] for row in measurements[1])
    all_core = statistics.median(
        row["seconds"] for row in measurements[args.total_workers]
    )
    one_values = {row["objective_value"] for row in measurements[1]}
    all_values = {
        row["objective_value"] for row in measurements[args.total_workers]
    }
    if one_values != all_values:
        raise RuntimeError("T28 objective differs by CPU worker count")
    if not all_core < one:
        raise RuntimeError("T28 all-core implementation did not accelerate")
    summary = {
        "schema_version": 1,
        "corpus_id": "T28",
        "source_commit": args.source_commit,
        "one_worker_median_seconds": one,
        "all_worker_median_seconds": all_core,
        "all_vs_one_speedup": one / all_core,
        "workers": args.total_workers,
        "observations": args.h6_observations,
        "objective_identity": True,
    }
    path = root / "h6" / "summary.json"
    path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")


def select_backend(args: argparse.Namespace, root: Path) -> str:
    probe = [
        "--data", args.data, "--objective", "jerm", "--year", "2023",
        "--evaluation-year", "2023", "--samples", "150", "--forecasts", "10",
        "--iterations", "2", "--evaluation-limit", "512", "--workers",
        str(args.total_workers), "--seed", "918273", "--reserve-limit", "50",
    ]
    cpu = invoke(
        args.binary, ["--backend", "cpu", *probe],
        root / "backend-selection" / "cpu.json", args.source_commit, 900,
    )
    try:
        cuda = invoke(
            args.binary, ["--backend", "cuda", *probe],
            root / "backend-selection" / "cuda.json", args.source_commit, 900,
        )
    except (RuntimeError, subprocess.TimeoutExpired):
        return "cpu"
    return "cuda" if cuda["seconds"] < cpu["seconds"] else "cpu"


def formal_specs(backend: str) -> list[tuple[str, list[str]]]:
    specs: list[tuple[str, list[str]]] = []
    common = [
        "--backend", backend, "--year", "2023", "--forecasts", "10",
        "--iterations", "2000", "--workers", "20", "--learning-rate", "112",
    ]
    for seed in range(5):
        for objective in ("aep", "daem", "jerm"):
            for samples in (20, 50, 100, 150):
                specs.append(
                    (
                        f"{objective}-kt{samples}-seed{seed}",
                        [
                            *common, "--objective", objective,
                            "--evaluation-year", "2023", "--samples", str(samples),
                            "--seed", str(seed), "--reserve-limit", "50",
                        ],
                    )
                )
            specs.append(
                (
                    f"{objective}-kt150-unseen2024-seed{seed}",
                    [
                        *common, "--objective", objective,
                        "--evaluation-year", "2024", "--samples", "150",
                        "--seed", str(seed), "--reserve-limit", "50",
                    ],
                )
            )
        for reserve_limit in ("117", "221.4"):
            specs.append(
                (
                    f"jerm-kt150-r{reserve_limit}-seed{seed}",
                    [
                        *common, "--objective", "jerm",
                        "--evaluation-year", "2023", "--samples", "150",
                        "--seed", str(seed), "--reserve-limit", reserve_limit,
                    ],
                )
            )
    return specs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=3)
    parser.add_argument("--phase", choices=("h6", "formal", "all"), default="all")
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    if args.phase in ("h6", "all"):
        h6(args, args.output_root)
    if args.phase in ("formal", "all"):
        backend = select_backend(args, args.output_root)
        for name, extra in formal_specs(backend):
            invoke(
                args.binary,
                ["--data", args.data, *extra],
                args.output_root / "formal" / f"{name}.json",
                args.source_commit,
                172800,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
