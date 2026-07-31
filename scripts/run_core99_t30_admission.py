#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T30 Waffle H6 and paper-native campaign
Paper DOI: 10.1007/s10732-015-9283-4
Public source: none found; open author thesis 20.500.12608/17839.
Missing/conflicts/reconstruction/HPC/claim boundary:
hpc/core99_cpp/include/core99/fischetti_t30.hpp and the frozen contract.
Resource rule: one worker is used only for three limited H6 references.
All 350 production cases use every one of Waffle's 20 CPU workers.
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
            "completed_at": datetime.now(timezone.utc).isoformat(),
        }
    )
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return payload


def run_h6(args: argparse.Namespace) -> None:
    records: dict[int, list[dict]] = {1: [], args.total_workers: []}
    for workers in records:
        for observation in range(args.h6_observations):
            output = (
                args.output_root / "h6"
                / f"o{observation + 1:02d}-w{workers:02d}.json"
            )
            records[workers].append(
                invoke(
                    args.binary,
                    [
                        "--sites", "20000",
                        "--instance", "0",
                        "--workers", str(workers),
                        "--time-limit", "3",
                        "--fixed-moves", "80",
                        "--seed", "30",
                    ],
                    output,
                    args.source_commit,
                    900,
                )
            )
    one = statistics.median(
        row["matrix_seconds"] for row in records[1]
    )
    all_core = statistics.median(
        row["matrix_seconds"] for row in records[args.total_workers]
    )
    one_hashes = {row["scientific_hash"] for row in records[1]}
    all_hashes = {
        row["scientific_hash"] for row in records[args.total_workers]
    }
    if len(one_hashes) != 1 or one_hashes != all_hashes:
        raise RuntimeError("T30 fixed-work scientific result differs by worker count")
    if not all_core < one:
        raise RuntimeError("T30 all-core pair construction did not accelerate")
    summary = {
        "schema_version": 1,
        "corpus_id": "T30",
        "source_commit": args.source_commit,
        "region": "uncached 20000-site packed pair-matrix construction",
        "one_worker_median_seconds": one,
        "all_worker_median_seconds": all_core,
        "all_vs_one_speedup": one / all_core,
        "workers": args.total_workers,
        "observations": args.h6_observations,
        "fixed_work_scientific_identity": True,
    }
    (args.output_root / "h6" / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )


def formal_specs(
    root: Path,
    workers: int,
) -> list[tuple[str, list[str], float]]:
    specs: list[tuple[str, list[str], float]] = []
    checkpoints = (60, 300, 600, 900, 1200, 1800, 3600)
    for sites in (1000, 5000, 10000, 15000, 20000):
        for instance in range(10):
            seed = 300000 + sites + instance
            cache = root / "matrix-cache" / f"n{sites}-i{instance}.bin"
            for seconds in checkpoints:
                name = f"n{sites}-i{instance:02d}-t{seconds:04d}"
                specs.append(
                    (
                        name,
                        [
                            "--sites", str(sites),
                            "--instance", str(instance),
                            "--workers", str(workers),
                            "--time-limit", str(seconds),
                            "--seed", str(seed),
                            "--matrix-cache", str(cache),
                        ],
                        seconds + 1800,
                    )
                )
    return specs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=3)
    parser.add_argument("--phase", choices=("h6", "formal", "all"), default="all")
    args = parser.parse_args()
    args.output_root.mkdir(parents=True, exist_ok=True)
    if args.phase in ("h6", "all"):
        run_h6(args)
    if args.phase in ("formal", "all"):
        for name, arguments, timeout in formal_specs(
            args.output_root, args.total_workers
        ):
            invoke(
                args.binary,
                arguments,
                args.output_root / "formal" / f"{name}.json",
                args.source_commit,
                timeout,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
