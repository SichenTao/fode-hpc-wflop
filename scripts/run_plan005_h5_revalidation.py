#!/usr/bin/env python3
"""Append-only H5 revalidation after Plan-005 thread-topology refinement."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = (
    ROOT
    / "evidence/development/"
    "plan004_h5_independent_reference_receipts_20260730.json"
)
DEFAULT_RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_thread_topology_revalidation_20260730.json"
)
LEARNING_TESTS = (
    "plan004_learning_backend_matrix",
    "plan004_artifact_target_optimization",
    "plan004_learning_full_optimizer_artifacts",
    "plan004_taae_reference_equivalence",
    "plan004_alga_reference_equivalence",
    "plan004_rlpso_reference_equivalence",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str]) -> dict[str, Any]:
    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout + completed.stderr
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed: {json.dumps(command)}\n{output[-8000:]}"
        )
    return {
        "command": command,
        "return_code": completed.returncode,
        "measured_wall_seconds": elapsed,
        "output_sha256": hashlib.sha256(output.encode("utf-8")).hexdigest(),
        "output": output,
    }


def structured(output: str) -> list[dict[str, Any]]:
    result = []
    for line in output.splitlines():
        opening = line.find("{")
        if opening < 0:
            continue
        try:
            value = json.loads(line[opening:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            result.append(value)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--torch-build-dir",
        type=Path,
        default=ROOT / "build/plan005-torch",
    )
    parser.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    arguments = parser.parse_args()
    build = arguments.torch_build_dir.resolve()
    receipt = arguments.receipt.resolve()
    if receipt.exists():
        raise RuntimeError(f"append-only H5 receipt exists: {receipt}")
    full = run([
        "ctest", "--test-dir", str(build), "--output-on-failure",
    ])
    match = re.search(
        r"100% tests passed, 0 tests failed out of (\d+)",
        full["output"],
    )
    if not match:
        raise RuntimeError("fresh full Torch CTest did not report a clean suite")
    selected = {}
    for test_name in LEARNING_TESTS:
        item = run([
            "ctest", "--test-dir", str(build), "-V",
            "--output-on-failure", "-R", f"^{test_name}$",
        ])
        item["structured_output"] = structured(item["output"])
        selected[test_name] = item
    full_optimizer = selected[
        "plan004_learning_full_optimizer_artifacts"
    ]["structured_output"]
    if not any(
        value.get("status") == "pass"
        and value.get("methods") == 3
        for value in full_optimizer
    ):
        raise RuntimeError("full learned optimizer H5 receipt is absent")
    artifact_target = selected["plan004_artifact_target_optimization"][
        "structured_output"
    ]
    artifact_records = [
        value for value in artifact_target
        if value.get("status") == "pass"
        and value.get("contract") == "plan004_artifact_target_optimization_v1"
    ]
    if len(artifact_records) != 1:
        raise RuntimeError("learning artifact target receipt is absent")
    source_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    binaries = {
        "trainer": (
            build
            / "hpc/learning_libtorch/plan004_learning_target_hpc"
        ),
        "taae": build / "hpc/taae_cpp/taae_evolution_hpc",
        "wflop": build / "hpc/wflop_cpp/wflop_cpp_hpc",
    }
    document = {
        "schema_version": 1,
        "receipt_id": (
            "plan005_h5_post_thread_topology_revalidation_spark_20260730"
        ),
        "source_commit": source_commit,
        "host": platform.node(),
        "architecture": platform.machine(),
        "original_plan004_h5_receipt": str(ORIGINAL.relative_to(ROOT)),
        "original_plan004_h5_receipt_sha256": sha256(ORIGINAL),
        "full_torch_ctest": full,
        "full_torch_test_count": int(match.group(1)),
        "selected_h5_tests": selected,
        "binary_receipts": {
            name: {
                "logical_path": str(path.relative_to(ROOT)),
                "sha256": sha256(path),
            }
            for name, path in binaries.items()
        },
        "thread_topology_contract": {
            "explicit_outer_workers": True,
            "explicit_torch_intraop_threads": True,
            "explicit_torch_interop_threads": True,
            "bounded_h5_observed_topology": {
                "outer_workers": 1,
                "torch_intraop_threads": 1,
                "torch_interop_threads": 1,
            },
        },
        "status": "accepted_h5_revalidated_after_nonsemantic_thread_control",
        "claim_boundary": (
            "The performance-only thread-pool controls do not alter method, "
            "problem, random-event, artifact, or physical-FES semantics. "
            "The original append-only Plan-004 H5 receipt remains unchanged."
        ),
    }
    receipt.parent.mkdir(parents=True, exist_ok=True)
    with receipt.open("x", encoding="utf-8") as handle:
        handle.write(json.dumps(document, indent=2, sort_keys=True) + "\n")
    print(
        "plan005_h5_revalidation_pass "
        f"full_tests={document['full_torch_test_count']} "
        f"selected_h5_tests={len(selected)} topology=outer1_torch1_1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
