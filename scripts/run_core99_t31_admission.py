#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T31 Waffle H6 and paper-native formal campaign
Paper title: Variable Neighborhood Search for Large Offshore Wind Farm Layout
Optimization
Paper DOI: 10.1016/j.cor.2021.105588
Dataset DOI: 10.11583/DTU.13134731.
Public source: official ten-instance dataset; no paper-linked VNS source found.
Missing/conflicts/reconstruction:
hpc/core99_cpp/include/core99/cazzaro_t31.hpp and the frozen contract.
Resource rule: one optimization owns one persistent all-core CPU team. Paper
cases run sequentially, and prebuilt packed matrices are reused so the stated
30-second/one-hour/ten-hour limits apply to VNS rather than preprocessing.
Claim boundary: academic paper/data reconstruction, not author source or
identical numerical replay.
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


OFFICIAL_CASES = tuple(f"t31_official_{site}" for site in "abcdefghij")
MOSETTI_CASES = (
    "t31_mosetti_di",
    "t31_mosetti_dplus_i",
    "t31_mosetti_dplus_iplus",
)
SHAKE_MODES = (
    "circular",
    "conic",
    "directional",
    "displacement",
    "random",
    "random_directional",
    "circular_displacement",
    "random_conic",
    "directional_conic",
    "all",
)


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
    data_root: str,
    output: Path,
    source_commit: str,
    case_id: str,
    seed: int,
    workers: int,
    time_seconds: float,
    foundation_mode: str = "none",
    shake_mode: str = "random_conic",
    fixed_iterations: int = 0,
    matrix_cache: Path | None = None,
    checkpoints: tuple[int, ...] = (),
) -> dict:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if (
            previous.get("source_commit") == source_commit
            and previous.get("case_id") == case_id
            and previous.get("seed") == seed
            and previous.get("requested_workers") == workers
            and previous.get("foundation_mode") == foundation_mode
            and previous.get("shake_mode") == shake_mode
            and previous.get("frozen_time_seconds") == time_seconds
            and previous.get("frozen_iterations") == fixed_iterations
        ):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    command = [
        binary,
        "--case",
        case_id,
        "--seed",
        str(seed),
        "--workers",
        str(workers),
        "--time-seconds",
        str(time_seconds),
        "--foundation-mode",
        foundation_mode,
        "--shake-mode",
        shake_mode,
        "--output",
        str(temporary),
    ]
    if case_id.startswith("t31_official_"):
        command.extend(("--data-root", data_root))
    if fixed_iterations:
        command.extend(("--iterations", str(fixed_iterations)))
    if matrix_cache is not None:
        matrix_cache.parent.mkdir(parents=True, exist_ok=True)
        command.extend(("--matrix-cache", str(matrix_cache)))
    if checkpoints:
        command.extend(
            ("--checkpoint-seconds", ",".join(map(str, checkpoints)))
        )
    started = time.monotonic()
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        timeout=max(1800.0, time_seconds + 1800.0),
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))["runs"][0]
    payload.update(
        {
            "case_id": case_id,
            "source_commit": source_commit,
            "frozen_time_seconds": time_seconds,
            "frozen_iterations": fixed_iterations,
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


def validate(
    row: dict,
    *,
    case_id: str,
    workers: int,
    foundation_mode: str,
    shake_mode: str,
) -> None:
    require(row["case_id"] == case_id, f"{case_id}: identity")
    require(
        row["method_semantic_id"]
        == "t31_pdsp_random_conic_vns_declared_v1",
        f"{case_id}: method semantics",
    )
    require(
        row["protocol_semantic_id"]
        == "t31_3x30s_10x3x1h_10x10h_v1",
        f"{case_id}: protocol semantics",
    )
    require(
        row["requested_workers"] == workers,
        f"{case_id}: requested workers",
    )
    if workers == 20:
        require(
            row["observed_workers"] == 20,
            f"{case_id}: all-core team not observed",
        )
    require(
        row["foundation_mode"] == foundation_mode
        and row["shake_mode"] == shake_mode,
        f"{case_id}: objective or shake mode",
    )
    require(bool(row["best_positions"]), f"{case_id}: empty layout")
    require(
        math.isfinite(row["best"]["objective_mwh_equivalent"])
        and row["best"]["spacing_violation_m"] <= 1.0e-9
        and row["best"]["objective_mwh_equivalent"]
        >= row["initial"]["objective_mwh_equivalent"] - 1.0e-7,
        f"{case_id}: objective retention or feasibility",
    )
    require(bool(row["scientific_hash"]), f"{case_id}: scientific hash")


def h6_probe(args: argparse.Namespace, root: Path) -> dict:
    case_id = "t31_official_a"
    rows: dict[int, list[dict]] = {1: [], args.total_workers: []}
    for workers in (1, args.total_workers):
        run_one(
            binary=args.binary,
            data_root=args.data_root,
            output=root / "h6" / f"warmup-w{workers:02d}.json",
            source_commit=args.source_commit,
            case_id=case_id,
            seed=args.seed_base - 100,
            workers=workers,
            time_seconds=60.0,
            fixed_iterations=1,
        )
        for observation in range(1, args.h6_observations + 1):
            row = run_one(
                binary=args.binary,
                data_root=args.data_root,
                output=(
                    root / "h6"
                    / f"observation-{observation:02d}-w{workers:02d}.json"
                ),
                source_commit=args.source_commit,
                case_id=case_id,
                seed=args.seed_base + observation,
                workers=workers,
                time_seconds=60.0,
                fixed_iterations=1,
            )
            validate(
                row,
                case_id=case_id,
                workers=workers,
                foundation_mode="none",
                shake_mode="random_conic",
            )
            rows[workers].append(row)
    for serial, parallel in zip(
        rows[1], rows[args.total_workers], strict=True
    ):
        require(
            serial["scientific_hash"] == parallel["scientific_hash"],
            "T31 H6 one/all-core scientific trajectory mismatch",
        )
    fields = (
        "problem_preprocessing_seconds",
        "matrix_seconds",
        "initialization_seconds",
        "local_search_seconds",
        "end_to_end_seconds",
    )
    medians = {
        workers: {
            field: statistics.median(row[field] for row in worker_rows)
            for field in fields
        }
        for workers, worker_rows in rows.items()
    }
    serial = medians[1]
    parallel = medians[args.total_workers]
    require(
        parallel["matrix_seconds"] < serial["matrix_seconds"]
        and parallel["end_to_end_seconds"] < serial["end_to_end_seconds"],
        "T31 all-core matrix/end-to-end path did not accelerate",
    )
    algorithm_case = "t31_official_i"
    algorithm_cache = root / "matrix-cache" / f"{algorithm_case}.pair"
    run_one(
        binary=args.binary,
        data_root=args.data_root,
        output=root / "h6" / "algorithm-cache-prebuild.json",
        source_commit=args.source_commit,
        case_id=algorithm_case,
        seed=args.seed_base - 200,
        workers=args.total_workers,
        time_seconds=600.0,
        fixed_iterations=1,
        matrix_cache=algorithm_cache,
    )
    algorithm_rows: dict[int, list[dict]] = {
        1: [],
        args.total_workers: [],
    }
    for workers in (1, args.total_workers):
        for observation in range(1, args.h6_observations + 1):
            row = run_one(
                binary=args.binary,
                data_root=args.data_root,
                output=(
                    root / "h6"
                    / (
                        f"algorithm-observation-{observation:02d}"
                        f"-w{workers:02d}.json"
                    )
                ),
                source_commit=args.source_commit,
                case_id=algorithm_case,
                seed=args.seed_base + 100 + observation,
                workers=workers,
                time_seconds=600.0,
                fixed_iterations=1,
                matrix_cache=algorithm_cache,
            )
            validate(
                row,
                case_id=algorithm_case,
                workers=workers,
                foundation_mode="none",
                shake_mode="random_conic",
            )
            require(
                row["matrix_pair_evaluations"] == 0,
                "T31 algorithm probe unexpectedly rebuilt matrix",
            )
            algorithm_rows[workers].append(row)
    for serial_row, parallel_row in zip(
        algorithm_rows[1],
        algorithm_rows[args.total_workers],
        strict=True,
    ):
        require(
            serial_row["scientific_hash"]
            == parallel_row["scientific_hash"],
            "T31 algorithm one/all-core scientific trajectory mismatch",
        )
    algorithm_fields = (
        "problem_preprocessing_seconds",
        "initialization_seconds",
        "local_search_seconds",
        "optimization_seconds",
        "end_to_end_seconds",
    )
    algorithm_medians = {
        workers: {
            field: statistics.median(row[field] for row in worker_rows)
            for field in algorithm_fields
        }
        for workers, worker_rows in algorithm_rows.items()
    }
    algorithm_serial = algorithm_medians[1]
    algorithm_parallel = algorithm_medians[args.total_workers]
    require(
        algorithm_parallel["initialization_seconds"]
            < algorithm_serial["initialization_seconds"]
        and algorithm_parallel["local_search_seconds"]
            < algorithm_serial["local_search_seconds"]
        and algorithm_parallel["end_to_end_seconds"]
            < algorithm_serial["end_to_end_seconds"],
        "T31 all-core initialization/local-search path did not accelerate",
    )
    receipt = {
        "status": "pass",
        "case_id": case_id,
        "observation_count": args.h6_observations,
        "source_commit": args.source_commit,
        "cpu_affinity": affinity(),
        "rows": rows,
        "median_seconds": {
            "one_worker": serial,
            "all_workers": parallel,
        },
        "median_speedup": {
            field: (
                serial[field] / parallel[field]
                if parallel[field] > 0.0
                else None
            )
            for field in fields
        },
        "algorithm_cached_matrix_probe": {
            "case_id": algorithm_case,
            "rows": algorithm_rows,
            "median_seconds": {
                "one_worker": algorithm_serial,
                "all_workers": algorithm_parallel,
            },
            "median_speedup": {
                field: algorithm_serial[field] / algorithm_parallel[field]
                for field in algorithm_fields
            },
            "matrix_pair_evaluations": 0,
        },
        "claim_boundary":
            "same pure-C++ source and scientific trajectories; site A "
            "measures uncached wake-matrix construction and site I with the "
            "same prebuilt matrix measures PDSP/VNS work; one versus all "
            "twenty Waffle CPU workers",
    }
    (root / "h6").mkdir(parents=True, exist_ok=True)
    (root / "h6" / "summary.json").write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return receipt


def prebuild_matrices(args: argparse.Namespace, root: Path) -> dict[str, Path]:
    caches: dict[str, Path] = {}
    for offset, case_id in enumerate(OFFICIAL_CASES):
        cache = root / "matrix-cache" / f"{case_id}.pair"
        caches[case_id] = cache
        row = run_one(
            binary=args.binary,
            data_root=args.data_root,
            output=root / "matrix-cache" / f"{case_id}.receipt.json",
            source_commit=args.source_commit,
            case_id=case_id,
            seed=args.seed_base - 1000 + offset,
            workers=args.total_workers,
            time_seconds=60.0,
            fixed_iterations=1,
            matrix_cache=cache,
        )
        validate(
            row,
            case_id=case_id,
            workers=args.total_workers,
            foundation_mode="none",
            shake_mode="random_conic",
        )
        require(cache.exists(), f"{case_id}: packed matrix cache absent")
    return caches


def formal_campaign(
    args: argparse.Namespace,
    root: Path,
    caches: dict[str, Path],
) -> dict:
    rows: list[dict] = []
    for offset, case_id in enumerate(MOSETTI_CASES):
        row = run_one(
            binary=args.binary,
            data_root=args.data_root,
            output=root / "formal" / "mosetti" / f"{case_id}.json",
            source_commit=args.source_commit,
            case_id=case_id,
            seed=args.seed_base + 100 + offset,
            workers=args.total_workers,
            time_seconds=args.mosetti_seconds,
        )
        validate(
            row,
            case_id=case_id,
            workers=args.total_workers,
            foundation_mode="none",
            shake_mode="random_conic",
        )
        rows.append(row)

    for case_offset, site in enumerate("bde"):
        case_id = f"t31_official_{site}"
        for mode_offset, shake_mode in enumerate(SHAKE_MODES):
            row = run_one(
                binary=args.binary,
                data_root=args.data_root,
                output=(
                    root / "formal" / "shake-ablation"
                    / f"{case_id}-{shake_mode}.json"
                ),
                source_commit=args.source_commit,
                case_id=case_id,
                seed=args.seed_base + 1000 + 10 * case_offset + mode_offset,
                workers=args.total_workers,
                time_seconds=args.one_hour_seconds,
                shake_mode=shake_mode,
                matrix_cache=caches[case_id],
            )
            validate(
                row,
                case_id=case_id,
                workers=args.total_workers,
                foundation_mode="none",
                shake_mode=shake_mode,
            )
            rows.append(row)

    for case_offset, case_id in enumerate(OFFICIAL_CASES):
        for mode_offset, foundation_mode in enumerate(
            ("low_cost", "high_cost", "none")
        ):
            row = run_one(
                binary=args.binary,
                data_root=args.data_root,
                output=(
                    root / "formal" / "one-hour"
                    / f"{case_id}-{foundation_mode}.json"
                ),
                source_commit=args.source_commit,
                case_id=case_id,
                seed=args.seed_base + 2000 + 3 * case_offset + mode_offset,
                workers=args.total_workers,
                time_seconds=args.one_hour_seconds,
                foundation_mode=foundation_mode,
                matrix_cache=caches[case_id],
            )
            validate(
                row,
                case_id=case_id,
                workers=args.total_workers,
                foundation_mode=foundation_mode,
                shake_mode="random_conic",
            )
            rows.append(row)

    checkpoint_scale = args.ten_hour_seconds / 36000.0
    checkpoints = tuple(
        max(1, round(seconds * checkpoint_scale))
        for seconds in (3600, 7200, 18000, 36000)
    )
    for case_offset, case_id in enumerate(OFFICIAL_CASES):
        row = run_one(
            binary=args.binary,
            data_root=args.data_root,
            output=root / "formal" / "ten-hour" / f"{case_id}.json",
            source_commit=args.source_commit,
            case_id=case_id,
            seed=args.seed_base + 3000 + case_offset,
            workers=args.total_workers,
            time_seconds=args.ten_hour_seconds,
            matrix_cache=caches[case_id],
            checkpoints=checkpoints,
        )
        validate(
            row,
            case_id=case_id,
            workers=args.total_workers,
            foundation_mode="none",
            shake_mode="random_conic",
        )
        require(
            len(row["time_checkpoints"]) == 4,
            f"{case_id}: four paper checkpoints absent",
        )
        rows.append(row)

    summary = {
        "schema_version": 1,
        "status": "complete",
        "corpus_id": "T31",
        "source_commit": args.source_commit,
        "completed_at": datetime.now(timezone.utc).isoformat(),
        "paper_run_count": len(rows),
        "mosetti_run_count": len(MOSETTI_CASES),
        "shake_ablation_run_count": 3 * len(SHAKE_MODES),
        "one_hour_run_count": 10 * 3,
        "ten_hour_run_count": 10,
        "paper_time_seconds": {
            "mosetti": args.mosetti_seconds,
            "one_hour": args.one_hour_seconds,
            "ten_hour": args.ten_hour_seconds,
        },
        "all_core_workers": args.total_workers,
        "cpu_affinity": affinity(),
        "claim_boundary":
            "academic paper/data reconstruction; formal objective values "
            "are not claimed as author numerical replay",
    }
    (root / "formal").mkdir(parents=True, exist_ok=True)
    (root / "formal" / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data-root", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=5)
    parser.add_argument("--seed-base", type=int, default=310000)
    parser.add_argument("--mosetti-seconds", type=float, default=30.0)
    parser.add_argument("--one-hour-seconds", type=float, default=3600.0)
    parser.add_argument("--ten-hour-seconds", type=float, default=36000.0)
    parser.add_argument(
        "--phase",
        choices=("h6", "formal", "all"),
        default="all",
    )
    args = parser.parse_args()
    require(args.total_workers == 20, "T31 formal runner requires Waffle 20")
    root = Path(args.output_root)
    root.mkdir(parents=True, exist_ok=True)
    result: dict[str, dict] = {}
    if args.phase in ("h6", "all"):
        result["h6"] = h6_probe(args, root)
    if args.phase in ("formal", "all"):
        caches = prebuild_matrices(args, root)
        result["formal"] = formal_campaign(args, root, caches)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
