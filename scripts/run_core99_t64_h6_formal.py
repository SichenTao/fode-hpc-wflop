#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T64 Waffle all-core H6 and paper-role formal
campaign
Paper/DOI: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
Public source: no target source or native paper arrays were located.
Missing/conflicts/reconstruction and semantic IDs:
hpc/core99_cpp/include/core99/sorkhabi_t64.hpp
Formal protocol: 9 main cases by 4 static/dynamic parameterizations by 25
seeds; 4 uniformity roles by 2 dynamic parameterizations by 25 seeds; and
the paper's 3 death-penalty preliminary roles by 25 seeds. This gives
900+200+75=1175 target runs. Final static/dynamic work uses 80000 complete
layout evaluations; death-preliminary work uses the paper's 40000 limit.
HPC protocol: one/all-core H6 uses the same complete 80000-evaluation
trajectory. Formal runs use one all-core process and execute sequentially to
avoid nested oversubscription.
Controlling contract: shared/contracts/core99_t64_sorkhabi_2016.json
Claim boundary: academic flexible declared reconstruction, not author code,
native polygon maps, native wind array, random states, or numerical replay.
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


METHOD_ID = "t64_nsga2_three_penalties_declared_reconstruction_v1"
PROBLEM_ID = "t64_energy_noise_land13role_declared_reconstruction_v1"
PROTOCOL_ID = "t64_80000fes_25seed_penalty_uniformity_v1"
MAIN_MODES = (
    "static_1e4",
    "static_4e4",
    "dynamic_cgen_ngen",
    "dynamic_cgen_half_ngen",
)
DYNAMIC_MODES = (
    "dynamic_cgen_ngen",
    "dynamic_cgen_half_ngen",
)
MAIN_CASES = tuple(
    (f"main_phi{availability}_n{turbines}", availability, turbines, 0)
    for availability in (70, 80, 90)
    for turbines in (5, 10, 15)
)
UNIFORMITY_CASES = tuple(
    (f"uniformity_phi80_n10_map{variant}", 80, 10, variant)
    for variant in range(4)
)
DEATH_CASES = tuple(
    (f"death_preliminary_phi80_n{turbines}", 80, turbines, 0)
    for turbines in (5, 10, 15)
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
    role: str,
    availability: int,
    turbines: int,
    map_variant: int,
    mode: str,
    workers: int,
    physical_fes: int,
    seed: int,
    source_commit: str,
    disable_convergence: bool = False,
) -> dict[str, Any]:
    if output.exists():
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload.get("source_commit") == source_commit:
            return payload
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    command = [
            str(binary),
            "--mode", "optimize",
            "--land-availability-percent", str(availability),
            "--turbines", str(turbines),
            "--map-variant", str(map_variant),
            "--penalty-mode", mode,
            "--workers", str(workers),
            "--physical-fes", str(physical_fes),
            "--seed", str(seed),
            "--output", str(temporary),
    ]
    if disable_convergence:
        command.append("--disable-convergence")
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
        "convergence_stopping_enabled": not disable_convergence,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(
    payload: dict[str, Any],
    role: str,
    mode: str,
    workers: int,
    physical_fes: int,
) -> None:
    require(
        payload.get("method_semantic_id") == METHOD_ID,
        f"{role}/{mode}: method semantic ID mismatch",
    )
    require(
        payload.get("problem_semantic_id") == PROBLEM_ID,
        f"{role}/{mode}: problem semantic ID mismatch",
    )
    require(
        payload.get("protocol_semantic_id") == PROTOCOL_ID,
        f"{role}/{mode}: protocol semantic ID mismatch",
    )
    require(payload.get("paper_case_role") == role, f"{role}: role mismatch")
    require(payload.get("penalty_mode") == mode, f"{role}: mode mismatch")
    require(
        payload.get("requested_workers") == workers
        and payload.get("observed_workers") == workers,
        f"{role}/{mode}: worker activation mismatch",
    )
    consumed_fes = payload.get("physical_fes", 0)
    require(
        0 < consumed_fes <= physical_fes
        and (
            consumed_fes == physical_fes
            or payload.get("converged") is True
        ),
        f"{role}/{mode}: paper stopping/FES mismatch",
    )
    require(payload.get("front"), f"{role}/{mode}: empty Pareto front")
    for point in payload["front"]:
        require(
            math.isfinite(point["aep_gwh"])
            and math.isfinite(point["maximum_spl_dba"]),
            f"{role}/{mode}: nonfinite objective",
        )
    require(payload.get("scientific_hash"), f"{role}/{mode}: missing hash")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = {}
    for workers in (1, arguments.total_workers):
        rows[workers] = execute(
            arguments.binary,
            root / "h6" / f"workers-{workers:02d}.json",
            "h6_phi80_n10_map0",
            80,
            10,
            0,
            "dynamic_cgen_ngen",
            workers,
            80000,
            arguments.seed_base - 1,
            arguments.source_commit,
            True,
        )
        validate(
            rows[workers],
            "h6_phi80_n10_map0",
            "dynamic_cgen_ngen",
            workers,
            80000,
        )
        require(
            rows[workers]["physical_fes"] == 80000,
            "T64 H6 must consume the complete fixed physical work",
        )
    serial = rows[1]
    parallel = rows[arguments.total_workers]
    require(
        serial["scientific_hash"] == parallel["scientific_hash"]
        and serial["front"] == parallel["front"],
        "T64 one/all-core scientific trajectory mismatch",
    )
    speedup = {
        stage: serial[f"{stage}_seconds"]
        / parallel[f"{stage}_seconds"]
        for stage in ("evaluator", "algorithm", "end_to_end")
    }
    require(
        speedup["evaluator"] > 1.0
        and speedup["end_to_end"] > 1.0,
        f"T64 dominant stages did not accelerate: {speedup}",
    )
    result = {
        "status": "pass",
        "case": "80 percent land availability, 10 turbines, map 0",
        "physical_layout_evaluations": 80000,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary":
            "same pure-C++ source, paper problem, seed, complete physical "
            "work, and scientific trajectory; one versus all Waffle cores",
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def summarize(payloads: list[dict[str, Any]]) -> dict[str, Any]:
    fronts = [point for payload in payloads for point in payload["front"]]
    return {
        "repeat_count": len(payloads),
        "all_requested_workers_observed": all(
            item["requested_workers"] == item["observed_workers"]
            for item in payloads
        ),
        "all_paper_stopping_rules_satisfied": all(
            item["physical_fes"] in (40000, 80000)
            or item["converged"]
            for item in payloads
        ),
        "converged_runs": sum(bool(item["converged"]) for item in payloads),
        "total_physical_layout_evaluations": sum(
            item["physical_fes"] for item in payloads
        ),
        "pooled_front_points": len(fronts),
        "maximum_aep_gwh": max(point["aep_gwh"] for point in fronts),
        "minimum_spl_dba": min(
            point["maximum_spl_dba"] for point in fronts
        ),
        "median_evaluator_seconds": statistics.median(
            item["evaluator_seconds"] for item in payloads
        ),
        "median_algorithm_seconds": statistics.median(
            item["algorithm_seconds"] for item in payloads
        ),
        "median_end_to_end_seconds": statistics.median(
            item["end_to_end_seconds"] for item in payloads
        ),
        "scientific_hashes": [
            item["scientific_hash"] for item in payloads
        ],
    }


def formal_jobs(repeat_count: int, seed_base: int):
    for role, availability, turbines, variant in MAIN_CASES:
        for mode_index, mode in enumerate(MAIN_MODES):
            for repeat in range(repeat_count):
                yield (
                    role, availability, turbines, variant, mode, 80000,
                    seed_base + mode_index * 100000 + repeat,
                )
    for role, availability, turbines, variant in UNIFORMITY_CASES:
        for mode_index, mode in enumerate(DYNAMIC_MODES):
            for repeat in range(repeat_count):
                yield (
                    role, availability, turbines, variant, mode, 80000,
                    seed_base + 500000 + mode_index * 100000 + repeat,
                )
    for role, availability, turbines, variant in DEATH_CASES:
        for repeat in range(repeat_count):
            yield (
                role, availability, turbines, variant, "death", 40000,
                seed_base + 700000 + repeat,
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--repeat-count", type=int, default=25)
    parser.add_argument("--seed-base", type=int, default=2026046400)
    parser.add_argument(
        "--h6-only",
        action="store_true",
        help="run only complete-work one/all-core admission",
    )
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.total_workers >= 2, "T64 H6 requires all host cores")
    require(arguments.repeat_count == 25, "T64 formal requires 25 seeds")
    require(arguments.binary.is_file(), "T64 binary missing")

    root = arguments.output_root
    root.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    h6 = run_h6(arguments, root)
    if arguments.h6_only:
        write_json(root / "h6_only_receipt.json", {
            "schema_version": 1,
            "corpus_id": "T64",
            "status": "complete",
            "source_commit": arguments.source_commit,
            "binary_sha256": sha256(arguments.binary),
            "h6_summary": h6,
            "runner_wall_seconds": time.monotonic() - started,
        })
        return 0

    grouped: dict[str, list[dict[str, Any]]] = {}
    completed = 0
    for (
        role, availability, turbines, variant, mode, physical_fes, seed
    ) in formal_jobs(arguments.repeat_count, arguments.seed_base):
        key = f"{role}__{mode}"
        payload = execute(
            arguments.binary,
            root / "formal_raw" / key / f"seed-{seed}.json",
            role,
            availability,
            turbines,
            variant,
            mode,
            arguments.total_workers,
            physical_fes,
            seed,
            arguments.source_commit,
        )
        validate(
            payload,
            role,
            mode,
            arguments.total_workers,
            physical_fes,
        )
        grouped.setdefault(key, []).append(payload)
        completed += 1

    require(completed == 1175, f"T64 formal run count {completed} != 1175")
    formal_summary = {
        "schema_version": 1,
        "corpus_id": "T64",
        "status": "complete",
        "source_commit": arguments.source_commit,
        "binary_sha256": sha256(arguments.binary),
        "workers_per_run": arguments.total_workers,
        "seeds_per_configuration": arguments.repeat_count,
        "main_static_dynamic_runs": 900,
        "uniformity_dynamic_runs": 200,
        "death_preliminary_runs": 75,
        "target_run_count": completed,
        "group_summaries": {
            key: summarize(payloads)
            for key, payloads in sorted(grouped.items())
        },
        "h6_summary": h6,
        "runner_wall_seconds": time.monotonic() - started,
        "claim_boundary":
            "paper-role academic flexible reconstruction with 25 platform "
            "seeds; not author raw-front or exact numerical replay",
    }
    write_json(root / "formal_summary.json", formal_summary)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
