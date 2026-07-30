#!/usr/bin/env python3
"""H6 throughput probe for the fine-grained T04 pure-C++ reproduction.

FACT DECLARATION
Paper/DOI: T04 UWFLO, 10.1016/j.renene.2011.09.017.
Reason for seed-level concurrency: one paper-scale pure-C++ PSO run contains
too little work per population evaluation to amortize a 20-way internal
barrier. This probe compares the same 20 deterministic seeds sequentially and
with 20 concurrent one-worker processes, thereby using every Waffle logical
CPU without altering any algorithm trajectory.
Claim boundary: bounded throughput admission, not formal paper timing.
END FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path


def run_one(
    binary: Path,
    problem: str,
    seed: int,
    output: Path,
) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(binary),
            "--problem",
            problem,
            "--seed",
            str(seed),
            "--workers",
            "1",
            "--output",
            str(output),
        ],
        check=True,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument(
        "--problem",
        default="t04_uwflo_case3_i_n18",
    )
    parser.add_argument("--seed-base", type=int, default=2026073100)
    parser.add_argument("--count", type=int, default=20)
    parser.add_argument("--concurrency", type=int, default=20)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--host", default="waffle")
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    seeds = [args.seed_base + index for index in range(1, args.count + 1)]

    sequential_start = time.perf_counter()
    sequential = [
        run_one(
            args.binary,
            args.problem,
            seed,
            args.output_root / "sequential" / f"seed-{seed}.json",
        )
        for seed in seeds
    ]
    sequential_wall = time.perf_counter() - sequential_start

    concurrent_start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as pool:
        concurrent = list(
            pool.map(
                lambda seed: run_one(
                    args.binary,
                    args.problem,
                    seed,
                    args.output_root
                    / "concurrent"
                    / f"seed-{seed}.json",
                ),
                seeds,
            )
        )
    concurrent_wall = time.perf_counter() - concurrent_start

    pairs = []
    for seed, serial, parallel in zip(seeds, sequential, concurrent):
        if serial["scientific_hash"] != parallel["scientific_hash"]:
            raise RuntimeError(f"seed {seed}: scientific hash mismatch")
        if serial["best_farm_efficiency"] != parallel["best_farm_efficiency"]:
            raise RuntimeError(f"seed {seed}: objective mismatch")
        pairs.append(
            {
                "seed": seed,
                "scientific_hash": serial["scientific_hash"],
                "best_farm_efficiency": serial["best_farm_efficiency"],
                "best_constraint_violation": serial[
                    "best_constraint_violation"
                ],
                "physical_fes": serial["physical_fes"],
            }
        )
    receipt = {
        "schema_version": 1,
        "receipt_type": "core99_t04_h6_throughput_admission",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "host": args.host,
        "source_commit": args.source_commit,
        "problem_id": args.problem,
        "seed_count": args.count,
        "workers_per_optimization": 1,
        "concurrent_optimizations": args.concurrency,
        "sequential_wall_seconds": sequential_wall,
        "concurrent_wall_seconds": concurrent_wall,
        "throughput_speedup": sequential_wall / concurrent_wall,
        "scientific_equivalence": "exact_hash_and_objective",
        "observations": pairs,
        "claim_boundary": (
            "bounded full-resource throughput admission; not formal timing"
        ),
    }
    canonical = json.dumps(receipt, sort_keys=True).encode()
    receipt["receipt_sha256"] = hashlib.sha256(canonical).hexdigest()
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "core99_t04_throughput_h6_pass "
        f"seeds={args.count} concurrency={args.concurrency} "
        f"speedup={receipt['throughput_speedup']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
