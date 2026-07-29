#!/usr/bin/env python3
"""Regression test for Step-1 draft versus Step-2 strict core semantics."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT = ROOT / "scripts/audit_hpc_theory_plans.py"
FIXTURE = ROOT / "scripts/fixtures/hpc_theory_draft_core_fixture.json"


def main() -> int:
    specification = importlib.util.spec_from_file_location(
        "audit_hpc_theory_plans", AUDIT
    )
    if specification is None or specification.loader is None:
        raise RuntimeError("cannot import theory-plan audit")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    data = json.loads(FIXTURE.read_text(encoding="utf-8"))
    row = {
        "pair_id": data["pair_id"],
        "corpus_id": data["corpus_id"],
        "method_semantic_id": data["method_semantic_id"],
        "problem_semantic_id": data["problem_semantic_id"],
    }

    module.require_core_review(data, row, allow_draft=True)
    try:
        module.require_core_review(data, row, allow_draft=False)
    except RuntimeError as error:
        if "target review absent" not in str(error):
            raise
    else:
        raise RuntimeError("strict core mode admitted an unreviewed draft")

    accepted = {
        **data,
        "review_status": "reviewed_plan003_target_specific",
        "pair_specific_boundary": (
            "FIXTURE:fixture_method_v1 on fixture_problem_v1"
        ),
    }
    module.require_core_review(accepted, row, allow_draft=False)
    print("hpc_theory_draft_mode_regression_pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
