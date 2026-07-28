#!/usr/bin/env python3
"""Validate one complete 24-case BDE source-replay formal seed."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--result", type=Path, required=True)
    parser.add_argument("--cases", type=Path, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--workers", type=int, required=True)
    parser.add_argument("--physical-fes", type=int, required=True)
    arguments = parser.parse_args()

    manifest = json.loads(arguments.cases.read_text())
    case_by_id = {case["case_id"]: case for case in manifest["cases"]}
    lines = [
        line for line in arguments.result.read_text().splitlines()
        if line.strip()
    ]
    if len(lines) != len(case_by_id) or len(case_by_id) != 24:
        raise RuntimeError(
            f"expected 24 records, got {len(lines)} for {len(case_by_id)} cases"
        )
    records = [json.loads(line) for line in lines]
    if {record["case_id"] for record in records} != set(case_by_id):
        raise RuntimeError("formal seed case set differs from frozen manifest")
    for record in records:
        case = case_by_id[record["case_id"]]
        required = {
            "algorithm_id": "bde",
            "effective_semantics_id": "bde_paper_equations_physical_fes_v1",
            "problem_id": "bde2025_source_replay_ws1_ws4",
            "problem_semantics_id": "bde2025_source_replay_ws1_ws4_v1",
            "seed": arguments.seed,
            "physical_fes": arguments.physical_fes,
            "requested_workers": arguments.workers,
            "observed_workers": arguments.workers,
        }
        for field, expected in required.items():
            if record.get(field) != expected:
                raise RuntimeError(
                    f"{record['case_id']}: {field}={record.get(field)!r}, "
                    f"expected {expected!r}"
                )
        objective = float(record["best_expected_power_kw"])
        if not math.isfinite(objective) or objective <= 0.0:
            raise RuntimeError(f"{record['case_id']}: invalid objective")
        layout = [int(value) for value in record["best_layout_1based"]]
        turbine_count = int(case["turbine_count"])
        if len(layout) != turbine_count or len(set(layout)) != turbine_count:
            raise RuntimeError(f"{record['case_id']}: invalid layout cardinality")
        unavailable = set(case["unavailable_cells_1based"])
        grid_size = int(case["rows"]) * int(case["cols"])
        if (
            layout != sorted(layout)
            or min(layout) < 1
            or max(layout) > grid_size
            or unavailable.intersection(layout)
        ):
            raise RuntimeError(f"{record['case_id']}: infeasible layout")
    print(
        "bde_source_formal_seed_validation_pass "
        f"seed={arguments.seed} records={len(records)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
