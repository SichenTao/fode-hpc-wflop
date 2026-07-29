#!/usr/bin/env python3
"""Audit all Plan-004 independent-reference H5 validation receipts."""

from __future__ import annotations

import csv
import hashlib
import json
import math
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
LEARNING = {"Y36", "T42", "T45"}
DIRECT_COVERAGE = (
    "forward_tensors",
    "losses",
    "all_named_parameter_gradients",
    "one_optimizer_step",
    "artifact_reload",
    "artifact_driven_transition",
    "physical_fes",
    "random_event_ownership",
    "terminal_partial_work",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def finite_nonnegative(value: Any) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(value)
        and value >= 0.0
    )


def main() -> int:
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    require(len(rows) == 23, f"expected 23 core rows, found {len(rows)}")
    accepted = 0
    receipt_paths: set[Path] = set()
    for row in rows:
        analysis = ROOT / row["analysis_path"]
        validation = analysis.with_name(
            analysis.name.replace(
                "_hpc_analysis.json", "_hpc_validation.json"
            )
        )
        require(validation.is_file(), f"{row['pair_id']}: validation absent")
        data = json.loads(validation.read_text(encoding="utf-8"))
        h5 = data["H5_bounded_equivalence"]
        require(data["schema_version"] == 2, f"{row['pair_id']}: schema")
        require(data["pair_id"] == row["pair_id"], f"{row['pair_id']}: pair")
        require(
            data["analysis_sha256"] == sha256(analysis),
            f"{row['pair_id']}: analysis hash",
        )
        require(
            data["native_asset_sha256"]
            == sha256(ROOT / row["native_asset"]),
            f"{row['pair_id']}: native hash",
        )
        require(h5["status"] == "accepted_h5", f"{row['pair_id']}: H5")
        require(
            data["overall_status"] == "accepted_h5_pending_h6",
            f"{row['pair_id']}: overall",
        )
        require(
            data["H6_performance_validation"]["status"]
            == "pending_h6_measurement"
            and data["H6_performance_validation"]["accepted_backend"] is None,
            f"{row['pair_id']}: H6",
        )
        independent = h5.get("independent_reference", {})
        require(
            independent.get("does_not_call_candidate") is True,
            f"{row['pair_id']}: independent reference",
        )
        require(
            bool(independent.get("implementation_or_oracle")),
            f"{row['pair_id']}: oracle identity",
        )
        require(
            bool(independent.get("ctest_names")),
            f"{row['pair_id']}: oracle tests",
        )
        comparison = h5.get("numerical_comparison", {})
        require(
            comparison.get("passed") is True
            and finite_nonnegative(
                comparison.get("maximum_absolute_error")
            ),
            f"{row['pair_id']}: numerical comparison",
        )
        receipt = ROOT / h5["runtime_test_receipt"]
        receipt_paths.add(receipt)
        require(
            sha256(receipt) == h5["runtime_test_receipt_sha256"],
            f"{row['pair_id']}: receipt hash",
        )
        raw = json.loads(receipt.read_text(encoding="utf-8"))
        require(
            raw["schema_version"] == 2
            and raw["independent_reference_required"] is True,
            f"{row['pair_id']}: raw receipt schema",
        )
        tests = raw["tests"]
        runtime = tests.get(h5["runtime_test"])
        require(
            runtime
            and runtime["return_code"] == 0
            and runtime["output_sha256"]
            == h5["runtime_test_output_sha256"]
            and "100% tests passed" in runtime["output"],
            f"{row['pair_id']}: runtime receipt",
        )
        primary_test = independent["ctest_names"][0]
        primary = tests.get(primary_test)
        require(
            primary
            and primary["return_code"] == 0
            and primary["output_sha256"]
            == independent["primary_test_output_sha256"],
            f"{row['pair_id']}: independent test receipt",
        )
        numerical_records = [
            record
            for record in primary["structured_output"]
            if record.get("status") == "pass"
            and "maximum_absolute_error" in record
        ]
        require(
            len(numerical_records) == 1
            and numerical_records[0]["maximum_absolute_error"]
            == comparison["maximum_absolute_error"],
            f"{row['pair_id']}: numerical receipt mismatch",
        )
        for additional_test in independent["ctest_names"][1:]:
            require(
                tests.get(additional_test, {}).get("return_code") == 0,
                f"{row['pair_id']}: additional independent test",
            )
        if row["corpus_id"] in LEARNING:
            require(
                h5["runtime_test"]
                == "plan004_learning_full_optimizer_artifacts",
                f"{row['pair_id']}: full optimizer absent",
            )
            require(
                finite_nonnegative(
                    comparison.get("absolute_tolerance_per_tensor")
                )
                and isinstance(
                    comparison["observed_result"].get("tolerances"),
                    dict,
                ),
                f"{row['pair_id']}: per-tensor tolerances absent",
            )
            for field in DIRECT_COVERAGE:
                require(
                    h5.get("coverage", {}).get(field) == "passed",
                    f"{row['pair_id']}: coverage {field}",
                )
        accepted += 1
    require(
        len(receipt_paths) == 1,
        f"expected one immutable raw receipt, found {len(receipt_paths)}",
    )
    print(
        "hpc_core_h5_validation_audit_pass "
        f"pairs={accepted} independent_references={accepted} "
        f"learning_pairs={len(LEARNING)} h6_status=pending"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
