#!/usr/bin/env python3
"""Materialize the Plan-003 target-only HPC authority.

The 133-row Plan-002 registry is intentionally retained as an immutable
inventory.  This generator projects its one target row per completed paper
into the independent 23-row Plan-003 authority.
"""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/hpc_required_pairs.tsv"
OUTPUT = ROOT / "docs/hpc_core_target_pairs.tsv"


def main() -> int:
    with SOURCE.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    targets = [row for row in rows if row["role"] == "target"]
    if len(targets) != 23:
        raise RuntimeError(f"expected 23 target rows, observed {len(targets)}")
    fields = list(rows[0])
    with OUTPUT.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=fields, delimiter="\t", lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(targets)
    print(f"hpc_core_target_scope_generated targets={len(targets)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
