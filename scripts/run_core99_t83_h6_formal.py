#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T83 Waffle H6 and eight-seed paper campaign
Paper DOI: 10.1016/j.apenergy.2022.118830
Public source, missing information, completion, semantics, HPC and claim:
hpc/core99_cpp/include/core99/cazzaro_t83.hpp
Formal protocol: eight Table-1 seed roles; each run executes macro screening,
meso shape construction and equal 30-minute micro VNS for the optimized shape
and best rectangle, hence 16 paper design roles and eight hours of micro search.
HPC protocol: H6 compares one and every Waffle worker on identical fixed work.
Every formal seed then owns all Waffle cores; roles execute sequentially.
Controlling contract: shared/contracts/core99_t83_cazzaro_multiscale_2022.json
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time
from typing import Any


METHOD = "t83_macro_meso_random_conic_vns_declared_v1"
PROBLEM = "t83_round4_eightseed_same_lineage_proxy_v1"
PROTOCOL = "t83_native_8seed_shape_rectangle_30min_v1"
CASES = tuple("ABCDEFGH")
RECTANGLE_NPV = {
    "A": 4705.9, "B": 4675.2, "C": 4501.4, "D": 3955.7,
    "E": 4019.6, "F": 3929.7, "G": 3991.9, "H": 3932.0,
}


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
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def execute(
    *, binary: Path, data_root: Path, output: Path, role: str, workers: int,
    seed: int, source_commit: str, micro_seconds: float, micro_cycles: int,
    macro_cell_axis: int,
) -> dict[str, Any]:
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if (
            previous.get("source_commit") == source_commit
            and previous.get("requested_workers") == workers
            and previous.get("frozen_micro_seconds") == micro_seconds
            and previous.get("frozen_micro_cycles") == micro_cycles
            and previous.get("frozen_macro_cell_axis") == macro_cell_axis
        ):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--mode", "optimize", "--data-root", str(data_root),
        "--case", role, "--workers", str(workers), "--seed", str(seed),
        "--micro-seconds", str(micro_seconds), "--macro-cell-axis",
        str(macro_cell_axis), "--output", str(temporary),
    ]
    if micro_cycles:
        command.extend(("--micro-cycles", str(micro_cycles)))
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True,
        timeout=max(3600.0, 2.0 * micro_seconds + 3600.0),
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "frozen_micro_seconds": micro_seconds,
        "frozen_micro_cycles": micro_cycles,
        "frozen_macro_cell_axis": macro_cell_axis,
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], role: str, workers: int) -> None:
    require(payload.get("case_id") == f"t83_seed_{role}", f"{role}: identity")
    require(payload.get("method_semantic_id") == METHOD, f"{role}: method")
    require(payload.get("problem_semantic_id") == PROBLEM, f"{role}: problem")
    require(payload.get("protocol_semantic_id") == PROTOCOL, f"{role}: protocol")
    require(payload.get("requested_workers") == workers, f"{role}: requested")
    require(payload.get("observed_workers", 0) >= min(2, workers),
            f"{role}: worker participation")
    require(payload.get("turbines") == 100, f"{role}: turbine count")
    require(payload.get("macro_rectangles_evaluated") == 270,
            f"{role}: complete local macro screen")
    require(abs(payload["macro_rectangle"]["npv_meur"]
                - RECTANGLE_NPV[role]) <= 1.0e-8, f"{role}: Table-1 anchor")
    for design in ("meso_shape", "optimized_shape", "optimized_rectangle"):
        row = payload[design]
        require(row.get("feasible") is True, f"{role}: {design} feasible")
        require(row["minimum_spacing_m"] >= 1200.0 - 1.0e-6,
                f"{role}: {design} spacing")
        require(row["area_km2"] <= 500.0 + 1.0e-9,
                f"{role}: {design} area")
        require(row["perimeter_to_sqrt_area"] <= 5.0 + 1.0e-9,
                f"{role}: {design} PtA")
        require(row["density_mw_km2"] >= 3.0 - 1.0e-9,
                f"{role}: {design} density")
    require(payload["optimized_shape"]["npv_meur"]
            >= payload["meso_shape"]["npv_meur"] - 1.0e-9,
            f"{role}: shape VNS incumbent retention")
    require(payload["optimized_rectangle"]["npv_meur"]
            >= payload["macro_rectangle"]["npv_meur"] - 1.0e-9,
            f"{role}: rectangle VNS incumbent retention")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = {
        workers: execute(
            binary=arguments.binary, data_root=arguments.data_root,
            output=root / "h6" / f"workers-{workers:02d}.json", role="A",
            workers=workers, seed=arguments.seed_base - 1,
            source_commit=arguments.source_commit, micro_seconds=0.0,
            micro_cycles=50, macro_cell_axis=3,
        )
        for workers in (1, arguments.total_workers)
    }
    for workers, payload in rows.items():
        validate(payload, "A", workers)
    serial, parallel = rows[1], rows[arguments.total_workers]
    identity_keys = (
        "macro_rectangle", "meso_shape", "optimized_shape",
        "optimized_rectangle", "meso_positions", "optimized_shape_positions",
        "optimized_rectangle_positions", "scientific_hash",
    )
    for key in identity_keys:
        require(serial[key] == parallel[key], f"T83 H6 differs for {key}")
    stages = (
        "source_preprocessing", "pair_matrix", "macro", "meso",
        "shape_micro", "rectangle_micro", "end_to_end",
    )
    speedup = {
        stage: serial[f"{stage}_seconds"] / parallel[f"{stage}_seconds"]
        for stage in stages
    }
    for stage in stages:
        require(speedup[stage] > 1.0, f"T83 {stage} did not accelerate")
    result = {
        "status": "pass",
        "case": "A",
        "fixed_micro_cycles_per_design": 50,
        "serial": serial,
        "parallel": parallel,
        "speedup": speedup,
        "claim_boundary": (
            "same pure-C++ T83 proxy problem, seed, macro screen, meso work, "
            "micro cycles, layouts, evaluations and hash; one versus every "
            "Waffle worker; not MATLAB or author-Python comparison"
        ),
    }
    write_json(root / "h6" / "summary.json", result)
    return result


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    roles = CASES[:arguments.formal_max_cases] if arguments.formal_max_cases else CASES
    rows: list[dict[str, Any]] = []
    for index, role in enumerate(roles):
        payload = execute(
            binary=arguments.binary, data_root=arguments.data_root,
            output=root / "formal" / f"seed-{role}.json", role=role,
            workers=arguments.total_workers, seed=arguments.seed_base + index,
            source_commit=arguments.source_commit,
            micro_seconds=arguments.formal_micro_seconds, micro_cycles=0,
            macro_cell_axis=3,
        )
        validate(payload, role, arguments.total_workers)
        rows.append(payload)
        print(f"T83 formal progress {len(rows)}/{len(roles)}", flush=True)
    status = "pass" if len(rows) == 8 and arguments.formal_micro_seconds == 1800.0 \
        else "development_smoke_pass"
    result = {
        "status": status,
        "required_seed_runs": 8,
        "required_design_roles": 16,
        "completed_seed_runs": len(rows),
        "completed_design_roles": 2 * len(rows),
        "workers_per_seed": arguments.total_workers,
        "micro_seconds_per_design": arguments.formal_micro_seconds,
        "binary_sha256": sha256(arguments.binary),
        "source_commit": arguments.source_commit,
        "optimized_shape_npv_meur": {
            row["paper_case"]["seed_role"]: row["optimized_shape"]["npv_meur"]
            for row in rows
        },
        "optimized_rectangle_npv_meur": {
            row["paper_case"]["seed_role"]:
                row["optimized_rectangle"]["npv_meur"] for row in rows
        },
        "median_end_to_end_seconds": statistics.median(
            row["end_to_end_seconds"] for row in rows
        ),
        "claim_boundary": (
            "paper-native eight seed roles, each with the complete declared "
            "three-scale reconstruction and equal shape/rectangle micro time"
        ),
    }
    write_json(root / "formal" / "summary.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--seed-base", type=int, default=2026088300)
    parser.add_argument("--formal-micro-seconds", type=float, default=1800.0)
    parser.add_argument("--formal-max-cases", type=int, default=0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    arguments.data_root = arguments.data_root.resolve()
    root = arguments.output_root.resolve()
    require(arguments.binary.is_file(), "T83 binary absent")
    require(arguments.data_root.is_dir(), "T83 T31 dataset root absent")
    require(arguments.total_workers >= 2, "T83 all-core allocation too small")
    h6 = run_h6(arguments, root)
    formal = run_formal(arguments, root)
    write_json(root / "campaign_summary.json", {
        "status": formal["status"],
        "h6_speedup": h6["speedup"],
        "formal": formal,
    })
    print(json.dumps({
        "status": formal["status"],
        "h6_end_to_end_speedup": h6["speedup"]["end_to_end"],
        "completed_seed_runs": formal["completed_seed_runs"],
        "completed_design_roles": formal["completed_design_roles"],
        "output_root": str(root),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
