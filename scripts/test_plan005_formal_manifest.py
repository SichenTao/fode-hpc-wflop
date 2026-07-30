#!/usr/bin/env python3
"""Bounded structural test for the Plan-005 target-native manifest."""

from __future__ import annotations

import copy

from plan005_formal_common import build_manifest, validate_manifest


def expect_failure(document: dict, message: str) -> None:
    try:
        validate_manifest(document, prepared=True)
    except RuntimeError:
        return
    raise RuntimeError(message)


def main() -> int:
    document = build_manifest(prepared=True)
    validate_manifest(document, prepared=True)
    if document["case_count"] != 1153:
        raise RuntimeError(
            f"paper-native case count drift: {document['case_count']}"
        )
    if document["optimization_run_count"] != 28825:
        raise RuntimeError(
            "25-seed optimization run count drift: "
            f"{document['optimization_run_count']}"
        )

    baseline = copy.deepcopy(document)
    baseline["non_target_baselines_in_readiness"] = 1
    expect_failure(baseline, "non-target baseline tamper was accepted")

    invented_h6 = copy.deepcopy(document)
    invented_h6["campaigns"][0]["backend"]["selected_workers"] = 20
    expect_failure(invented_h6, "prepared manifest invented an H6 selection")

    duplicate = copy.deepcopy(document)
    duplicate["campaigns"][0]["cases"][1]["case_id"] = duplicate[
        "campaigns"
    ][0]["cases"][0]["case_id"]
    expect_failure(duplicate, "duplicate native case was accepted")

    print(
        "plan005_formal_manifest_fixture_pass "
        "targets=23 cases=1153 seeds=25 runs=28825 "
        "rejected_baseline=1 rejected_invented_h6=1 "
        "rejected_duplicate_case=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
