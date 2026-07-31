#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T07 Waffle H6 and nine-case formal campaign
Paper DOI: 10.1016/j.apenergy.2015.03.139.
Public source: no paper-linked author code or data found.
Missing/reconstruction: see core99/park_t07.hpp and the frozen contract.
Resource rule: one SCP run owns one persistent all-core evaluator team.
Claim boundary: declared paper-equation reconstruction, not author CVX replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import time


CASES = (
    "t07_single_0",
    "t07_single_41",
    "t07_single_90",
    "t07_expected_k033",
    "t07_expected_k030",
    "t07_expected_k035",
    "t07_expected_k040",
    "t07_expected_k045",
    "t07_expected_k050",
)
H6_CASE = "t07_expected_k033"


def affinity() -> str:
    for line in Path("/proc/self/status").read_text().splitlines():
        if line.startswith("Cpus_allowed_list:"):
            return line.split(":", 1)[1].strip()
    return "unknown"


def one(
    *,
    binary: str,
    output: Path,
    source_commit: str,
    case_id: str,
    workers: int,
    scp_iterations: int,
    qp_evaluations: int,
) -> dict:
    if output.exists():
        old = json.loads(output.read_text())
        if (
            old.get("source_commit") == source_commit
            and old.get("case_id") == case_id
            and old.get("requested_workers") == workers
            and old.get("frozen_scp_iterations") == scp_iterations
            and old.get("frozen_qp_evaluations") == qp_evaluations
        ):
            return old
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".tmp")
    started = time.monotonic()
    completed = subprocess.run(
        (
            binary,
            "--case",
            case_id,
            "--workers",
            str(workers),
            "--scp-iterations",
            str(scp_iterations),
            "--qp-evaluations",
            str(qp_evaluations),
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
    payload.update(
        {
            "source_commit": source_commit,
            "frozen_scp_iterations": scp_iterations,
            "frozen_qp_evaluations": qp_evaluations,
            "runner_wall_seconds": time.monotonic() - started,
            "cpu_affinity": affinity(),
        }
    )
    output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    temporary.unlink()
    return payload


def validate(row: dict, case_id: str, workers: int) -> None:
    if row["case_id"] != case_id:
        raise RuntimeError(f"{case_id}: identity")
    if (
        row["method_semantic_id"]
        != "t07_explicit_scp_open_qp_declared_v1"
    ):
        raise RuntimeError(f"{case_id}: method semantics")
    if row["requested_workers"] != workers:
        raise RuntimeError(f"{case_id}: worker request")
    if workers == 20 and row["observed_workers"] != 20:
        raise RuntimeError(f"{case_id}: all-core evaluator team")
    if row["maximum_constraint_violation_m"] > 1e-5:
        raise RuntimeError(f"{case_id}: feasibility")
    if row["final"]["efficiency"] + 1e-13 < row["initial"]["efficiency"]:
        raise RuntimeError(f"{case_id}: retained objective")
    if not row["scientific_hash"]:
        raise RuntimeError(f"{case_id}: scientific hash")


def median(rows: list[dict], field: str) -> float:
    return statistics.median(row[field] for row in rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, default=20)
    parser.add_argument("--h6-observations", type=int, default=5)
    args = parser.parse_args()
    if args.total_workers != 20:
        raise RuntimeError("T07 Waffle campaign requires all 20 cores")
    if args.h6_observations != 5:
        raise RuntimeError("T07 H6 requires five observations")
    root = Path(args.output_root)
    started = time.monotonic()
    h6_rows: dict[int, list[dict]] = {1: [], 20: []}
    for workers in (1, 20):
        # First invocation warms the binary, allocator and instruction cache.
        one(
            binary=args.binary,
            output=root / "h6" / f"warmup-w{workers:02d}.json",
            source_commit=args.source_commit,
            case_id=H6_CASE,
            workers=workers,
            scp_iterations=3,
            qp_evaluations=80,
        )
        for observation in range(1, 6):
            row = one(
                binary=args.binary,
                output=(
                    root / "h6"
                    / f"observation-{observation:02d}-w{workers:02d}.json"
                ),
                source_commit=args.source_commit,
                case_id=H6_CASE,
                workers=workers,
                scp_iterations=10,
                qp_evaluations=100,
            )
            validate(row, H6_CASE, workers)
            h6_rows[workers].append(row)
    for serial, parallel in zip(h6_rows[1], h6_rows[20], strict=True):
        if serial["scientific_hash"] != parallel["scientific_hash"]:
            raise RuntimeError("T07 one/all-core scientific hash")
    serial, parallel = h6_rows[1], h6_rows[20]
    serial_evaluator = median(serial, "evaluator_seconds")
    parallel_evaluator = median(parallel, "evaluator_seconds")
    serial_total = median(serial, "end_to_end_seconds")
    parallel_total = median(parallel, "end_to_end_seconds")
    if not parallel_evaluator < serial_evaluator:
        raise RuntimeError("T07 all-core evaluator did not accelerate")
    h6 = {
        "status": "pass",
        "case_id": H6_CASE,
        "observation_count": 5,
        "cpu_affinity": affinity(),
        "serial": serial,
        "parallel": parallel,
        "median_seconds": {
            "serial_evaluator": serial_evaluator,
            "parallel_evaluator": parallel_evaluator,
            "serial_qp": median(serial, "qp_seconds"),
            "parallel_qp": median(parallel, "qp_seconds"),
            "serial_end_to_end": serial_total,
            "parallel_end_to_end": parallel_total,
        },
        "median_speedup": {
            "evaluator": serial_evaluator / parallel_evaluator,
            "end_to_end": serial_total / parallel_total,
        },
        "parallel_attribution": {
            "evaluator_fraction": parallel_evaluator / parallel_total,
            "qp_fraction": median(parallel, "qp_seconds") / parallel_total,
            "orchestration_fraction": max(
                0.0,
                1.0
                - (
                    parallel_evaluator
                    + median(parallel, "qp_seconds")
                )
                / parallel_total,
            ),
        },
        "claim_boundary":
            "same pure-C++ source, problem and deterministic SCP work; "
            "one versus twenty Waffle evaluator workers",
    }
    (root / "h6").mkdir(parents=True, exist_ok=True)
    (root / "h6" / "summary.json").write_text(
        json.dumps(h6, indent=2, sort_keys=True) + "\n"
    )
    formal = {}
    for index, case_id in enumerate(CASES, 1):
        row = one(
            binary=args.binary,
            output=root / "formal" / f"{case_id}.json",
            source_commit=args.source_commit,
            case_id=case_id,
            workers=20,
            scp_iterations=100,
            qp_evaluations=300,
        )
        validate(row, case_id, 20)
        formal[case_id] = row
        print(f"T07 completed {index}/{len(CASES)}", flush=True)
    summary = {
        "campaign": "T07 Waffle H6 and nine deterministic paper cases",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "source_commit": args.source_commit,
        "formal_run_count": len(CASES),
        "repeat_count_per_case": 1,
        "repeat_reason":
            "paper SCP/CVX procedure is deterministic and reports no seeds",
        "maximum_aggregate_cpu_workers": 20,
        "h6": h6,
        "formal": formal,
        "campaign_wall_seconds": time.monotonic() - started,
        "status": "pass",
        "claim_boundary":
            "academic equation-level reconstruction with declared "
            "geometry, quadrature, discretization and open-QP completions",
    }
    root.mkdir(parents=True, exist_ok=True)
    (root / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
