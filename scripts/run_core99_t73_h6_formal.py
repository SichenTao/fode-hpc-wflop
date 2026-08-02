#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T73 Waffle H6 and 25-seed native campaign
Paper/DOI: Song et al.; 10.1016/j.cie.2018.04.051.
Formal protocol: primary four-cluster profile, twelve native paper roles per
seed, 25 platform seeds, binary GA 100x50, pattern search 200 iterations,
1000 maintenance replications and all available Waffle CPU cores.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
hpc/core99_cpp/include/core99/song_t73.hpp.
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


METHOD = "t73_ga_pattern_kmeans_ocbm_declared_v1"
PROBLEM = "t73_nj342_layout_maintenance_declared_v1"
PROTOCOL = "t73_table3_table5_12roles_25seed_v1"


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


def optimize(
    *, binary: Path, output: Path, seed: int, workers: int,
    ga_population: int, ga_generations: int, pattern_iterations: int,
    maintenance_replications: int, source_commit: str,
) -> dict[str, Any]:
    identity = {
        "source_commit": source_commit,
        "seed": seed,
        "requested_workers": workers,
        "ga_population": ga_population,
        "ga_generations": ga_generations,
        "pattern_iterations": pattern_iterations,
        "maintenance_replications": maintenance_replications,
        "case_id": "nj342-equation_text_four",
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in identity.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    subprocess.run([
        str(binary), "--seed", str(seed), "--workers", str(workers),
        "--ga-population", str(ga_population),
        "--ga-generations", str(ga_generations),
        "--pattern-iterations", str(pattern_iterations),
        "--maintenance-replications", str(maintenance_replications),
        "--cluster-profile", "equation_text_four",
        "--output", str(temporary),
    ], check=True, timeout=86_400.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], workers: int) -> None:
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["cluster_count"] == 4, "primary four-cluster profile")
    require(len(payload["roles"]) == 12, "twelve native roles")
    require(len(payload["discrete_layout"]) > 0, "discrete layout")
    require(len(payload["continuous_layout"])
            == len(payload["discrete_layout"]), "layout cardinality")
    require(len(payload["cluster_assignment"])
            == len(payload["continuous_layout"]), "clusters")
    require(payload["layout_evaluations"]
            == payload["ga_population"] * (payload["ga_generations"] + 1)
            + 20 * payload["pattern_iterations"], "physical layout work")
    require(payload["component_life_events"] > 0, "maintenance work")
    if workers > 1:
        require(payload["observed_workers"] == workers,
                "all requested workers participated")


def scientific_projection(payload: dict[str, Any]) -> dict[str, Any]:
    excluded = {
        "source_commit", "binary_sha256", "runner_wall_seconds",
        "requested_workers", "observed_workers", "parallel_regions",
        "scenario_precompute_seconds", "layout_evaluator_seconds",
        "algorithm_seconds", "maintenance_simulation_seconds",
        "end_to_end_seconds",
    }
    return {key: value for key, value in payload.items() if key not in excluded}


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair: dict[int, dict[str, Any]] = {}
    for workers in (1, arguments.total_workers):
        payload = optimize(
            binary=arguments.binary,
            output=root / "h6" / f"primary-w{workers:02d}.json",
            seed=73001, workers=workers,
            ga_population=100, ga_generations=10,
            pattern_iterations=40, maintenance_replications=100,
            source_commit=arguments.source_commit,
        )
        validate(payload, workers)
        pair[workers] = payload
    serial = pair[1]
    parallel = pair[arguments.total_workers]
    require(scientific_projection(serial) == scientific_projection(parallel),
            "H6 one/all-core scientific identity")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "layout_algorithm_wall": serial["algorithm_seconds"]
            / parallel["algorithm_seconds"],
        "maintenance_wall": serial["maintenance_simulation_seconds"]
            / parallel["maintenance_simulation_seconds"],
    }
    require(speedup["end_to_end"] > 1.0, "end-to-end speedup")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "fixed_work": {
            "ga_population": 100,
            "ga_generations": 10,
            "pattern_iterations": 40,
            "maintenance_replications": 100,
        },
        "one_worker": serial,
        "all_worker": parallel,
        "speedup": speedup,
        "same_layouts_clusters_roles_work_and_hash": True,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for seed_index in range(arguments.seeds):
        seed = 7_300_100 + seed_index
        payload = optimize(
            binary=arguments.binary,
            output=root / "formal" / f"seed-{seed_index + 1:02d}.json",
            seed=seed, workers=arguments.total_workers,
            ga_population=arguments.ga_population,
            ga_generations=arguments.ga_generations,
            pattern_iterations=arguments.pattern_iterations,
            maintenance_replications=arguments.maintenance_replications,
            source_commit=arguments.source_commit,
        )
        validate(payload, arguments.total_workers)
        rows.append({
            "seed_index": seed_index + 1,
            "seed": seed,
            "paper_roles": len(payload["roles"]),
            "turbine_count": len(payload["continuous_layout"]),
            "layout_evaluations": payload["layout_evaluations"],
            "wind_scenario_turbine_evaluations":
                payload["wind_scenario_turbine_evaluations"],
            "component_life_events": payload["component_life_events"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
        write_json(root / "formal" / "summary.partial.json", {
            "status": "running",
            "source_commit": arguments.source_commit,
            "completed_runs": len(rows),
            "expected_runs": arguments.seeds,
            "completed_paper_roles": 12 * len(rows),
            "rows": rows,
        })
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "seeds": arguments.seeds,
        "paper_roles_per_seed": 12,
        "completed_runs": len(rows),
        "completed_paper_roles": 12 * len(rows),
        "layout_evaluations": sum(row["layout_evaluations"] for row in rows),
        "wind_scenario_turbine_evaluations": sum(
            row["wind_scenario_turbine_evaluations"] for row in rows
        ),
        "component_life_events": sum(
            row["component_life_events"] for row in rows
        ),
        "summed_end_to_end_seconds": sum(
            row["end_to_end_seconds"] for row in rows
        ),
        "rows": rows,
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", required=True, type=int)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--seeds", type=int, default=25)
    parser.add_argument("--ga-population", type=int, default=100)
    parser.add_argument("--ga-generations", type=int, default=50)
    parser.add_argument("--pattern-iterations", type=int, default=200)
    parser.add_argument("--maintenance-replications", type=int, default=1000)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.total_workers >= 2, "H6 requires multiple cores")
    arguments.output.mkdir(parents=True, exist_ok=True)
    manifest = {
        "corpus_id": "T73",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
        "stage": arguments.stage,
        "formal_budget": {
            "seeds": arguments.seeds,
            "ga_population": arguments.ga_population,
            "ga_generations": arguments.ga_generations,
            "pattern_iterations": arguments.pattern_iterations,
            "maintenance_replications": arguments.maintenance_replications,
        },
    }
    write_json(arguments.output / "manifest.json", manifest)
    if arguments.stage in {"h6", "all"}:
        run_h6(arguments, arguments.output)
    if arguments.stage in {"formal", "all"}:
        require((arguments.output / "h6" / "summary.json").exists(),
                "formal run requires completed H6 summary")
        run_formal(arguments, arguments.output)


if __name__ == "__main__":
    main()
