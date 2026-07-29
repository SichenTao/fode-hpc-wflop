#!/usr/bin/env python3
"""Positive and tamper-negative tests for the Plan-005 H6 receipt audit."""

from __future__ import annotations

import copy
import csv
import hashlib
import json
import statistics
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from audit_plan005_h6_receipts import ROOT, WORKERS, audit


REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
LEARNING = {"Y36", "T42", "T45"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_jsonl(path: Path, records: list[dict[str, Any]]) -> None:
    path.write_text(
        "".join(
            json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
            for record in records
        ),
        encoding="utf-8",
    )


def rows() -> list[dict[str, str]]:
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def build_fixture(
    raw_path: Path,
    summary_path: Path,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    source_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    binary_paths = {
        "trainer": (
            ROOT
            / "build/plan005-torch/hpc/learning_libtorch/"
            "plan004_learning_target_hpc"
        ),
        "wflop": ROOT / "build/plan005-torch/hpc/wflop_cpp/wflop_cpp_hpc",
        "taae": ROOT / "build/plan005-torch/hpc/taae_cpp/taae_evolution_hpc",
    }
    original_h5 = (
        ROOT
        / "evidence/development/"
        "plan004_h5_independent_reference_receipts_20260730.json"
    )
    visible_cpus = list(range(20))
    environment = {
        "architecture": "aarch64",
        "affinity_visible_cpus": visible_cpus,
        "sha256": "e" * 64,
    }
    header = {
        "record_type": "campaign_header",
        "schema_version": 1,
        "source_commit": source_commit,
        "scope": "exact 23 target-only pairs",
        "workers": WORKERS,
        "repetitions": 5,
        "measurement_order_policy": "balanced_rotation",
        "backend_parallelism": 1,
        "environment": environment,
        "binaries": {
            name: {
                "logical_path": str(path.relative_to(ROOT)),
                "sha256": sha256(path),
            }
            for name, path in binary_paths.items()
        },
        "learning_artifacts": {
            method: {
                "artifact_sha256": str(index) * 64,
                "training_result": {
                    "thread_topology": {
                        "torch_intraop_threads": 1,
                        "torch_interop_threads": 1,
                    }
                },
            }
            for index, method in enumerate(("taae", "alga", "rlpso"), 1)
        },
        "post_thread_control_h5_revalidation": {
            "logical_path": str(original_h5.relative_to(ROOT)),
            "sha256": sha256(original_h5),
        },
        "learning_thread_topology_contract": {
            "outer_persistent_workers": "W",
            "torch_intraop_threads": "W",
            "torch_interop_threads": 1,
            "affinity_allocated_cpus": "exactly W",
            "phase_separation": (
                "fixture: pools never perform CPU work concurrently"
            ),
        },
    }
    observations = []
    targets = []
    for pair_index, row in enumerate(rows()):
        per_worker: dict[int, list[float]] = {}
        for repetition in range(5):
            order = WORKERS[repetition:] + WORKERS[:repetition]
            for order_index, workers in enumerate(order):
                duration = (
                    100.0 * (0.20 + 0.80 / workers)
                    + repetition * 0.01
                )
                per_worker.setdefault(workers, []).append(duration)
                raw = {
                    "physical_fes": 1000,
                    "resolved_workers": workers,
                }
                command = ["fixture", "--workers", str(workers)]
                if row["corpus_id"] in LEARNING:
                    raw["thread_topology"] = {
                        "outer_workers": workers,
                        "torch_intraop_threads": workers,
                        "torch_interop_threads": 1,
                    }
                    command.extend([
                        "--torch-intraop-threads", str(workers),
                        "--torch-interop-threads", "1",
                    ])
                observations.append({
                    "record_type": "observation",
                    "schema_version": 1,
                    "key": {
                        "pair_id": row["pair_id"],
                        "native_asset": row["native_asset"],
                        "case_id": "fixture_case",
                        "physical_fes": 1000,
                        "source_commit": source_commit,
                        "binary_sha256": next(
                            iter(header["binaries"].values())
                        )["sha256"],
                        "architecture": "aarch64",
                        "environment_sha256": "e" * 64,
                        "workers": workers,
                        "repetition": repetition,
                        "order_index": order_index,
                        "seed": 1000 + pair_index * 100 + repetition,
                    },
                    "command": command,
                    "process": {
                        "affinity_cpu_union": visible_cpus[:workers],
                        "external_wall_seconds": duration + 0.1,
                        "user_cpu_seconds": duration,
                        "system_cpu_seconds": 0.01,
                        "cpu_time_to_wall": 1.0,
                        "maximum_resident_set_kib": 1024,
                        "voluntary_context_switches": 1,
                        "involuntary_context_switches": 1,
                        "peak_os_threads": workers,
                        "cpu_migrations": 0,
                    },
                    "active_workers": {
                        "requested_outer_workers": workers,
                        "observed_outer_workers": workers,
                    },
                    "timing": {
                        "algorithm_end_to_end_seconds": duration,
                        "throughput_fes_per_second": 1000 / duration,
                        "named_h0_stages_seconds": {
                            "fixture_stage": duration,
                        },
                        "named_h0_stage_attribution": 1.0,
                    },
                    "scientific_output_sha256": (
                        f"{pair_index:02x}{repetition:02x}".ljust(64, "a")
                    ),
                    "raw_result": raw,
                })
        medians = {
            worker: statistics.median(values)
            for worker, values in per_worker.items()
        }
        fastest = min(WORKERS, key=lambda worker: medians[worker])
        targets.append({
            "pair_id": row["pair_id"],
            "status": "accepted_h6",
            "backend": "cpu_hpc_v1",
            "observation_count": 35,
            "selected_workers": fastest,
            "all_visible_workers": 20,
            "fastest_measured_workers": fastest,
            "worker_statistics": {
                str(worker): {
                    "repetitions": 5,
                    "median_seconds": medians[worker],
                    "median_absolute_deviation_seconds": statistics.median(
                        [
                            abs(value - medians[worker])
                            for value in per_worker[worker]
                        ]
                    ),
                    "speedup": medians[1] / medians[worker],
                    "parallel_efficiency": (
                        medians[1] / medians[worker] / worker
                    ),
                }
                for worker in WORKERS
            },
        })
    records = [header, *observations]
    write_jsonl(raw_path, records)
    summary = {
        "schema_version": 1,
        "status": "accepted_h6",
        "target_count": 23,
        "observation_count": 805,
        "workers": WORKERS,
        "repetitions": 5,
        "raw_observations_sha256": sha256(raw_path),
        "minimum_stage_attribution": 1.0,
        "non_target_baselines_in_readiness": 0,
        "targets": targets,
    }
    summary_path.write_text(
        json.dumps(summary, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return records, summary


def expect_failure(
    records: list[dict[str, Any]],
    summary: dict[str, Any],
    directory: Path,
    name: str,
    expected: str,
) -> None:
    raw = directory / f"{name}.jsonl"
    report = directory / f"{name}-summary.json"
    write_jsonl(raw, records)
    changed_summary = copy.deepcopy(summary)
    changed_summary["raw_observations_sha256"] = sha256(raw)
    report.write_text(
        json.dumps(changed_summary, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    try:
        audit(
            raw_path=raw,
            summary_path=report,
            verify_repository_state=False,
        )
    except RuntimeError as error:
        if expected not in str(error):
            raise RuntimeError(
                f"{name}: wrong failure: {error}"
            ) from error
        return
    raise RuntimeError(f"{name}: tampered receipt was accepted")


def main() -> int:
    with tempfile.TemporaryDirectory(
        prefix="plan005-h6-audit-fixture-"
    ) as temporary:
        directory = Path(temporary)
        raw = directory / "positive.jsonl"
        summary_path = directory / "positive-summary.json"
        records, summary = build_fixture(raw, summary_path)
        result = audit(
            raw_path=raw,
            summary_path=summary_path,
            verify_repository_state=False,
        )
        if result["observations"] != 805:
            raise RuntimeError("positive fixture cardinality drift")

        affinity = copy.deepcopy(records)
        affinity[1]["process"]["affinity_cpu_union"] = [19]
        expect_failure(
            affinity, summary, directory, "bad-affinity", "affinity escaped"
        )

        attribution = copy.deepcopy(records)
        attribution[1]["timing"]["named_h0_stages_seconds"] = {
            "fixture_stage": 50.0
        }
        attribution[1]["timing"]["named_h0_stage_attribution"] = 0.5
        expect_failure(
            attribution,
            summary,
            directory,
            "bad-attribution",
            "stage attribution invalid",
        )

        order = copy.deepcopy(records)
        order[1]["key"]["order_index"] = 1
        order[2]["key"]["order_index"] = 0
        expect_failure(
            order, summary, directory, "bad-order", "not balanced rotation"
        )
    print(
        "plan005_h6_receipt_audit_fixture_pass "
        "positive=1 rejected_affinity=1 rejected_attribution=1 "
        "rejected_order=1 observations=805"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
