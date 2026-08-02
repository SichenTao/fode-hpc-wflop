#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0368 Waffle H6 and paper-native campaign
Paper DOI: 10.1016/j.enconman.2021.114610
Cited same-author public asset DOI: 10.17632/bvrdgykzwy.1
Protocol: one/all-core H6 on the most expensive S5W4 case; all 20 S1--S5
times W1--W4 paper-native cases at population100 and 500 generations; then
the sixteen Fig.11-style S1-layout transfers onto S2--S5.
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/liu_l0368.hpp
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


METHOD = "l0368_matlab_lineage_real_ga_declared_v1"
PROBLEM = "l0368_seabed_engineering_capital_power_proxy_v1"
PROTOCOL = "l0368_native_s1_s5_w1_w4_single_run_v1"
H6_CASE = "L0368_S5W4"


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


def call_json(command: list[str], timeout: float = 7200.0) -> dict | list:
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True, timeout=timeout
    )
    return json.loads(completed.stdout)


def list_cases(binary: Path) -> list[dict[str, Any]]:
    payload = call_json([str(binary), "--action", "list-cases"])
    require(isinstance(payload, list) and len(payload) == 20, "20-case matrix")
    return payload


def optimize(
    *, binary: Path, output: Path, case: str, seed: int, workers: int,
    population: int, generations: int, source_commit: str,
) -> dict[str, Any]:
    expected = {
        "source_commit": source_commit,
        "metadata": {"case_id": case},
        "seed": seed,
        "requested_workers": workers,
        "population": population,
        "generations": generations,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if (
            previous.get("source_commit") == source_commit
            and previous.get("metadata", {}).get("case_id") == case
            and all(previous.get(key) == value for key, value in expected.items()
                    if key not in {"source_commit", "metadata"})
        ):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    command = [
        str(binary), "--case", case, "--seed", str(seed),
        "--workers", str(workers), "--population", str(population),
        "--generations", str(generations), "--crossover-fraction", "0.3",
        "--elite-count", "5", "--spacing", "euclidean",
        "--output", str(temporary),
    ]
    subprocess.run(command, check=True, timeout=7200.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], case: str, workers: int) -> None:
    require(payload["metadata"]["case_id"] == case, "case")
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    evaluation = payload["best_evaluation"]
    require(evaluation["feasible"] is True, "feasible")
    require(1 <= evaluation["turbine_count"] <= 25, "turbine count")
    if evaluation["turbine_count"] > 1:
        require(evaluation["minimum_distance_m"] >= 500.0 - 1e-7, "spacing")
    require(evaluation["expected_power_mw"] > 0.0, "power")
    require(evaluation["initial_capital_cost_gbp"] > 0.0, "capital")
    if workers > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair = {}
    for workers in (1, arguments.total_workers):
        payload = optimize(
            binary=arguments.binary,
            output=root / "h6" / f"{H6_CASE}-w{workers:02d}.json",
            case=H6_CASE, seed=36801, workers=workers,
            population=100, generations=arguments.h6_generations,
            source_commit=arguments.source_commit,
        )
        validate(payload, H6_CASE, workers)
        pair[workers] = payload
    serial = pair[1]
    parallel = pair[arguments.total_workers]
    for key in ("scientific_hash", "best_layout", "best_evaluation", "physical_fes"):
        require(serial[key] == parallel[key], f"H6 identity: {key}")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "evaluator": serial["evaluator_seconds"]
            / parallel["evaluator_seconds"],
        "algorithm_non_evaluator": serial["algorithm_seconds"]
            / max(parallel["algorithm_seconds"], 1e-12),
    }
    require(speedup["end_to_end"] > 1.0, "H6 end-to-end speedup")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "case_id": H6_CASE, "population": 100,
        "generations": arguments.h6_generations,
        "one_worker": serial, "all_worker": parallel,
        "same_layout_objective_physics_fes_and_hash": True,
        "speedup": speedup,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def layout_argument(layout: list[list[float]]) -> str:
    return ";".join(f"{point[0]:.17g},{point[1]:.17g}" for point in layout)


def run_formal(
    arguments: argparse.Namespace, root: Path, cases: list[dict[str, Any]]
) -> dict[str, Any]:
    payloads: dict[str, dict[str, Any]] = {}
    rows = []
    for index, metadata in enumerate(cases):
        case = metadata["case_id"]
        payload = optimize(
            binary=arguments.binary,
            output=root / "formal" / f"{case}.json",
            case=case, seed=3680100 + index,
            workers=arguments.total_workers, population=100,
            generations=arguments.formal_generations,
            source_commit=arguments.source_commit,
        )
        validate(payload, case, arguments.total_workers)
        payloads[case] = payload
        evaluation = payload["best_evaluation"]
        rows.append({
            "case_id": case,
            "seed": payload["seed"],
            "physical_fes": payload["physical_fes"],
            "turbine_count": evaluation["turbine_count"],
            "capital_power_proxy_gbp_per_mw":
                evaluation["capital_power_proxy_gbp_per_mw"],
            "expected_power_mw": evaluation["expected_power_mw"],
            "efficiency_percent": evaluation["efficiency_percent"],
            "paper_turbine_anchor": metadata["paper_turbine_anchor"],
            "paper_capital_power_anchor_million_gbp_per_mw":
                metadata["paper_capital_power_anchor_million_gbp_per_mw"],
            "paper_total_power_anchor_mw":
                metadata["paper_total_power_anchor_mw"],
            "paper_efficiency_anchor_percent":
                metadata["paper_efficiency_anchor_percent"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
        write_json(root / "formal" / "summary.partial.json", {
            "status": "running", "source_commit": arguments.source_commit,
            "completed_cases": len(rows), "expected_cases": 20, "rows": rows,
        })

    transfers = []
    for wind in range(1, 5):
        source_case = f"L0368_S1W{wind}"
        source_layout = payloads[source_case]["best_layout"]
        for terrain in range(2, 6):
            target_case = f"L0368_S{terrain}W{wind}"
            target = call_json([
                str(arguments.binary), "--action", "evaluate-layout",
                "--case", target_case, "--spacing", "euclidean",
                "--layout-points", layout_argument(source_layout),
            ])
            require(isinstance(target, dict), "transfer payload")
            evaluation = target["evaluation"]
            require(evaluation["feasible"] is True, "transfer feasibility")
            native = payloads[target_case]["best_evaluation"]
            transfers.append({
                "source_case": source_case,
                "target_case": target_case,
                "source_turbine_count": len(source_layout),
                "transferred_capital_power_proxy_gbp_per_mw":
                    evaluation["capital_power_proxy_gbp_per_mw"],
                "native_capital_power_proxy_gbp_per_mw":
                    native["capital_power_proxy_gbp_per_mw"],
                "relative_increment":
                    evaluation["capital_power_proxy_gbp_per_mw"]
                    / native["capital_power_proxy_gbp_per_mw"] - 1.0,
            })
    require(len(rows) == 20, "formal case matrix")
    require(len(transfers) == 16, "transfer matrix")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "paper_native_cases": 20, "random_seed_repeats_per_case": 1,
        "population": 100, "generations": arguments.formal_generations,
        "workers_per_case": arguments.total_workers,
        "rows": rows, "figure11_style_transfers": transfers,
        "paper_table_closure_warning": (
            "For identical S1 turbine counts, paper COE times displayed power "
            "implies wind-dependent ICC despite Eqs.3-10; anchors are reported "
            "but not forced by nonphysical calibration."
        ),
        "claim_boundary": (
            "source-backed flexible academic reconstruction; not author target "
            "code, private Nanao arrays, MATLAB trajectory or numerical replay"
        ),
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--h6-generations", type=int, default=100)
    parser.add_argument("--formal-generations", type=int, default=500)
    parser.add_argument("--phase", choices=("all", "h6", "formal"), default="all")
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    cases = list_cases(arguments.binary)
    h6 = run_h6(arguments, root) if arguments.phase in ("all", "h6") else None
    formal = (
        run_formal(arguments, root, cases)
        if arguments.phase in ("all", "formal") else None
    )
    if arguments.phase != "all":
        print(json.dumps({
            "status": "pass", "phase": arguments.phase,
            "output_root": str(root),
        }, sort_keys=True))
        return
    assert h6 is not None and formal is not None
    write_json(root / "campaign_summary.json", {
        "status": "pass", "h6_status": h6["status"],
        "formal_cases": len(formal["rows"]),
        "transfer_cases": len(formal["figure11_style_transfers"]),
        "output_root": str(root),
    })
    print(json.dumps({
        "status": "pass", "h6_speedup": h6["speedup"]["end_to_end"],
        "formal_cases": len(formal["rows"]),
        "transfer_cases": len(formal["figure11_style_transfers"]),
        "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
