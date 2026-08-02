#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T67 Waffle all-core H6 and paper-role campaign
Paper DOI: 10.1016/j.renene.2016.10.038
Public source: no target MATLAB source, full 61-turbine table, fitted curves,
or native random states were located.
Missing information and declared completion:
hpc/core99_cpp/include/core99/abdulrahman_t67.hpp
Formal protocol: three layouts by three spacing multipliers by three reference
speeds by two terrain classes by three separately optimized objectives by 25
platform-standard independent seeds = 4050 target runs. Each run follows the
paper's 3000-generation maximum and TolFun=1e-15 completion, with the declared
population and 50-generation stall window.
HPC protocol: H6 compares the same complete 729256 physical-layout trajectory
with one and all Waffle cores. Formal runs use one all-core process at a time.
Controlling contract: shared/contracts/core99_t67_abdulrahman_2017.json
Claim boundary: academic flexible declared reconstruction, not author source,
native commercial-turbine arrays, random streams, or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD_ID = "t67_matlab_ga_mixed_turbine_height_declared_v1"
PROBLEM_ID = "t67_til_swf_power_cf_tciop_162role_declared_v1"
PROTOCOL_ID = "t67_3000gen_25seed_162role_v1"
LAYOUTS = ("til", "array", "staggered")
SPACINGS = (3, 4, 5)
SPEEDS = (8, 10, 12)
TERRAINS = ("onshore", "offshore")
OBJECTIVES = ("max_power", "max_cf", "min_tciop")
CASES = tuple(
    (
        f"t67_{layout}_s{spacing}_u{speed}_{terrain}_{objective}",
        layout,
        spacing,
        speed,
        terrain,
        objective,
    )
    for layout in LAYOUTS
    for spacing in SPACINGS
    for speed in SPEEDS
    for terrain in TERRAINS
    for objective in OBJECTIVES
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def execute(
    binary: Path,
    output: Path,
    case: tuple[str, str, int, int, str, str],
    workers: int,
    seed: int,
    source_commit: str,
    fixed_complete_work: bool,
) -> dict[str, Any]:
    role, layout, spacing, speed, terrain, objective = case
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary),
        "--mode", "optimize",
        "--layout", layout,
        "--spacing", str(spacing),
        "--reference-speed", str(speed),
        "--terrain", terrain,
        "--objective", objective,
        "--workers", str(workers),
        "--population", "256",
        "--generations", "3000",
        "--stall-generations", "4000" if fixed_complete_work else "50",
        "--tolerance", "1e-15",
        "--seed", str(seed),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=60 * 60,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "paper_case_role": role,
        "source_commit": source_commit,
        "fixed_complete_work": fixed_complete_work,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(
    payload: dict[str, Any],
    role: str,
    workers: int,
    fixed_complete_work: bool,
) -> None:
    require(payload.get("case_id") == role, f"{role}: case mismatch")
    require(
        payload.get("method_semantic_id") == METHOD_ID,
        f"{role}: method semantic ID mismatch",
    )
    require(
        payload.get("problem_semantic_id") == PROBLEM_ID,
        f"{role}: problem semantic ID mismatch",
    )
    require(
        payload.get("protocol_semantic_id") == PROTOCOL_ID,
        f"{role}: protocol semantic ID mismatch",
    )
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{role}: worker activation mismatch",
    )
    require(payload.get("population_size") == 256, f"{role}: population")
    require(
        0 < payload.get("generations", 0) <= 3000,
        f"{role}: generation limit",
    )
    require(
        payload.get("physical_fes")
        == 256 + payload.get("generations") * 243,
        f"{role}: physical FES ledger",
    )
    if fixed_complete_work:
        require(payload.get("generations") == 3000, f"{role}: full work")
        require(payload.get("physical_fes") == 729256, f"{role}: full FES")
    require(
        payload.get("best_evaluation", {}).get("feasible") is True,
        f"{role}: infeasible result",
    )
    for field in (
        "total_power_mw",
        "rated_power_mw",
        "capacity_factor",
        "total_cost_index",
        "total_cost_index_per_output_power",
    ):
        require(
            math.isfinite(payload["best_evaluation"][field]),
            f"{role}: nonfinite {field}",
        )
    require(payload.get("scientific_hash"), f"{role}: missing hash")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    case = (
        "t67_staggered_s4_u10_offshore_min_tciop",
        "staggered", 4, 10, "offshore", "min_tciop",
    )
    rows = {}
    for workers in (1, arguments.total_workers):
        rows[workers] = execute(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            case,
            workers,
            arguments.seed_base - 1,
            arguments.source_commit,
            True,
        )
        validate(rows[workers], case[0], workers, True)
    serial = rows[1]
    parallel = rows[arguments.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        and serial["best_evaluation"] == parallel["best_evaluation"]
        and serial["best_decision"] == parallel["best_decision"],
        "T67 one/all-core scientific trajectory mismatch",
    )
    speedup = {
        stage: serial[f"{stage}_seconds"]
        / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "algorithm", "end_to_end")
    }
    require(
        speedup["evaluator"] > 1.0
        and speedup["end_to_end"] > 1.0,
        f"T67 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": case[0],
        "physical_layout_evaluations": 729256,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, complete physical "
            "work, and scientific trajectory; one versus all Waffle cores",
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(
    arguments: argparse.Namespace,
    root: Path,
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    jobs = [
        (case, seed_index, arguments.seed_base + seed_index)
        for case in CASES
        for seed_index in range(25)
    ]
    if arguments.formal_max_runs > 0:
        jobs = jobs[:arguments.formal_max_runs]
    for position, (case, seed_index, seed) in enumerate(jobs, 1):
        role = case[0]
        payload = execute(
            arguments.binary,
            root / "formal" / role / f"seed-{seed_index:02d}.json",
            case,
            arguments.total_workers,
            seed,
            arguments.source_commit,
            False,
        )
        validate(payload, role, arguments.total_workers, False)
        rows.append(payload)
        if position % 25 == 0 or position == len(jobs):
            print(
                f"T67 formal progress {position}/{len(jobs)}",
                flush=True,
            )
    complete = len(rows) == 162 * 25
    summary = {
        "status": "pass" if complete else "bounded_pass",
        "paper_case_roles": 162,
        "seeds_per_case": 25,
        "required_target_runs": 4050,
        "completed_target_runs": len(rows),
        "complete": complete,
        "all_all_core": all(
            row["requested_workers"] == arguments.total_workers
            and row["observed_workers"] == arguments.total_workers
            for row in rows
        ),
        "all_feasible": all(
            row["best_evaluation"]["feasible"] for row in rows
        ),
        "total_physical_layout_evaluations": sum(
            row["physical_fes"] for row in rows
        ),
        "median_generations": statistics.median(
            row["generations"] for row in rows
        ),
        "median_end_to_end_seconds": statistics.median(
            row["end_to_end_seconds"] for row in rows
        ),
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026076700)
    parser.add_argument("--h6-only", action="store_true")
    parser.add_argument("--skip-h6", action="store_true")
    parser.add_argument("--formal-max-runs", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.binary.is_file(), "T67 binary missing")
    require(arguments.total_workers >= 2, "T67 all-core workers invalid")
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)

    receipt: dict[str, Any] = {
        "schema_version": 1,
        "corpus_id": "T67",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
    }
    if not arguments.skip_h6:
        receipt["h6"] = run_h6(arguments, root)
    if not arguments.h6_only:
        receipt["formal"] = run_formal(arguments, root)
    write_json(root / "campaign_receipt.json", receipt)
    print(json.dumps(receipt, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
