#!/usr/bin/env python3
"""Fail-closed audit for Plan-005 prepared and H6-final manifests."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from plan005_formal_common import (
    FINAL_MANIFEST,
    PREPARED_MANIFEST,
    build_manifest,
    validate_manifest,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.add_argument("--strict", action="store_true", required=True)
    parser.add_argument("--prepared", action="store_true")
    parser.add_argument("--manifest", type=Path)
    arguments = parser.parse_args()
    path = (
        arguments.manifest.resolve()
        if arguments.manifest is not None
        else (PREPARED_MANIFEST if arguments.prepared else FINAL_MANIFEST)
    )
    if not path.is_file():
        raise RuntimeError(f"manifest absent: {path}")
    observed = json.loads(path.read_text(encoding="utf-8"))
    validate_manifest(observed, prepared=arguments.prepared)
    expected = build_manifest(prepared=arguments.prepared)
    if observed != expected:
        raise RuntimeError("manifest differs from current authoritative inputs")
    print(
        "plan005_formal_manifest_audit_pass "
        f"status={observed['status']} targets={observed['target_count']} "
        f"cases={observed['case_count']} "
        f"runs={observed['optimization_run_count']} "
        "non_target_baselines=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
