#!/usr/bin/env python3
"""Run the frozen T01/T02 paper-native 25-seed pure-C++ campaign.

FACT DECLARATION
Papers: T01 Mosetti, DOI 10.1016/0167-6105(94)90080-9; T02 Grady,
DOI 10.1016/j.renene.2004.05.007.
Scientific contract: shared/contracts/core99_mosetti_grady_cases.json.
Execution: one pure-C++ optimization at a time, each using every requested
logical CPU through the persistent worker team.
Claim boundary: formal quality observations for the project's declared
academic reproductions, not author-exact numerical reproductions.
END FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CASES = (
    ("T01", "t01_mosetti_ga", "t01_mosetti_case_a"),
    ("T01", "t01_mosetti_ga", "t01_mosetti_case_b"),
    ("T01", "t01_mosetti_ga", "t01_mosetti_case_c"),
    ("T02", "t02_grady_island_ga", "t02_grady_case_a"),
    ("T02", "t02_grady_island_ga", "t02_grady_case_b"),
    (
        "T02",
        "t02_grady_island_ga",
        "t02_grady_case_c_body1000",
    ),
    (
        "T02",
        "t02_grady_island_ga",
        "t02_grady_case_c_abstract2500",
    ),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def valid_existing(
    path: Path,
    algorithm: str,
    problem: str,
    seed: int,
    workers: int,
) -> bool:
    if not path.is_file():
        return False
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        data.get("algorithm_id") == algorithm
        and data.get("problem_id") == problem
        and data.get("requested_workers") == workers
        and data.get("seed") == seed
        and data.get("physical_fes", 0) > 0
        and data.get("best_objective", 0) > 0
        and bool(data.get("scientific_hash"))
        and seed > 0
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026073100)
    args = parser.parse_args()
    if args.workers <= 0 or args.repeats <= 0:
        raise ValueError("workers and repeats must be positive")

    source_contract = ROOT / "shared/contracts/core99_mosetti_grady_cases.json"
    records = []
    for corpus_id, algorithm, problem in CASES:
        case_root = args.output_root / corpus_id / problem
        case_root.mkdir(parents=True, exist_ok=True)
        for repeat in range(1, args.repeats + 1):
            seed = args.seed_base + repeat
            output = case_root / f"seed-{repeat:02d}.json"
            reused = valid_existing(
                output,
                algorithm,
                problem,
                seed,
                args.workers,
            )
            if not reused:
                subprocess.run(
                    [
                        str(args.binary),
                        "--algorithm",
                        algorithm,
                        "--problem",
                        problem,
                        "--seed",
                        str(seed),
                        "--workers",
                        str(args.workers),
                        "--output",
                        str(output),
                    ],
                    check=True,
                )
            records.append(
                {
                    "corpus_id": corpus_id,
                    "algorithm_id": algorithm,
                    "problem_id": problem,
                    "repeat": repeat,
                    "seed": seed,
                    "workers": args.workers,
                    "result": str(output.relative_to(args.output_root)),
                    "result_sha256": sha256(output),
                    "reused": reused,
                }
            )
            print(
                f"formal_progress completed={len(records)}/"
                f"{len(CASES) * args.repeats} "
                f"paper={corpus_id} problem={problem} seed={seed}",
                flush=True,
            )

    manifest = {
        "schema_version": 1,
        "campaign_id": "core99_t01_t02_paper_native_25seed_v1",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "source_contract": str(source_contract.relative_to(ROOT)),
        "source_contract_sha256": sha256(source_contract),
        "workers_per_optimization": args.workers,
        "seed_base": args.seed_base,
        "repeats": args.repeats,
        "paper_case_count": len(CASES),
        "observation_count": len(records),
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
