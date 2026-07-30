#!/usr/bin/env python3
"""Audit the approved full-core-only Plan-005 production H6 campaign."""

from __future__ import annotations

import csv
import hashlib
import json
import math
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs" / "hpc_core_target_pairs.tsv"
RAW = (
    ROOT
    / "evidence" / "performance"
    / "plan005_h6_performance_first_raw_observations_20260730.jsonl"
)
SUMMARY = (
    ROOT
    / "evidence" / "performance"
    / "plan005_h6_performance_first_summary_20260730.json"
)
CALIBRATION = (
    ROOT
    / "evidence" / "development"
    / "plan005_learning_phase_thread_calibration_20260730.json"
)
EXPECTED_WORKERS = 20
EXPECTED_REPETITIONS = 5
EXPECTED_TARGETS = 23
LEARNING_TORCH_THREADS = {"Y36": 4, "T42": 4, "T45": 1}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def finite_nonnegative(value: Any) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(value)
        and value >= 0.0
    )


def main() -> int:
    require(RAW.is_file(), "production H6 raw receipt is absent")
    require(SUMMARY.is_file(), "production H6 summary is absent")
    require(CALIBRATION.is_file(), "learning phase-thread calibration absent")
    calibration = json.loads(CALIBRATION.read_text(encoding="utf-8"))
    require(
        calibration.get("status")
        == "accepted_phase_specific_cpu_thread_profiles"
        and {
            item["corpus_id"]: item["selected_torch_intraop_threads"]
            for item in calibration["methods"].values()
        }
        == LEARNING_TORCH_THREADS,
        "learning phase-thread calibration selection drift",
    )
    records = [
        json.loads(line)
        for line in RAW.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    require(records, "production H6 raw receipt is empty")
    header, observations = records[0], records[1:]
    require(
        header.get("record_type") == "campaign_header"
        and header.get("campaign_id")
        == "plan005_h6_full_core_production_spark_20260730"
        and header.get("workers") == [EXPECTED_WORKERS]
        and header.get("repetitions") == EXPECTED_REPETITIONS
        and header.get("measurement_order_policy")
        == "production_profile_only"
        and header.get("backend_parallelism") == 1,
        "production H6 header drift",
    )
    current_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    require(
        header["source_commit"] == current_commit,
        "production H6 source commit is not current",
    )
    environment = header["environment"]
    affinity = environment["worker_affinity_sets"][str(EXPECTED_WORKERS)]
    require(
        len(affinity) == EXPECTED_WORKERS
        and sorted(affinity) == environment["affinity_visible_cpus"],
        "production H6 does not use every affinity-visible CPU",
    )

    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    pair_ids = {row["pair_id"] for row in rows}
    require(
        len(rows) == EXPECTED_TARGETS
        and all(row["role"] == "target" for row in rows),
        "target registry scope drift",
    )
    require(
        len(observations)
        == EXPECTED_TARGETS * EXPECTED_REPETITIONS,
        "production H6 observation cardinality drift",
    )

    counts: Counter[str] = Counter()
    repetitions: dict[str, set[int]] = defaultdict(set)
    minimum_attribution = 1.0
    key_hashes: set[str] = set()
    for observation in observations:
        require(
            observation.get("record_type") == "observation",
            "non-observation record entered H6 body",
        )
        key = observation["key"]
        pair_id = key["pair_id"]
        require(pair_id in pair_ids, f"unknown H6 pair {pair_id}")
        require(
            key["workers"] == EXPECTED_WORKERS
            and key["affinity_cpus"] == affinity
            and key["source_commit"] == current_commit,
            f"{pair_id}: production worker/source identity drift",
        )
        key_hash = canonical_sha256(key)
        require(key_hash not in key_hashes, f"{pair_id}: duplicate H6 key")
        key_hashes.add(key_hash)
        require(
            observation["command_sha256"]
            == canonical_sha256(observation["command"]),
            f"{pair_id}: command hash drift",
        )
        raw = observation["raw_result"]
        observed_fes = raw.get(
            "physical_fes",
            raw.get("complete_layout_evaluations"),
        )
        require(
            observed_fes == key["physical_fes"] > 0,
            f"{pair_id}: physical FES drift",
        )
        process = observation["process"]
        torch_threads = LEARNING_TORCH_THREADS.get(
            next(
                row["corpus_id"]
                for row in rows
                if row["pair_id"] == pair_id
            ),
            1,
        )
        require(
            process["affinity_cpu_union"] == affinity
            and process["peak_os_threads"]
            <= 2 * EXPECTED_WORKERS + torch_threads + 4
            and finite_nonnegative(process["external_wall_seconds"]),
            f"{pair_id}: process resource receipt invalid",
        )
        active = observation["active_workers"]
        require(
            active["requested_outer_workers"] == EXPECTED_WORKERS
            and active["observed_outer_workers"] == EXPECTED_WORKERS,
            f"{pair_id}: outer-worker receipt invalid",
        )
        corpus_id = next(
            row["corpus_id"] for row in rows if row["pair_id"] == pair_id
        )
        if corpus_id in LEARNING_TORCH_THREADS:
            require(
                raw.get("thread_topology") == {
                    "outer_workers": EXPECTED_WORKERS,
                    "torch_intraop_threads": LEARNING_TORCH_THREADS[
                        corpus_id
                    ],
                    "torch_interop_threads": 1,
                },
                f"{pair_id}: phase-specific LibTorch topology drift",
            )
        timing = observation["timing"]
        attribution = timing["named_h0_stage_attribution"]
        require(
            finite_nonnegative(timing["algorithm_end_to_end_seconds"])
            and timing["algorithm_end_to_end_seconds"] > 0.0
            and 0.95 <= attribution <= 1.01,
            f"{pair_id}: H0 stage attribution invalid",
        )
        minimum_attribution = min(minimum_attribution, attribution)
        counts[pair_id] += 1
        repetitions[pair_id].add(key["repetition"])

    require(
        all(counts[pair_id] == EXPECTED_REPETITIONS for pair_id in pair_ids)
        and all(
            repetitions[pair_id] == set(range(EXPECTED_REPETITIONS))
            for pair_id in pair_ids
        ),
        "production H6 per-pair repetition coverage drift",
    )

    summary = json.loads(SUMMARY.read_text(encoding="utf-8"))
    require(
        summary.get("status") == "accepted_h6"
        and summary.get("summary_id")
        == "plan005_h6_full_core_production_summary_spark_20260730"
        and summary.get("source_commit") == current_commit
        and summary.get("raw_observations_sha256") == sha256(RAW)
        and summary.get("target_count") == EXPECTED_TARGETS
        and summary.get("observation_count")
        == EXPECTED_TARGETS * EXPECTED_REPETITIONS
        and summary.get("workers") == [EXPECTED_WORKERS]
        and summary.get("repetitions") == EXPECTED_REPETITIONS
        and len(summary.get("targets", [])) == EXPECTED_TARGETS,
        "production H6 summary drift",
    )
    for target in summary["targets"]:
        require(
            target["pair_id"] in pair_ids
            and target["selected_workers"] == EXPECTED_WORKERS
            and target["all_visible_workers"] == EXPECTED_WORKERS
            and target["observation_count"] == EXPECTED_REPETITIONS
            and target["status"] == "accepted_h6"
            and target["measured_serial_fraction"] is None
            and target["minimum_named_h0_stage_attribution"] >= 0.95,
            f"{target.get('pair_id')}: production H6 target summary invalid",
        )
    require(
        math.isclose(
            summary["minimum_stage_attribution"],
            minimum_attribution,
            rel_tol=1.0e-12,
            abs_tol=1.0e-12,
        ),
        "production H6 minimum attribution drift",
    )
    print(
        "plan005_production_h6_audit_pass "
        f"targets={EXPECTED_TARGETS} observations={len(observations)} "
        f"workers={EXPECTED_WORKERS} "
        f"minimum_attribution={minimum_attribution:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
