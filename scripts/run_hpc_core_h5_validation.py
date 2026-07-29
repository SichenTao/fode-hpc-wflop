#!/usr/bin/env python3
"""Run and persist bounded H5 validation for the 23 target-only pairs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import platform
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
COVERAGE = ROOT / "shared/contracts/hpc_core_cpu_runtime_coverage.json"
LEARNING = {"Y36", "T42", "T45"}
RAW_RECEIPT = (
    ROOT
    / "evidence/development/plan003_h5_ctest_receipts_20260730.json"
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ctest(
    build_dir: Path, test_name: str
) -> dict[str, object]:
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "--output-on-failure",
        "-R",
        f"^{test_name}$",
    ]
    started = time.perf_counter()
    completed = subprocess.run(
        command, capture_output=True, text=True
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout + completed.stderr
    if completed.returncode != 0 or "100% tests passed" not in output:
        raise RuntimeError(
            f"{test_name}: bounded H5 CTest failed\n{output}"
        )
    return {
        "test_name": test_name,
        "command": command,
        "return_code": completed.returncode,
        "measured_wall_seconds": elapsed,
        "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
        "output": output,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cpu-build-dir", type=Path, required=True)
    parser.add_argument("--torch-build-dir", type=Path, required=True)
    args = parser.parse_args()
    cpu_build = args.cpu_build_dir.resolve()
    torch_build = args.torch_build_dir.resolve()

    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))
    route: dict[str, str] = {}
    for group in coverage["coverage_groups"]:
        for corpus_id in group["corpus_ids"]:
            route[corpus_id] = group["ctest_name"]

    receipts: dict[str, dict[str, object]] = {}
    for test_name in sorted(set(route.values())):
        receipts[test_name] = ctest(cpu_build, test_name)
    receipts["hpc_learning_libtorch_backends"] = ctest(
        torch_build, "hpc_learning_libtorch_backends"
    )
    raw = {
        "schema_version": 1,
        "receipt_id": "plan003_h5_ctest_receipts_spark_20260730",
        "host": platform.node(),
        "machine": platform.machine(),
        "logical_cpu_count": os.cpu_count(),
        "scope": "23 target-only pairs",
        "tests": receipts,
        "claim_boundary": (
            "Bounded H5 runtime evidence only; it is not H6 performance "
            "evidence or formal campaign quality evidence."
        ),
    }
    RAW_RECEIPT.parent.mkdir(parents=True, exist_ok=True)
    RAW_RECEIPT.write_text(
        json.dumps(raw, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    raw_sha = sha256(RAW_RECEIPT)

    written = 0
    for row in rows:
        corpus_id = row["corpus_id"]
        analysis = ROOT / row["analysis_path"]
        validation_path = analysis.with_name(
            analysis.name.replace(
                "_hpc_analysis.json", "_hpc_validation.json"
            )
        )
        runtime_test = route[corpus_id]
        candidate_backends = ["cpu_hpc_v1"]
        comparison_scope = (
            "reference CPU versus persistent-team CPU: exact discrete "
            "state, FES, layout/front/artifact hashes, with floating "
            "values checked by the routed target test"
        )
        if corpus_id in LEARNING:
            candidate_backends.extend(
                [
                    "libtorch_cpu_hpc_v1",
                    "libtorch_gpu_hpc_v1",
                    "libtorch_hybrid_cpu_gpu_hpc_v1",
                ]
            )
            comparison_scope += (
                "; the registered LibTorch kernel is additionally checked "
                "CPU/CUDA/hybrid with full canonical inference vectors at "
                "max_abs_error <= 1e-8 and exact per-backend artifact replay"
            )
        data = {
            "schema_version": 1,
            "validation_id": (
                f"plan003_h5__{row['pair_id']}__spark_20260730"
            ),
            "pair_id": row["pair_id"],
            "corpus_id": corpus_id,
            "method_semantic_id": row["method_semantic_id"],
            "problem_semantic_id": row["problem_semantic_id"],
            "paper_protocol_id": row["paper_protocol_id"],
            "analysis_path": row["analysis_path"],
            "analysis_sha256": sha256(analysis),
            "native_asset": row["native_asset"],
            "native_asset_sha256": sha256(ROOT / row["native_asset"]),
            "H5_bounded_equivalence": {
                "status": "accepted_h5",
                "reference_backend": "bounded_reference_cpu_v1",
                "candidate_backends": candidate_backends,
                "runtime_test": runtime_test,
                "runtime_test_receipt": str(
                    RAW_RECEIPT.relative_to(ROOT)
                ),
                "runtime_test_receipt_sha256": raw_sha,
                "runtime_test_output_sha256": receipts[runtime_test][
                    "output_sha256"
                ],
                "comparison_scope": comparison_scope,
                "learning_backend_test": (
                    "hpc_learning_libtorch_backends"
                    if corpus_id in LEARNING
                    else None
                ),
                "learning_backend_output_sha256": (
                    receipts["hpc_learning_libtorch_backends"][
                        "output_sha256"
                    ]
                    if corpus_id in LEARNING
                    else None
                ),
                "physical_fes_ledger": (
                    "target runtime test checks the declared bounded "
                    "physical-FES ledger; LibTorch training records zero "
                    "physical objective calls"
                ),
            },
            "H6_performance_validation": {
                "status": "pending_h6_measurement",
                "accepted_backend": None,
            },
            "overall_status": "accepted_h5_pending_h6",
            "claim_boundary": (
                "H5 is accepted only for bounded implementation "
                "equivalence. No backend is selected until exact-pair H6, "
                "and no formal-quality claim is made."
            ),
        }
        validation_path.write_text(
            json.dumps(data, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        written += 1
    print(
        "hpc_core_h5_validation_pass "
        f"pairs={written} ctests={len(receipts)} "
        "status=accepted_h5_pending_h6"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
