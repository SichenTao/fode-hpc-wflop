#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable Y14 Waffle H6 and paper-native campaign
Paper DOI: 10.1109/TSTE.2026.3661110
Protocol: six paper-native farm/preference roles; one/all-core reduced fixed
work H6; then 3 farm sizes x 2 preference rounds x 10 independent seeds at
population 50 and 150000 nominal evaluation slots on all cores.
Public asset, missing information, conflict, reconstruction, semantic IDs,
production backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/zhang_y14.hpp
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from typing import Any


CASES = (
    "Y14_n16_original", "Y14_n24_original", "Y14_n48_original",
    "Y14_n16_adjusted", "Y14_n24_adjusted", "Y14_n48_adjusted",
)
METHOD = "y14_psdrde_declared_reconstruction_v1"
PROBLEM = "y14_energy_noise_threefarm_declared_proxy_v1"
PROTOCOL = "y14_threefarm_two_preference_10seed_150k_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024*1024):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix+".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True)+"\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def execute(
    *, binary: Path, output: Path, case: str, workers: int, seed: int,
    maximum_slots: int, source_commit: str, timeout_seconds: float,
) -> dict[str, Any]:
    expected = {
        "source_commit": source_commit, "case_id": case,
        "requested_workers": workers, "seed": seed,
        "configured_maximum_evaluation_slots": maximum_slots,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in expected.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--case", case, "--workers", str(workers),
        "--seed", str(seed), "--population", "50", "--subpopulation", "10",
        "--maximum-evaluation-slots", str(maximum_slots),
        "--crossover-rate", "0.9", "--learning-period", "50",
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=timeout_seconds
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "configured_maximum_evaluation_slots": maximum_slots,
        "runner_wall_seconds": time.monotonic()-started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], case: str, workers: int, slots: int) -> None:
    require(payload["case_id"] == case, "case")
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["population"] == 50, "population")
    require(payload["nominal_evaluation_slots"] == slots, "nominal slots")
    require(50 <= payload["physical_fes"] <= slots, "physical FES")
    require(len(payload["front"]) >= 1, "nonempty preferred front")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for case_index, case in enumerate(CASES):
        pair = {}
        for workers in (1, arguments.total_workers):
            payload = execute(
                binary=arguments.binary,
                output=root/"h6"/f"{case}-w{workers:02d}.json",
                case=case, workers=workers,
                seed=arguments.seed_base-100+case_index,
                maximum_slots=arguments.h6_evaluation_slots,
                source_commit=arguments.source_commit,
                timeout_seconds=7200.0,
            )
            validate(payload, case, workers, arguments.h6_evaluation_slots)
            pair[workers] = payload
        serial, parallel = pair[1], pair[arguments.total_workers]
        for key in (
            "generations", "nominal_evaluation_slots", "physical_fes",
            "scientific_hash", "front",
        ):
            require(serial[key] == parallel[key], f"{case} differs: {key}")
        speedup = {
            "evaluator": serial["evaluator_seconds"]
                / parallel["evaluator_seconds"],
            "end_to_end": serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        }
        require(speedup["evaluator"] > 1.0, f"{case} evaluator speedup")
        rows.append({
            "case_id": case, "one_worker": serial, "all_worker": parallel,
            "speedup": speedup,
            "same_front_fes_and_hash": True,
        })
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "case_count": len(rows), "rows": rows,
    }
    write_json(root/"h6"/"summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for case_index, case in enumerate(CASES):
        for repeat in range(arguments.formal_repeats):
            seed = arguments.seed_base+100*case_index+repeat
            payload = execute(
                binary=arguments.binary,
                output=root/"formal"/case/f"seed-{seed}.json",
                case=case, workers=arguments.total_workers, seed=seed,
                maximum_slots=arguments.formal_evaluation_slots,
                source_commit=arguments.source_commit,
                timeout_seconds=14400.0,
            )
            validate(
                payload, case, arguments.total_workers,
                arguments.formal_evaluation_slots,
            )
            rows.append({
                "case_id": case, "repeat": repeat, "seed": seed,
                "nominal_evaluation_slots": payload["nominal_evaluation_slots"],
                "physical_fes": payload["physical_fes"],
                "front_size": len(payload["front"]),
                "end_to_end_seconds": payload["end_to_end_seconds"],
                "scientific_hash": payload["scientific_hash"],
            })
    require(len(rows) == 6*arguments.formal_repeats, "formal matrix")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "paper_native_roles": 6, "independent_runs_per_role": arguments.formal_repeats,
        "population": 50,
        "maximum_evaluation_slots": arguments.formal_evaluation_slots,
        "workers_per_run": arguments.total_workers, "rows": rows,
        "claim_boundary": (
            "source-backed flexible academic reconstruction; not author code, "
            "private Gansu data, exact ISO site replay or numerical replay"
        ),
    }
    write_json(root/"formal"/"summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--seed-base", type=int, default=1414000)
    parser.add_argument("--h6-evaluation-slots", type=int, default=5000)
    parser.add_argument("--formal-evaluation-slots", type=int, default=150000)
    parser.add_argument("--formal-repeats", type=int, default=10)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    root = args.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    h6 = run_h6(args, root)
    formal = run_formal(args, root)
    write_json(root/"campaign_summary.json", {
        "status": "pass", "h6_cases": len(h6["rows"]),
        "formal_runs": len(formal["rows"]),
        "minimum_h6_evaluator_speedup": min(
            row["speedup"]["evaluator"] for row in h6["rows"]
        ),
        "minimum_h6_end_to_end_speedup": min(
            row["speedup"]["end_to_end"] for row in h6["rows"]
        ),
    })
    print(json.dumps({
        "status": "pass", "h6_cases": len(h6["rows"]),
        "formal_runs": len(formal["rows"]), "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
