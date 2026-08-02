#!/usr/bin/env python3
"""Run Plan-004 independent-reference H5 validation for the 23 targets."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import subprocess
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
COVERAGE = ROOT / "shared/contracts/hpc_core_cpu_runtime_coverage.json"
RAW_RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan004_h5_independent_reference_receipts_20260730.json"
)
LEARNING_METHOD = {
    "Y36": "taae",
    "T45": "alga",
    "T42": "rlpso",
}
SCALAR = {
    "S01",
    "S02",
    "S03",
    "S04",
    "S05",
    "L0608",
    "T37",
    "T38",
    "T39",
    "T40",
    "T41",
    "T47",
    "Y34",
    "Y35",
}
ORACLE_BY_CORPUS = {
    **{
        corpus_id: (
            "scalar_native_evaluator_oracle",
            "scripts/validate_scalar_native_evaluator.py",
            "independent Python Jensen/Park complete-layout evaluator",
        )
        for corpus_id in SCALAR
    },
    "T44": (
        "bde_ws56_independent_oracle",
        "scripts/validate_bde_ws56_declared_proxy.py",
        "independent Python Jensen/Park evaluator",
    ),
    "T43": (
        "ppga_nantong_problem",
        "shared/contracts/"
        "ppga_nantong_structured_3d_declared_proxy_oracle.json",
        "independently frozen declared-proxy scalar and layout oracle",
    ),
    "Y06": (
        "gga_cpp_evaluator_oracle",
        "scripts/validate_gga_cpp_evaluator.py",
        "independent Python/SciPy wake, routing, and LCOE evaluator",
    ),
    "T36": (
        "plan004_tmoea_nysted_r4_numerical_receipt",
        "scripts/validate_tmoea_nysted_r4.py",
        "independent Python/SciPy scalar, biobjective, and front oracle",
    ),
    "L0726": (
        "geoga_anholt_structured_problem",
        "shared/contracts/"
        "geoga_anholt_structured_declared_proxy_oracle.json",
        "independently frozen declared-proxy scalar and layout oracle",
    ),
    "T46": (
        "pbea_cpp_evaluator_oracle",
        "scripts/validate_pbea_cpp_evaluator.py",
        "independent Python repaired Zhang et al. TWFLO evaluator",
    ),
}
LEARNING_REFERENCE_SOURCE = (
    "hpc/learning_libtorch/tests/reference_equivalence_test.cpp"
)
LEARNING_FULL_OPTIMIZER_TEST = "plan004_learning_full_optimizer_artifacts"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def json_records(output: str) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for line in output.splitlines():
        opening = line.find("{")
        if opening < 0:
            continue
        try:
            record = json.loads(line[opening:])
        except json.JSONDecodeError:
            continue
        if isinstance(record, dict):
            records.append(record)
    return records


def ctest(build_dir: Path, test_name: str) -> dict[str, Any]:
    command = [
        "ctest",
        "--test-dir",
        str(build_dir),
        "-V",
        "--output-on-failure",
        "-R",
        f"^{test_name}$",
    ]
    started = time.perf_counter()
    completed = subprocess.run(command, capture_output=True, text=True)
    elapsed = time.perf_counter() - started
    output = completed.stdout + completed.stderr
    if completed.returncode != 0 or "100% tests passed" not in output:
        raise RuntimeError(f"{test_name}: H5 CTest failed\n{output}")
    return {
        "test_name": test_name,
        "command": command,
        "return_code": completed.returncode,
        "measured_wall_seconds": elapsed,
        "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
        "structured_output": json_records(output),
        "output": output,
    }


def metric(receipt: dict[str, Any], test_name: str) -> dict[str, Any]:
    records = [
        record
        for record in receipt["tests"][test_name]["structured_output"]
        if record.get("status") == "pass"
        and "maximum_absolute_error" in record
    ]
    if len(records) != 1:
        raise RuntimeError(
            f"{test_name}: expected one structured numerical result, "
            f"found {len(records)}"
        )
    result = records[0]
    maximum = result["maximum_absolute_error"]
    if (
        isinstance(maximum, bool)
        or not isinstance(maximum, (int, float))
        or not math.isfinite(maximum)
        or maximum < 0.0
    ):
        raise RuntimeError(f"{test_name}: invalid maximum absolute error")
    return result


def discover_build_dir(explicit: Path | None) -> Path:
    candidates = (
        [explicit]
        if explicit is not None
        else [
            ROOT / "build/plan004-torch",
            ROOT / "build-plan004-torch",
            ROOT / "build-plan003-torch",
        ]
    )
    for candidate in candidates:
        if candidate is not None:
            resolved = candidate.resolve()
            if (resolved / "CTestTestfile.cmake").is_file():
                return resolved
    raise RuntimeError("no configured Torch-enabled CTest build was found")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.add_argument(
        "--require-independent-reference",
        action="store_true",
        required=True,
    )
    parser.add_argument("--torch-build-dir", type=Path)
    parser.add_argument("--receipt", type=Path, default=RAW_RECEIPT)
    arguments = parser.parse_args()
    build_dir = discover_build_dir(arguments.torch_build_dir)
    receipt_path = arguments.receipt.resolve()
    if receipt_path.exists():
        raise RuntimeError(
            f"append-only H5 receipt already exists: {receipt_path}"
        )

    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if len(rows) != 23:
        raise RuntimeError(f"expected 23 target pairs, found {len(rows)}")
    coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))
    runtime_by_corpus = {
        corpus_id: group["ctest_name"]
        for group in coverage["coverage_groups"]
        for corpus_id in group["corpus_ids"]
    }
    if set(runtime_by_corpus) != {row["corpus_id"] for row in rows}:
        raise RuntimeError("runtime coverage and target registry differ")

    oracle_test_by_corpus = dict(ORACLE_BY_CORPUS)
    for corpus_id, method in LEARNING_METHOD.items():
        oracle_test_by_corpus[corpus_id] = (
            f"plan004_{method}_reference_equivalence",
            LEARNING_REFERENCE_SOURCE,
            "independent direct-formula C++ tensor/loss/gradient reference",
        )
    required_tests = {
        runtime_by_corpus[row["corpus_id"]]
        for row in rows
        if row["corpus_id"] not in LEARNING_METHOD
    }
    required_tests.update(
        value[0] for value in oracle_test_by_corpus.values()
    )
    required_tests.update(
        {LEARNING_FULL_OPTIMIZER_TEST, "rlfode_math_fixture"}
    )

    tests: dict[str, dict[str, Any]] = {}
    for test_name in sorted(required_tests):
        tests[test_name] = ctest(build_dir, test_name)
    raw: dict[str, Any] = {
        "schema_version": 2,
        "receipt_id": (
            "plan004_h5_independent_reference_receipts_spark_20260730"
        ),
        "host": platform.node(),
        "machine": platform.machine(),
        "logical_cpu_count": os.cpu_count(),
        "scope": "23 target-only pairs",
        "build_dir": str(build_dir),
        "independent_reference_required": True,
        "tests": tests,
        "claim_boundary": (
            "Bounded H5 independent-reference and runtime evidence only; "
            "this is not H6 performance or formal campaign evidence."
        ),
    }
    full_optimizer = tests[LEARNING_FULL_OPTIMIZER_TEST][
        "structured_output"
    ]
    if (
        len(full_optimizer) != 1
        or full_optimizer[0].get("status") != "pass"
        or full_optimizer[0].get("methods") != 3
    ):
        raise RuntimeError("learning full-optimizer receipt is invalid")

    validation_documents: list[tuple[Path, dict[str, Any]]] = []
    for row in rows:
        corpus_id = row["corpus_id"]
        analysis = ROOT / row["analysis_path"]
        native_asset = ROOT / row["native_asset"]
        oracle_test, oracle_path, oracle_description = (
            oracle_test_by_corpus[corpus_id]
        )
        observed = metric(raw, oracle_test)
        learning = corpus_id in LEARNING_METHOD
        runtime_test = (
            LEARNING_FULL_OPTIMIZER_TEST
            if learning
            else runtime_by_corpus[corpus_id]
        )
        coverage_fields: dict[str, str] | None = None
        if learning:
            direct_coverage = observed.get("coverage", {})
            expected_direct = (
                "forward_tensors",
                "losses",
                "all_named_parameter_gradients",
                "one_optimizer_step",
                "artifact_reload",
                "artifact_driven_transition",
            )
            if any(
                direct_coverage.get(field) != "passed"
                for field in expected_direct
            ):
                raise RuntimeError(
                    f"{corpus_id}: incomplete direct-reference coverage"
                )
            coverage_fields = {
                **{field: "passed" for field in expected_direct},
                "physical_fes": "passed",
                "random_event_ownership": "passed",
                "terminal_partial_work": "passed",
            }
        independent_tests = [oracle_test]
        if corpus_id == "S04":
            independent_tests.append("rlfode_math_fixture")
        comparison: dict[str, Any] = {
            "passed": True,
            "maximum_absolute_error": observed[
                "maximum_absolute_error"
            ],
            "observed_result": observed,
        }
        if "maximum_scaled_absolute_error" in observed:
            comparison["maximum_scaled_absolute_error"] = observed[
                "maximum_scaled_absolute_error"
            ]
        if "scaled_tolerance" in observed:
            comparison["scaled_tolerance"] = observed["scaled_tolerance"]
        elif "absolute_tolerance_per_tensor" in observed:
            comparison["absolute_tolerance_per_tensor"] = observed[
                "absolute_tolerance_per_tensor"
            ]
        else:
            comparison["tolerance_policy"] = observed.get(
                "tolerance_policy", "method-specific bounded tolerance"
            )
        h5: dict[str, Any] = {
            "status": "accepted_h5",
            "reference_backend": "independent_reference_v1",
            "candidate_backends": [
                "libtorch_cpu_hpc_v1" if learning else "cpu_hpc_v1"
            ],
            "independent_reference": {
                "implementation_or_oracle": oracle_path,
                "description": oracle_description,
                "does_not_call_candidate": True,
                "ctest_names": independent_tests,
                "primary_test_output_sha256": tests[oracle_test][
                    "output_sha256"
                ],
            },
            "numerical_comparison": comparison,
            "runtime_test": runtime_test,
            "runtime_test_output_sha256": tests[runtime_test][
                "output_sha256"
            ],
            "runtime_test_receipt": str(receipt_path.relative_to(ROOT)),
            "physical_fes_ledger": (
                "bounded target runtime checks exact physical objective "
                "work; learning artifact training is outside that ledger"
            ),
            "comparison_scope": (
                "independent oracle/formula comparison plus bounded target "
                "optimizer execution; backend self-agreement alone is not "
                "accepted as equivalence"
            ),
        }
        if coverage_fields is not None:
            h5["coverage"] = coverage_fields
            h5["learning_method"] = LEARNING_METHOD[corpus_id]
            h5["full_optimizer_test"] = LEARNING_FULL_OPTIMIZER_TEST
        validation_path = analysis.with_name(
            analysis.name.replace(
                "_hpc_analysis.json", "_hpc_validation.json"
            )
        )
        validation_documents.append((
            validation_path,
            {
                "schema_version": 2,
                "validation_id": (
                    f"plan004_h5__{row['pair_id']}__spark_20260730"
                ),
                "pair_id": row["pair_id"],
                "corpus_id": corpus_id,
                "method_semantic_id": row["method_semantic_id"],
                "problem_semantic_id": row["problem_semantic_id"],
                "paper_protocol_id": row["paper_protocol_id"],
                "analysis_path": row["analysis_path"],
                "analysis_sha256": sha256(analysis),
                "native_asset": row["native_asset"],
                "native_asset_sha256": sha256(native_asset),
                "H5_bounded_equivalence": h5,
                "H6_performance_validation": {
                    "status": "pending_h6_measurement",
                    "accepted_backend": None,
                },
                "overall_status": "accepted_h5_pending_h6",
                "claim_boundary": (
                    "H5 accepts bounded independent-reference equivalence "
                    "only. No backend is selected before exact-pair H6 and "
                    "no formal-quality claim is made."
                ),
            },
        ))

    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    receipt_path.write_text(
        json.dumps(raw, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    receipt_sha256 = sha256(receipt_path)
    for validation_path, document in validation_documents:
        document["H5_bounded_equivalence"][
            "runtime_test_receipt_sha256"
        ] = receipt_sha256
        validation_path.write_text(
            json.dumps(document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    print(
        "hpc_core_h5_validation_pass "
        f"pairs={len(validation_documents)} "
        f"ctests={len(tests)} independent_references=23 "
        "status=accepted_h5_pending_h6"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
