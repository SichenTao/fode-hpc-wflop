#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T05 Waffle H6 and formal paper campaign
Paper DOI: 10.1016/j.renene.2013.10.023.
Public source: no author code found.
Missing: author CPLEX model and numeric Figure-5 array.
Reconstruction: resumable deterministic open-solver H6/formal execution.
Resource rule: one optimization owns one all-twenty-core persistent team.
Claim boundary: academic reconstruction, not CPLEX replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time


CASES = (
    "t05_case_a_k26",
    "t05_case_a_k30",
    "t05_case_b_k19",
    "t05_case_b_k39",
    "t05_case_c_k15",
    "t05_case_c_k39",
)
H6_CASE = "t05_case_c_k39"


def one(
    binary: str,
    root: Path,
    source_commit: str,
    case_id: str,
    workers: int,
) -> dict:
    path = root / f"{case_id}-workers-{workers:02d}.json"
    if path.exists():
        old = json.loads(path.read_text())
        if (
            old.get("source_commit") == source_commit
            and old.get("requested_workers") == workers
        ):
            return old
    temporary = path.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        (
            binary,
            "--case",
            case_id,
            "--workers",
            str(workers),
            "--seed",
            "201410023",
            "--output",
            str(temporary),
        ),
        text=True,
        capture_output=True,
        timeout=6 * 60 * 60,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text())["runs"][0]
    payload["source_commit"] = source_commit
    payload["runner_wall_seconds"] = time.monotonic() - started
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    temporary.unlink()
    return payload


def validate(row: dict, case_id: str, workers: int) -> None:
    if row["case_id"] != case_id:
        raise RuntimeError(f"{case_id}: identity")
    if row["requested_workers"] != workers:
        raise RuntimeError(f"{case_id}: worker request")
    if workers == 20 and row["observed_workers"] != 20:
        raise RuntimeError(f"{case_id}: all-core team not observed")
    if row["multistarts"] != 4096 or row["node_limit"] != 200000:
        raise RuntimeError(f"{case_id}: frozen solver work")
    if len(row["best_layout"]) != row["turbine_count"]:
        raise RuntimeError(f"{case_id}: cardinality")
    if abs(
        row["qip_objective"] - row["milp_linearized_objective"]
    ) > 2e-12:
        raise RuntimeError(f"{case_id}: formulation equivalence")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    args = parser.parse_args()
    if args.total_workers != 20:
        raise RuntimeError("T05 Waffle campaign requires all 20 cores")
    root = Path(args.output_root)
    started = time.monotonic()
    serial = one(
        args.binary, root / "h6", args.source_commit, H6_CASE, 1
    )
    parallel = one(
        args.binary, root / "h6", args.source_commit, H6_CASE, 20
    )
    validate(serial, H6_CASE, 1)
    validate(parallel, H6_CASE, 20)
    if (
        serial["scientific_hash"] != parallel["scientific_hash"]
        or serial["explored_nodes"] != parallel["explored_nodes"]
    ):
        raise RuntimeError("T05 one/all-core scientific work mismatch")
    h6 = {
        "status": "pass",
        "case_id": H6_CASE,
        "scientific_hash": serial["scientific_hash"],
        "explored_nodes": serial["explored_nodes"],
        "speedup": {
            "interaction_assembly":
                serial["interaction_assembly_seconds"]
                / parallel["interaction_assembly_seconds"],
            "incumbent_search":
                serial["incumbent_search_seconds"]
                / parallel["incumbent_search_seconds"],
            "branch_and_bound":
                serial["branch_and_bound_seconds"]
                / parallel["branch_and_bound_seconds"],
            "power_evaluation":
                serial["power_evaluation_seconds"]
                / parallel["power_evaluation_seconds"],
            "end_to_end":
                serial["end_to_end_seconds"]
                / parallel["end_to_end_seconds"],
        },
        "serial": serial,
        "parallel": parallel,
    }
    (root / "h6" / "summary.json").write_text(
        json.dumps(h6, indent=2, sort_keys=True) + "\n"
    )
    formal = {}
    for index, case_id in enumerate(CASES, 1):
        row = one(
            args.binary, root / "formal", args.source_commit, case_id, 20
        )
        validate(row, case_id, 20)
        formal[case_id] = row
        print(f"T05 completed {index}/{len(CASES)}", flush=True)
    summary = {
        "campaign": "T05 Waffle H6 and six paper-native cases",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "formal_run_count": len(CASES),
        "maximum_aggregate_cpu_workers": 20,
        "resource_mapping":
            "one paper run at a time; one persistent all-core team",
        "h6": h6,
        "formal": formal,
        "campaign_wall_seconds": time.monotonic() - started,
        "status": "pass",
        "claim_boundary":
            "academic equation-level reconstruction; not CPLEX or exact "
            "author numerical replay",
    }
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
