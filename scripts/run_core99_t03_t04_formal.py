#!/usr/bin/env python3
"""Run frozen paper-native T03/T04 25-seed pure-C++ campaigns.

FACT DECLARATION
T03 DOI 10.1016/j.renene.2009.08.019 uses one 20-worker optimization
at a time because its direction/Weibull evaluator has sufficient internal
parallel grain. T04 DOI 10.1016/j.renene.2011.06.033 uses 20 concurrent
one-worker optimizations because its tiny per-layout kernel is throughput
limited. Both modes occupy all Waffle logical CPUs without changing a
scientific trajectory. Formal results are project-declared academic
reproductions, not author-exact numerical reproductions.
END FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import UTC, datetime
from pathlib import Path


T03_CASES = tuple(
    f"t03_kusiak_s{scenario}_n{count}"
    for scenario in (1, 2)
    for count in range(2, 7)
)
T04_CASES = (
    "t04_uwflo_case1_n9",
    "t04_uwflo_case2_n9",
    "t04_uwflo_case3_i_n6",
    "t04_uwflo_case3_i_n9",
    "t04_uwflo_case3_i_n12",
    "t04_uwflo_case3_i_n15",
    "t04_uwflo_case3_i_n18",
    "t04_uwflo_case3_ii_f1",
    "t04_uwflo_case3_ii_f2",
    "t04_uwflo_case3_ii_f3",
    "t04_uwflo_case3_ii_f4",
    "t04_uwflo_case3_ii_f5",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid(
    output: Path,
    algorithm: str,
    problem: str,
    seed: int,
    workers: int,
) -> bool:
    if not output.is_file():
        return False
    try:
        value = json.loads(output.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        value.get("algorithm_id") == algorithm
        and value.get("problem_id") == problem
        and value.get("seed") == seed
        and value.get("requested_workers") == workers
        and value.get("physical_fes", 0) > 0
        and bool(value.get("scientific_hash"))
    )


def run_one(
    binary: Path,
    output_root: Path,
    corpus_id: str,
    algorithm: str,
    problem: str,
    repeat: int,
    seed: int,
    workers: int,
) -> dict:
    output = (
        output_root
        / corpus_id
        / problem
        / f"seed-{repeat:02d}.json"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    reused = valid(output, algorithm, problem, seed, workers)
    if not reused:
        subprocess.run(
            [
                str(binary),
                "--problem",
                problem,
                "--seed",
                str(seed),
                "--workers",
                str(workers),
                "--output",
                str(output),
            ],
            check=True,
        )
    return {
        "corpus_id": corpus_id,
        "algorithm_id": algorithm,
        "problem_id": problem,
        "repeat": repeat,
        "seed": seed,
        "workers_per_optimization": workers,
        "result": str(output.relative_to(output_root)),
        "result_sha256": sha256(output),
        "reused": reused,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--t03-binary", type=Path, required=True)
    parser.add_argument("--t04-binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026073100)
    parser.add_argument("--logical-cpus", type=int, default=20)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()

    records = []
    total = (len(T03_CASES) + len(T04_CASES)) * args.repeats
    for problem in T03_CASES:
        for repeat in range(1, args.repeats + 1):
            record = run_one(
                args.t03_binary,
                args.output_root,
                "T03",
                "t03_kusiak_spea_es",
                problem,
                repeat,
                args.seed_base + repeat,
                args.logical_cpus,
            )
            records.append(record)
            print(
                f"formal_progress completed={len(records)}/{total} "
                f"paper=T03 problem={problem} repeat={repeat}",
                flush=True,
            )

    jobs = [
        (problem, repeat)
        for problem in T04_CASES
        for repeat in range(1, args.repeats + 1)
    ]
    with ThreadPoolExecutor(max_workers=args.logical_cpus) as pool:
        futures = {
            pool.submit(
                run_one,
                args.t04_binary,
                args.output_root,
                "T04",
                "t04_uwflo_constrained_pso",
                problem,
                repeat,
                args.seed_base + repeat,
                1,
            ): (problem, repeat)
            for problem, repeat in jobs
        }
        for future in as_completed(futures):
            record = future.result()
            records.append(record)
            print(
                f"formal_progress completed={len(records)}/{total} "
                f"paper=T04 problem={record['problem_id']} "
                f"repeat={record['repeat']}",
                flush=True,
            )
    records.sort(
        key=lambda row: (
            row["corpus_id"],
            row["problem_id"],
            row["repeat"],
        )
    )
    manifest = {
        "schema_version": 1,
        "campaign_id": "core99_t03_t04_paper_native_25seed_v1",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "source_commit": args.source_commit,
        "repeats": args.repeats,
        "logical_cpus": args.logical_cpus,
        "paper_count": 2,
        "paper_case_count": len(T03_CASES) + len(T04_CASES),
        "observation_count": len(records),
        "execution_policies": {
            "T03": {
                "workers_per_optimization": args.logical_cpus,
                "concurrent_optimizations": 1
            },
            "T04": {
                "workers_per_optimization": 1,
                "concurrent_optimizations": args.logical_cpus
            }
        },
        "records": records,
        "claim_boundary": (
            "formal project-declared reproduction results; not author-exact "
            "numerical reproduction"
        ),
    }
    manifest_path = args.output_root / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"formal_complete manifest={manifest_path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
