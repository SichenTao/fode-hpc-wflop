#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 resumable all-core H6 and paper-native campaign.
Paper/DOI: Wilson et al., Evolutionary Computation for Wind Farm Layout
Optimization; 10.1016/j.renene.2018.03.052.
Paper protocol: four submitted methods, five hidden competition scenarios and
a shared allowance of 10000 complete WindFLO calls, represented by the
published 2000-call per-scenario comparison. The paper reports one competition
result per method/scenario; it does not report independent statistical repeats.
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license.
Missing/conflicts and reconstruction:
hpc/core99_cpp/include/core99/windflo_t12.hpp.
Method semantic ID: t12_four_competition_methods_v1.
Problem semantic ID: t12_windflo_2015_five_scenarios_v1.
Protocol semantic ID: t12_five_scenarios_four_methods_single_competition_run_v1.
HPC rule: every role requests all available Waffle cores. Stateful trajectories
remain ordered; independent candidates and direction calculations use the
persistent team. Formal roles are checkpointed independently and fast methods
finish across all scenarios before the expensive 3s-MDE roles begin.
Claim boundary: academic flexible reproduction, not author random-state,
numeric-result or runtime replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
from datetime import UTC, datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
from typing import Any


METHOD = "t12_four_competition_methods_v1"
PROBLEM = "t12_windflo_2015_five_scenarios_v1"
PROTOCOL = "t12_five_scenarios_four_methods_single_competition_run_v1"
FAST_ALGORITHMS = (
    "t12_goldman_lattice",
    "t12_cmaes_geometric",
    "t12_sshh",
)
EXPENSIVE_ALGORITHMS = ("t12_3s_mde",)
ALGORITHMS = (*FAST_ALGORITHMS, *EXPENSIVE_ALGORITHMS)
SCENARIOS = tuple(range(5))
PAPER_COST_ANCHORS = {
    "t12_3s_mde": [
        1.164422e-3, 1.00929e-3, 6.26867e-4, 6.53861e-4, 1.142309e-3,
    ],
    "t12_cmaes_geometric": [
        1.172731e-3, 1.029998e-3, 6.30916e-4, 6.5356e-4, 1.152661e-3,
    ],
    "t12_sshh": [
        1.181129e-3, 1.039825e-3, 6.40241e-4, 6.66205e-4, 1.167168e-3,
    ],
    "t12_goldman_lattice": [
        1.185466e-3, 1.044906e-3, 6.49096e-4, 6.64341e-4, 1.16033e-3,
    ],
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
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def valid_role(
    path: Path,
    algorithm: str,
    scenario: int,
    seed: int,
    workers: int,
    physical_fes: int,
    source_commit: str,
    binary_hash: str,
) -> bool:
    if not path.is_file():
        return False
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        value.get("algorithm_id") == algorithm
        and value.get("problem_id") == f"t12_windflo_s{scenario + 1}"
        and value.get("method_semantic_id") == METHOD
        and value.get("problem_semantic_id") == PROBLEM
        and value.get("protocol_semantic_id") == PROTOCOL
        and value.get("seed") == seed
        and value.get("requested_workers") == workers
        and value.get("physical_fes_limit") == physical_fes
        and value.get("physical_fes", 0) > 0
        and value.get("best_constraint_violation_m", 1.0) <= 1.0e-8
        and value.get("source_commit") == source_commit
        and value.get("binary_sha256") == binary_hash
        and bool(value.get("scientific_hash"))
    )


def run_role(
    args: argparse.Namespace,
    root: Path,
    algorithm: str,
    scenario: int,
) -> tuple[dict[str, Any], bool]:
    output = root / "roles" / f"scenario-{scenario + 1:02d}" / (
        f"{algorithm}.json"
    )
    binary_hash = sha256(args.binary)
    reused = valid_role(
        output,
        algorithm,
        scenario,
        args.seed,
        args.workers,
        args.physical_fes,
        args.source_commit,
        binary_hash,
    )
    if reused:
        return json.loads(output.read_text(encoding="utf-8")), True

    output.parent.mkdir(parents=True, exist_ok=True)
    raw = output.with_suffix(".raw.json")
    subprocess.run(
        [
            str(args.binary),
            "--scenario", str(scenario),
            "--algorithm", algorithm,
            "--seed", str(args.seed),
            "--physical-fes-limit", str(args.physical_fes),
            "--workers", str(args.workers),
            "--output", str(raw),
        ],
        check=True,
    )
    payload = json.loads(raw.read_text(encoding="utf-8"))
    raw.unlink()
    payload.update({
        "schema_version": 1,
        "corpus_id": "T12",
        "protocol_semantic_id": PROTOCOL,
        "campaign_role": "paper_native_method_scenario",
        "campaign_scenario": scenario + 1,
        "campaign_algorithm": algorithm,
        "campaign_seed": args.seed,
        "source_commit": args.source_commit,
        "binary_sha256": binary_hash,
    })
    require(payload["method_semantic_id"] == METHOD, "T12 method identity")
    require(payload["problem_semantic_id"] == PROBLEM, "T12 problem identity")
    require(payload["problem_id"] == f"t12_windflo_s{scenario + 1}",
            "T12 scenario identity")
    require(payload["requested_workers"] == args.workers,
            "T12 all-core request")
    require(payload["observed_workers"] > 1,
            "T12 multicore participation")
    require(payload["physical_fes"] > 0
            and payload["physical_fes"] <= args.physical_fes,
            "T12 physical-work accounting")
    require(payload["best_constraint_violation_m"] <= 1.0e-8,
            "T12 final feasibility")
    require(payload["best_energy_cost"] > 0.0,
            "T12 finite positive energy cost")
    write_json(output, payload)
    return payload, False


def receipt(row: dict[str, Any], reused: bool) -> dict[str, Any]:
    return {
        "algorithm_id": row["algorithm_id"],
        "scenario": row["campaign_scenario"],
        "seed": row["seed"],
        "physical_fes": row["physical_fes"],
        "best_energy_cost": row["best_energy_cost"],
        "paper_energy_cost_anchor": PAPER_COST_ANCHORS[
            row["algorithm_id"]
        ][row["campaign_scenario"] - 1],
        "best_constraint_violation_m": row["best_constraint_violation_m"],
        "requested_workers": row["requested_workers"],
        "observed_workers": row["observed_workers"],
        "evaluator_seconds": row["evaluator_seconds"],
        "algorithm_seconds": row["algorithm_seconds"],
        "end_to_end_seconds": row["end_to_end_seconds"],
        "scientific_hash": row["scientific_hash"],
        "reused": reused,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--physical-fes", type=int, default=2000)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.output_root = args.output_root.resolve()
    require(args.binary.is_file(), "T12 binary missing")
    require(args.workers >= 4, "T12 formal run requires all-core allocation")
    require(args.physical_fes == 2000,
            "T12 paper campaign requires 2000 calls per scenario")
    args.output_root.mkdir(parents=True, exist_ok=True)

    records: list[dict[str, Any]] = []
    execution_order = [
        (algorithm, scenario)
        for algorithm in FAST_ALGORITHMS
        for scenario in SCENARIOS
    ] + [
        (algorithm, scenario)
        for algorithm in EXPENSIVE_ALGORITHMS
        for scenario in SCENARIOS
    ]
    for index, (algorithm, scenario) in enumerate(execution_order, start=1):
        row, reused = run_role(
            args, args.output_root, algorithm, scenario
        )
        records.append(receipt(row, reused))
        print(
            f"t12_formal completed={index}/20 scenario={scenario + 1} "
            f"algorithm={algorithm} wall={row['end_to_end_seconds']:.6f}",
            flush=True,
        )

    require(len(records) == 20, "T12 formal role matrix incomplete")
    require({(row["algorithm_id"], row["scenario"]) for row in records}
            == {(algorithm, scenario + 1)
                for algorithm in ALGORITHMS for scenario in SCENARIOS},
            "T12 formal role identities differ")
    manifest = {
        "schema_version": 2,
        "campaign_id": "core99_t12_five_scenario_four_method_native_v2",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "corpus_id": "T12",
        "source_commit": args.source_commit,
        "binary_sha256": sha256(args.binary),
        "paper_doi": "10.1016/j.renene.2018.03.052",
        "problem_semantic_id": PROBLEM,
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "paper_native_roles": 20,
        "paper_reported_independent_repeats": 1,
        "platform_repeat_extension": "not part of paper-native admission",
        "five_scenario_physical_fes_per_method": 10000,
        "per_scenario_physical_fes_limit": args.physical_fes,
        "workers_per_role": args.workers,
        "execution_policy": {
            "persistent_team": "one all-core team per optimization",
            "stateful_order": "trajectory updates remain paper-ordered",
            "parallel_axes": "independent candidates and wind directions",
            "scheduling": "15 fast roles before five 3s-MDE roles",
        },
        "records": records,
        "status": "pass",
        "complete": True,
        "claim_boundary": (
            "paper/source-backed flexible academic reproduction; paper values "
            "are external anchors, not numeric or random-state acceptance targets"
        ),
    }
    write_json(args.output_root / "manifest.json", manifest)
    print(json.dumps({
        "status": "pass",
        "corpus_id": "T12",
        "completed_roles": 20,
        "output_root": str(args.output_root),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
