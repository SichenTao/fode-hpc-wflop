#!/usr/bin/env python3
"""Generate the prepared or H6-final Plan-005 target-native manifest."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path

from plan005_formal_common import (
    FINAL_MANIFEST,
    PREPARED_MANIFEST,
    build_manifest,
    validate_manifest,
)


def atomic_create(path: Path, document: dict) -> None:
    if path.exists():
        raise RuntimeError(f"append-only manifest exists: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("x", encoding="utf-8") as handle:
        handle.write(
            json.dumps(
                document,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            + "\n"
        )
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.add_argument("--seeds", type=int, choices=(25,), required=True)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    document = build_manifest(prepared=arguments.prepare_only)
    validate_manifest(document, prepared=arguments.prepare_only)
    output = (
        arguments.output.resolve()
        if arguments.output is not None
        else (
            PREPARED_MANIFEST
            if arguments.prepare_only
            else FINAL_MANIFEST
        )
    )
    atomic_create(output, document)
    print(
        "plan005_formal_manifest_generation_pass "
        f"status={document['status']} targets={document['target_count']} "
        f"cases={document['case_count']} "
        f"runs={document['optimization_run_count']} "
        f"ready_cpu={document['ready_cpu_target_count']} "
        f"deferred_learning={document['deferred_learning_target_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
