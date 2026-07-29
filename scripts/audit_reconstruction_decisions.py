#!/usr/bin/env python3
"""Validate reconstruction decisions before an incomplete profile executes."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--corpus-id")
    args = parser.parse_args()
    schema = json.loads(
        (ROOT / "shared/contracts/reconstruction_decision_schema.json")
        .read_text(encoding="utf-8")
    )
    required = set(schema["required_decision_fields"])
    classifications = set(schema["classifications"])
    root = ROOT / "shared/contracts/reconstruction-decisions"
    paths = [root / f"{args.corpus_id}.json"] if args.corpus_id else sorted(root.glob("*.json"))
    if not paths or any(not path.exists() for path in paths):
        raise RuntimeError("requested reconstruction decision ledger is absent")
    count = 0
    for path in paths:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if not payload["profiles"] or not payload["decisions"]:
            raise RuntimeError(f"{path.stem}: empty profiles or decisions")
        for decision in payload["decisions"]:
            missing = required.difference(decision)
            if missing:
                raise RuntimeError(f"{path.stem}: missing {sorted(missing)}")
            if decision["classification"] not in classifications:
                raise RuntimeError(f"{path.stem}: invalid classification")
            if not decision["rejected_alternatives"]:
                raise RuntimeError(f"{path.stem}: no rejected alternative")
            if decision["sensitivity_test_required"] not in {True, False}:
                raise RuntimeError(f"{path.stem}: invalid sensitivity obligation")
            count += 1
    print(
        f"reconstruction_decision_audit_pass ledgers={len(paths)} decisions={count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
