#!/usr/bin/env python3
"""Write the exact, non-admitting Step-8 blocker receipt."""

from __future__ import annotations

import csv
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = (
    ROOT
    / "evidence/development/plan002_step8_blockers_spark2_20260730.json"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    registry = ROOT / "docs/hpc_required_pairs.tsv"
    with registry.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    missing = [
        {
            "pair_id": row["pair_id"],
            "corpus_id": row["corpus_id"],
            "algorithm_id": row["algorithm_id"],
            "problem_id": row["problem_id"],
        }
        for row in rows
        if row["implementation_status"]
        == "planned_missing_native_comparator"
    ]
    taae_path = (
        ROOT
        / "evidence/development/"
        "taae_paper_scale_cpu_feasibility_spark2_20260730.json"
    )
    taae = json.loads(taae_path.read_text(encoding="utf-8"))
    receipt = {
        "schema_version": 1,
        "plan": "002-complete-paper-paired-gao-tao-wflop-hpc-benchmark",
        "step": 8,
        "status": "profile_and_dependency_stop",
        "required_pair_registry": {
            "path": str(registry.relative_to(ROOT)),
            "sha256": sha256(registry),
            "pair_count": len(rows),
        },
        "missing_native_implementation_pairs": missing,
        "missing_native_implementation_count": len(missing),
        "draft_h0_h4_pair_count": sum(
            row.get("theory_status")
            == "draft_h0_h4_scaffold_unadmitted"
            for row in rows
        ),
        "h5_h6_accepted_pair_count": sum(
            row["validation_status"] == "accepted_h5_h6"
            for row in rows
        ),
        "taae_training_stop": {
            "path": str(taae_path.relative_to(ROOT)),
            "sha256": sha256(taae_path),
            "status": taae["status"],
            "measured_probe_seconds": taae["bounded_fixed_work_probe"][
                "wall_seconds"
            ],
            "projected_paper_scale_seconds": taae[
                "measured_linear_projection"
            ]["projected_wall_seconds"],
            "projected_paper_scale_days": taae[
                "measured_linear_projection"
            ]["projected_wall_days"],
        },
        "scientific_boundary": (
            "No formal campaign may start until every required native pair "
            "has an accepted pair-specific H0-H6 chain. Draft dossiers and "
            "registered comparators are not performance admission."
        ),
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "plan002_step8_blocker_snapshot_written "
        f"pairs={len(rows)} missing={len(missing)} output={OUTPUT}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
