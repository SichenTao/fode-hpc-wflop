#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T27 Waffle H6 and paper-native campaign
Paper DOI: 10.1145/3711896.3737181
Public source: https://github.com/dbsxodud-11/layopt at revision 19ff389;
FLORIS 4.1.1 at 2c3be8f.
Missing/conflicts/resolution/HPC/claim boundary:
hpc/t27_libtorch/include/core99/shin_t27.hpp and the frozen contract.
Resource rule: H6 uses all available CPU cores; paper-scale learning uses a
CUDA backend only. If no CUDA device is available, a machine-readable deferred
receipt is written instead of silently replacing the paper protocol with an
infeasible CPU run.
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


def affinity() -> str:
    for line in Path("/proc/self/status").read_text().splitlines():
        if line.startswith("Cpus_allowed_list:"):
            return line.split(":", 1)[1].strip()
    return "unknown"


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
        [binary, *arguments],
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(completed.stdout)
    payload.update(
        {
            "source_commit": source_commit,
            "command_arguments": arguments,
            "runner_wall_seconds": time.monotonic() - started,
            "cpu_affinity": affinity(),
            "completed_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def cuda_available(binary: str) -> bool:
    completed = subprocess.run(
        [
            binary,
            "--mode",
            "optimize",
            "--backend",
            "cuda",
            "--turbines",
            "4",
            "--side-length-m",
            "2000",
            "--initial-layouts",
            "2",
            "--rounds",
            "0",
            "--generated",
            "1",
            "--training-steps",
            "0",
            "--batch-size",
            "2",
            "--repair-steps",
            "0",
            "--sample-steps",
            "2",
            "--hidden-width",
            "8",
            "--workers",
            "1",
        ],
        text=True,
        capture_output=True,
        timeout=120,
    )
    return completed.returncode == 0


def h6(args: argparse.Namespace, root: Path) -> dict:
    rows: dict[int, list[dict]] = {1: [], args.total_workers: []}
    for workers in rows:
        for observation in range(1, args.h6_observations + 1):
            rows[workers].append(
                invoke(
                    args.binary,
                    [
                        "--mode",
                        "evaluator-throughput",
                        "--backend",
                        "cpu",
                        "--turbines",
                        "30",
                        "--side-length-m",
                        "3000",
                        "--initial-layouts",
                        "10000",
                        "--workers",
                        str(workers),
                        "--wind-speed",
                        "8",
                        "--wind-direction",
                        "60",
                    ],
                    root
                    / "h6"
                    / f"evaluator-o{observation:02d}-w{workers:02d}.json",
                    args.source_commit,
                    600,
                )
            )
    serial = statistics.median(row["seconds"] for row in rows[1])
    parallel = statistics.median(
        row["seconds"] for row in rows[args.total_workers]
    )
    serial_hashes = {row["aep_checksum_mwh"] for row in rows[1]}
    parallel_hashes = {
        row["aep_checksum_mwh"] for row in rows[args.total_workers]
    }
    if serial_hashes != parallel_hashes:
        raise RuntimeError("T27 evaluator checksum differs by worker count")
    if not parallel < serial:
        raise RuntimeError("T27 all-core evaluator did not accelerate")
    summary = {
        "schema_version": 1,
        "corpus_id": "T27",
        "source_commit": args.source_commit,
        "one_worker_median_seconds": serial,
        "all_worker_median_seconds": parallel,
        "all_vs_one_speedup": serial / parallel,
        "workers": args.total_workers,
        "observations": args.h6_observations,
        "cuda_available": cuda_available(args.binary),
    }
    path = root / "h6" / "summary.json"
    path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


def formal_specs() -> list[tuple[str, list[str]]]:
    specs: list[tuple[str, list[str]]] = []
    common = [
        "--mode",
        "optimize",
        "--backend",
        "cuda",
        "--initial-layouts",
        "5000",
        "--rounds",
        "10",
        "--generated",
        "1000",
        "--training-steps",
        "10000",
        "--batch-size",
        "256",
        "--repair-steps",
        "1000",
        "--sample-steps",
        "128",
        "--hidden-width",
        "1024",
        "--workers",
        "20",
        "--activation",
        "paper",
    ]
    for seed in range(3):
        for turbines in (30, 40, 50):
            specs.append(
                (
                    f"fixed-n{turbines}-seed{seed}",
                    [
                        *common,
                        "--model-type",
                        "gnn",
                        "--turbines",
                        str(turbines),
                        "--side-length-m",
                        str(turbines * 100),
                        "--seed",
                        str(seed),
                        "--wind-speed",
                        "8",
                        "--wind-direction",
                        "60",
                    ],
                )
            )
        for speed, direction in ((8, 60), (10, 120), (12, 150)):
            specs.append(
                (
                    f"diverse-u{speed}-d{direction}-seed{seed}",
                    [
                        *common,
                        "--model-type",
                        "gnn",
                        "--turbines",
                        "30",
                        "--side-length-m",
                        "3000",
                        "--seed",
                        str(seed),
                        "--wind-speed",
                        str(speed),
                        "--wind-direction",
                        str(direction),
                        "--diverse-training",
                        "true",
                    ],
                )
            )
        specs.append(
            (
                f"transfer-30to40-50-seed{seed}",
                [
                    *common,
                    "--model-type",
                    "gnn",
                    "--turbines",
                    "30",
                    "--side-length-m",
                    "3000",
                    "--seed",
                    str(seed),
                    "--wind-speed",
                    "8",
                    "--wind-direction",
                    "60",
                    "--transfer-counts",
                    "40,50",
                ],
            )
        )
        specs.append(
            (
                f"architecture-mlp-n30-seed{seed}",
                [
                    *common,
                    "--model-type",
                    "mlp",
                    "--turbines",
                    "30",
                    "--side-length-m",
                    "3000",
                    "--seed",
                    str(seed),
                ],
            )
        )
        for guidance in (1, 2, 4, 5):
            specs.append(
                (
                    f"guidance-{guidance}-seed{seed}",
                    [
                        *common,
                        "--model-type",
                        "gnn",
                        "--turbines",
                        "30",
                        "--side-length-m",
                        "3000",
                        "--seed",
                        str(seed),
                        "--guidance",
                        str(guidance),
                    ],
                )
            )
    return specs


def formal(args: argparse.Namespace, root: Path, has_cuda: bool) -> None:
    specs = formal_specs()
    manifest = {
        "schema_version": 1,
        "corpus_id": "T27",
        "source_commit": args.source_commit,
        "protocol_semantic_id": "shin2025_three_seed_paper_cases_v1",
        "run_count": len(specs),
        "cuda_required": True,
        "cuda_available": has_cuda,
        "runs": [name for name, _ in specs],
    }
    (root / "formal").mkdir(parents=True, exist_ok=True)
    (root / "formal" / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    if not has_cuda:
        (root / "formal" / "DEFERRED_NO_CUDA.json").write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "corpus_id": "T27",
                    "reason": "paper_scale_training_requires_cuda",
                    "source_commit": args.source_commit,
                    "run_count": len(specs),
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        return
    for name, arguments in specs:
        invoke(
            args.binary,
            arguments,
            root / "formal" / f"{name}.json",
            args.source_commit,
            12 * 3600,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=5)
    parser.add_argument("--phase", choices=("h6", "formal", "all"), default="all")
    args = parser.parse_args()
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    summary = None
    if args.phase in {"h6", "all"}:
        summary = h6(args, root)
    has_cuda = (
        summary["cuda_available"]
        if summary is not None
        else cuda_available(args.binary)
    )
    if args.phase in {"formal", "all"}:
        formal(args, root, has_cuda)
    print(
        json.dumps(
            {
                "corpus_id": "T27",
                "phase": args.phase,
                "cuda_available": has_cuda,
                "status": "completed_or_formally_deferred",
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
