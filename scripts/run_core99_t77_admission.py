#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T77 Waffle H6 and 18-case five-repeat campaign
Paper DOI: 10.1016/j.energy.2020.118310.
Public source: no paper-linked author code or data archive found.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/long_t77.hpp and the frozen contract.
Resource rule: one ADE-GRNN optimization owns one persistent all-core team;
paper cases and repeats execute sequentially without nested oversubscription.
Claim boundary: academic paper-first reconstruction, not numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics
import subprocess
import time


CASES = tuple(
    f"t77_ws{scenario}_n{turbines}"
    for scenario in (1, 2)
    for turbines in (15, 20, 25, 30, 35, 40, 60, 80, 100)
)
H6_CASE = "t77_ws1_n100"
FORMAL_GENERATIONS = 3750
FORMAL_STAGE1 = 125
FORMAL_CANDIDATES = 150000
FORMAL_EXACT_FES = 77540
FORMAL_SURROGATE_INFERENCES = 145000


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def affinity() -> str:
    for line in Path("/proc/self/status").read_text().splitlines():
        if line.startswith("Cpus_allowed_list:"):
            return line.split(":", 1)[1].strip()
    return "unknown"


def run_one(
    *,
    binary: str,
    output: Path,
    source_commit: str,
    case_id: str,
    seed: int,
    workers: int,
    generations: int,
    stage1_generations: int,
) -> dict:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if (
            previous.get("source_commit") == source_commit
            and previous.get("seed") == seed
            and previous.get("requested_workers") == workers
            and previous.get("generations") == generations
            and previous.get("frozen_stage1_generations")
            == stage1_generations
        ):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        (
            binary,
            "--case",
            case_id,
            "--seed",
            str(seed),
            "--workers",
            str(workers),
            "--generations",
            str(generations),
            "--stage1-generations",
            str(stage1_generations),
            "--output",
            str(temporary),
        ),
        text=True,
        capture_output=True,
        timeout=6 * 60 * 60,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))["runs"][0]
    payload.update(
        {
            "case_id": case_id,
            "source_commit": source_commit,
            "frozen_stage1_generations": stage1_generations,
            "runner_wall_seconds": time.monotonic() - started,
            "cpu_affinity": affinity(),
        }
    )
    output.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.unlink()
    return payload


def expected_counts(generations: int, stage1: int) -> tuple[int, int, int]:
    candidates = 40 * generations
    exact_fes = 40 + 40 * stage1 + 20 * (generations - stage1)
    surrogate = 40 * (generations - stage1)
    return candidates, exact_fes, surrogate


def validate(
    row: dict,
    *,
    case_id: str,
    workers: int,
    generations: int,
    stage1: int,
) -> None:
    candidates, exact_fes, surrogate = expected_counts(
        generations, stage1
    )
    require(row["case_id"] == case_id, f"{case_id}: identity")
    require(
        row["method_semantic_id"]
        == "t77_ade_grnn_paper_first_declared_v1",
        f"{case_id}: method semantics",
    )
    require(
        row["protocol_semantic_id"]
        == "t77_18_cases_5_runs_pop40_gen3750_v1",
        f"{case_id}: protocol semantics",
    )
    require(
        row["requested_workers"] == workers,
        f"{case_id}: worker request",
    )
    if workers == 20:
        require(
            row["observed_workers"] == 20,
            f"{case_id}: all-core team not observed",
        )
    require(
        row["candidate_proposals"] == candidates
        and row["physical_exact_fes"] == exact_fes
        and row["surrogate_inferences"] == surrogate,
        f"{case_id}: lifecycle/FES accounting",
    )
    require(
        len(row["best_history_kw"]) == generations,
        f"{case_id}: convergence history",
    )
    require(
        row["best_evaluation"]["constraint_violation_m"] <= 1.0e-9,
        f"{case_id}: final feasibility",
    )
    require(
        math.isfinite(row["best_evaluation"]["expected_power_kw"])
        and row["best_evaluation"]["expected_power_kw"]
        >= row["initial_best_power_kw"] - 1.0e-10,
        f"{case_id}: retained objective",
    )
    require(bool(row["scientific_hash"]), f"{case_id}: scientific hash")


def median(rows: list[dict], field: str) -> float:
    return statistics.median(row[field] for row in rows)


def h6_probe(args: argparse.Namespace, root: Path) -> dict:
    h6_generations = 130
    h6_stage1 = 125
    rows: dict[int, list[dict]] = {1: [], 20: []}
    for workers in (1, args.total_workers):
        run_one(
            binary=args.binary,
            output=root / "h6" / f"warmup-w{workers:02d}.json",
            source_commit=args.source_commit,
            case_id=H6_CASE,
            seed=args.seed_base - 100,
            workers=workers,
            generations=h6_generations,
            stage1_generations=h6_stage1,
        )
        for observation in range(1, args.h6_observations + 1):
            row = run_one(
                binary=args.binary,
                output=(
                    root / "h6"
                    / f"observation-{observation:02d}-w{workers:02d}.json"
                ),
                source_commit=args.source_commit,
                case_id=H6_CASE,
                seed=args.seed_base - 100 + observation,
                workers=workers,
                generations=h6_generations,
                stage1_generations=h6_stage1,
            )
            validate(
                row,
                case_id=H6_CASE,
                workers=workers,
                generations=h6_generations,
                stage1=h6_stage1,
            )
            rows[workers].append(row)
    for serial, parallel in zip(
        rows[1], rows[args.total_workers], strict=True
    ):
        require(
            serial["scientific_hash"] == parallel["scientific_hash"],
            "T77 H6 one/all-core scientific trajectory mismatch",
        )
    serial = rows[1]
    parallel = rows[args.total_workers]
    serial_exact = median(serial, "exact_evaluator_seconds")
    parallel_exact = median(parallel, "exact_evaluator_seconds")
    serial_surrogate = median(serial, "surrogate_seconds")
    parallel_surrogate = median(parallel, "surrogate_seconds")
    serial_total = median(serial, "end_to_end_seconds")
    parallel_total = median(parallel, "end_to_end_seconds")
    require(
        parallel_exact < serial_exact
        and parallel_surrogate < serial_surrogate
        and parallel_total < serial_total,
        "T77 all-core exact/surrogate/end-to-end path did not accelerate",
    )
    receipt = {
        "status": "pass",
        "case_id": H6_CASE,
        "observation_count": args.h6_observations,
        "cpu_affinity": affinity(),
        "serial": serial,
        "parallel": parallel,
        "median_seconds": {
            "serial_exact_evaluator": serial_exact,
            "parallel_exact_evaluator": parallel_exact,
            "serial_surrogate": serial_surrogate,
            "parallel_surrogate": parallel_surrogate,
            "serial_operator": median(serial, "operator_seconds"),
            "parallel_operator": median(parallel, "operator_seconds"),
            "serial_end_to_end": serial_total,
            "parallel_end_to_end": parallel_total,
        },
        "median_speedup": {
            "exact_evaluator": serial_exact / parallel_exact,
            "surrogate": serial_surrogate / parallel_surrogate,
            "end_to_end": serial_total / parallel_total,
        },
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, trajectory, "
            "candidate proposals and physical exact FES; one versus all "
            "twenty Waffle CPU workers",
    }
    (root / "h6").mkdir(parents=True, exist_ok=True)
    (root / "h6" / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def summarize(rows: list[dict]) -> dict:
    powers = [
        row["best_evaluation"]["expected_power_kw"] for row in rows
    ]
    return {
        "repeat_count": len(rows),
        "mean_power_kw": statistics.fmean(powers),
        "sample_standard_deviation_power_kw": statistics.stdev(powers),
        "minimum_power_kw": min(powers),
        "median_power_kw": statistics.median(powers),
        "maximum_power_kw": max(powers),
        "total_candidate_proposals": sum(
            row["candidate_proposals"] for row in rows
        ),
        "total_physical_exact_fes": sum(
            row["physical_exact_fes"] for row in rows
        ),
        "median_runner_wall_seconds": statistics.median(
            row["runner_wall_seconds"] for row in rows
        ),
        "scientific_hashes": [row["scientific_hash"] for row in rows],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=5)
    parser.add_argument("--repeat-count", type=int, default=5)
    parser.add_argument("--seed-base", type=int, default=2026077700)
    args = parser.parse_args()
    require(args.total_workers == 20, "T77 Waffle requires all 20 cores")
    require(args.h6_observations == 5, "T77 H6 requires five observations")
    require(args.repeat_count == 5, "T77 paper protocol requires five runs")
    require(
        expected_counts(FORMAL_GENERATIONS, FORMAL_STAGE1)
        == (
            FORMAL_CANDIDATES,
            FORMAL_EXACT_FES,
            FORMAL_SURROGATE_INFERENCES,
        ),
        "T77 frozen paper-budget arithmetic",
    )
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    h6 = h6_probe(args, root)
    results: dict[str, list[dict]] = {}
    completed = 0
    for case_index, case_id in enumerate(CASES):
        case_rows = []
        for repeat in range(1, args.repeat_count + 1):
            row = run_one(
                binary=args.binary,
                output=(
                    root / "formal" / case_id
                    / f"repeat-{repeat:02d}.json"
                ),
                source_commit=args.source_commit,
                case_id=case_id,
                seed=args.seed_base + case_index * 100 + repeat,
                workers=args.total_workers,
                generations=FORMAL_GENERATIONS,
                stage1_generations=FORMAL_STAGE1,
            )
            validate(
                row,
                case_id=case_id,
                workers=20,
                generations=FORMAL_GENERATIONS,
                stage1=FORMAL_STAGE1,
            )
            case_rows.append(row)
            completed += 1
            print(
                f"T77 completed {completed}/{len(CASES)*args.repeat_count}",
                flush=True,
            )
        results[case_id] = case_rows
    summary = {
        "campaign":
            "T77 Waffle H6 and 18-case five-repeat paper campaign",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "formal_case_count": len(CASES),
        "repeat_count_per_case": args.repeat_count,
        "formal_run_count": len(CASES) * args.repeat_count,
        "candidate_proposals_per_run": FORMAL_CANDIDATES,
        "physical_exact_fes_per_run": FORMAL_EXACT_FES,
        "surrogate_inferences_per_run": FORMAL_SURROGATE_INFERENCES,
        "maximum_aggregate_cpu_workers": 20,
        "resource_mapping":
            "one paper run at a time with one persistent all-twenty-core "
            "team; no case- or repeat-level nested oversubscription",
        "h6": h6,
        "case_summaries": {
            case_id: summarize(results[case_id]) for case_id in CASES
        },
        "campaign_wall_seconds": time.monotonic() - started,
        "status": "pass",
        "claim_boundary":
            "academic paper-equation and lifecycle reconstruction; not "
            "author code, raw-data, random-bitstream or numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
