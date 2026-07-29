#!/usr/bin/env python3
"""Independently audit Plan-005 exact-target H6 scaling receipts."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
import random
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
RAW = ROOT / "evidence/performance/plan005_h6_raw_observations_20260730.jsonl"
SUMMARY = ROOT / "evidence/performance/plan005_h6_summary_20260730.json"
WORKERS = [1, 2, 4, 8, 12, 16, 20]
LEARNING = {"Y36", "T42", "T45"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def finite_nonnegative(value: Any) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, (int, float))
        and math.isfinite(value)
        and value >= 0.0
    )


def read_rows() -> list[dict[str, str]]:
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    require(len(rows) == 23, f"expected 23 target rows, found {len(rows)}")
    require(
        all(row["role"] == "target" for row in rows),
        "non-target baseline entered H6 readiness",
    )
    return rows


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    require(path.is_file(), f"missing raw receipt: {path}")
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]


def observed_fes(raw: dict[str, Any]) -> int | None:
    return raw.get("physical_fes", raw.get("complete_layout_evaluations"))


def validation_path(row: dict[str, str]) -> Path:
    analysis = ROOT / row["analysis_path"]
    return analysis.with_name(
        analysis.name.replace("_hpc_analysis.json", "_hpc_validation.json")
    )


def canonical_sha256(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def paired_speed_ratio_ci(
    observations: list[dict[str, Any]],
    fastest_workers: int,
    compared_workers: int,
    seed: int,
) -> tuple[float, list[float]]:
    by_repetition: dict[int, dict[int, float]] = defaultdict(dict)
    for item in observations:
        by_repetition[item["key"]["repetition"]][
            item["key"]["workers"]
        ] = item["timing"]["algorithm_end_to_end_seconds"]
    ratios = [
        values[fastest_workers] / values[compared_workers]
        for _, values in sorted(by_repetition.items())
    ]
    generator = random.Random(seed)
    bootstrapped = []
    for _ in range(10000):
        bootstrapped.append(statistics.median(
            [generator.choice(ratios) for _ in ratios]
        ))
    bootstrapped.sort()
    return statistics.median(ratios), [
        bootstrapped[249],
        bootstrapped[9749],
    ]


def audit(
    *,
    raw_path: Path,
    summary_path: Path,
    verify_repository_state: bool = True,
) -> dict[str, Any]:
    rows = read_rows()
    row_by_pair = {row["pair_id"]: row for row in rows}
    records = read_jsonl(raw_path)
    require(len(records) == 806, f"expected header plus 805 observations")
    header = records[0]
    observations = records[1:]
    require(
        header.get("record_type") == "campaign_header"
        and header.get("schema_version") == 1,
        "H6 campaign header schema drift",
    )
    require(
        header.get("scope") == "exact 23 target-only pairs"
        and header.get("workers") == WORKERS
        and header.get("repetitions") == 5
        and header.get("measurement_order_policy") == "balanced_rotation"
        and header.get("backend_parallelism") == 1,
        "H6 frozen execution policy drift",
    )
    require(
        set(header["learning_artifacts"]) == {"taae", "alga", "rlpso"},
        "learning artifact coverage drift",
    )
    h5_revalidation = header["post_thread_control_h5_revalidation"]
    require(
        sha256(ROOT / h5_revalidation["logical_path"])
        == h5_revalidation["sha256"],
        "post-thread-control H5 revalidation changed",
    )
    for method, receipt in header["learning_artifacts"].items():
        require(
            len(receipt["artifact_sha256"]) == 64
            and receipt["training_result"]["thread_topology"] == {
                "torch_intraop_threads": 1,
                "torch_interop_threads": 1,
            },
            f"{method}: artifact training topology or hash invalid",
        )
    topology = header["learning_thread_topology_contract"]
    require(
        topology["outer_persistent_workers"] == "W"
        and topology["torch_intraop_threads"] == "W"
        and topology["torch_interop_threads"] == 1
        and topology["affinity_allocated_cpus"] == "exactly W"
        and "never perform CPU work concurrently" in topology["phase_separation"],
        "learning phase-separation topology contract drift",
    )
    commit_check = subprocess.run(
        ["git", "cat-file", "-e", f"{header['source_commit']}^{{commit}}"],
        cwd=ROOT,
        capture_output=True,
    )
    require(commit_check.returncode == 0, "H6 source commit is unavailable")
    for name, receipt in header["binaries"].items():
        path = ROOT / receipt["logical_path"]
        require(
            path.is_file() and sha256(path) == receipt["sha256"],
            f"{name}: measured binary is absent or changed",
        )

    by_pair: dict[str, list[dict[str, Any]]] = defaultdict(list)
    unique_keys: set[str] = set()
    minimum_attribution = 1.0
    for observation in observations:
        require(
            observation.get("record_type") == "observation"
            and observation.get("schema_version") == 1,
            "observation schema drift",
        )
        key = observation["key"]
        pair_id = key["pair_id"]
        require(pair_id in row_by_pair, f"unknown pair receipt: {pair_id}")
        encoded_key = json.dumps(key, sort_keys=True, separators=(",", ":"))
        require(encoded_key not in unique_keys, f"duplicate H6 key: {pair_id}")
        unique_keys.add(encoded_key)
        row = row_by_pair[pair_id]
        require(
            key["native_asset"] == row["native_asset"]
            and key["source_commit"] == header["source_commit"]
            and key["architecture"] == header["environment"]["architecture"]
            and key["environment_sha256"]
            == header["environment"]["sha256"],
            f"{pair_id}: identity or environment drift",
        )
        require(
            key["workers"] in WORKERS
            and key["repetition"] in range(5)
            and key["order_index"] in range(7),
            f"{pair_id}: worker/repetition/order key invalid",
        )
        binary_hashes = {
            value["sha256"] for value in header["binaries"].values()
        }
        require(
            key["binary_sha256"] in binary_hashes,
            f"{pair_id}: binary hash is outside campaign header",
        )
        raw = observation["raw_result"]
        require(
            observed_fes(raw) == key["physical_fes"],
            f"{pair_id}: physical-FES mismatch",
        )
        process = observation["process"]
        require(
            process["affinity_cpu_union"]
            == header["environment"]["affinity_visible_cpus"][:key["workers"]],
            f"{pair_id}: affinity escaped W CPUs",
        )
        for field in (
            "external_wall_seconds",
            "user_cpu_seconds",
            "system_cpu_seconds",
            "cpu_time_to_wall",
            "maximum_resident_set_kib",
            "voluntary_context_switches",
            "involuntary_context_switches",
            "peak_os_threads",
            "cpu_migrations",
        ):
            require(
                finite_nonnegative(process[field]),
                f"{pair_id}: invalid process metric {field}",
            )
        require(
            process["maximum_resident_set_kib"] > 0
            and process["peak_os_threads"] > 0,
            f"{pair_id}: memory/thread sampling absent",
        )
        active = observation["active_workers"]
        require(
            active["requested_outer_workers"] == key["workers"]
            and active["observed_outer_workers"] == key["workers"],
            f"{pair_id}: active outer worker mismatch",
        )
        if active.get("parallel_regions", 0) > 0:
            utilization = active.get("participant_activation_utilization")
            require(
                active.get("participant_activations", 0) > 0
                and 0 < active.get("distinct_participants", 0)
                <= key["workers"]
                and 0 < active.get("peak_region_participants", 0)
                <= key["workers"]
                and finite_nonnegative(utilization)
                and 0.0 < utilization <= 1.0 + 1.0e-12,
                f"{pair_id}: parallel region lacks real active workers",
            )
        timing = observation["timing"]
        require(
            finite_nonnegative(timing["algorithm_end_to_end_seconds"])
            and timing["algorithm_end_to_end_seconds"] > 0.0
            and finite_nonnegative(timing["throughput_fes_per_second"])
            and timing["throughput_fes_per_second"] > 0.0,
            f"{pair_id}: end-to-end or throughput invalid",
        )
        stage_sum = sum(timing["named_h0_stages_seconds"].values())
        require(
            all(
                finite_nonnegative(value)
                for value in timing["named_h0_stages_seconds"].values()
            )
            and math.isclose(
                timing["named_h0_stage_attribution"],
                stage_sum / timing["algorithm_end_to_end_seconds"],
                rel_tol=1.0e-12,
                abs_tol=1.0e-12,
            )
            and timing["named_h0_stage_attribution"] >= 0.95,
            f"{pair_id}: named H0 stage attribution invalid",
        )
        overlap = max(
            0.0,
            stage_sum - timing["algorithm_end_to_end_seconds"],
        )
        require(
            math.isclose(
                timing.get("stage_overlap_seconds", overlap),
                overlap,
                rel_tol=1.0e-12,
                abs_tol=1.0e-12,
            )
            and overlap
            <= max(
                1.0e-6,
                0.01 * timing["algorithm_end_to_end_seconds"],
            ),
            f"{pair_id}: named stage timers overlap excessively",
        )
        minimum_attribution = min(
            minimum_attribution,
            timing["named_h0_stage_attribution"],
        )
        if row["corpus_id"] in LEARNING:
            require(
                raw.get("thread_topology") == {
                    "outer_workers": key["workers"],
                    "torch_intraop_threads": key["workers"],
                    "torch_interop_threads": 1,
                },
                f"{pair_id}: learned runtime topology mismatch",
            )
            command = observation["command"]
            require(
                command[command.index("--torch-intraop-threads") + 1]
                == str(key["workers"])
                and command[command.index("--torch-interop-threads") + 1]
                == "1",
                f"{pair_id}: learned command topology mismatch",
            )
        by_pair[pair_id].append(observation)

    require(set(by_pair) == set(row_by_pair), "pair coverage differs")
    for pair_id, items in by_pair.items():
        require(len(items) == 35, f"{pair_id}: expected 35 observations")
        science_by_repetition: dict[int, set[str]] = defaultdict(set)
        for repetition in range(5):
            repeated = [
                item for item in items
                if item["key"]["repetition"] == repetition
            ]
            expected_order = (
                WORKERS[repetition:] + WORKERS[:repetition]
            )
            actual_order = [
                item["key"]["workers"]
                for item in sorted(
                    repeated,
                    key=lambda value: value["key"]["order_index"],
                )
            ]
            require(
                actual_order == expected_order,
                f"{pair_id}: repetition {repetition} is not balanced rotation",
            )
            for item in repeated:
                science_by_repetition[repetition].add(
                    item["scientific_output_sha256"]
                )
        require(
            all(len(values) == 1 for values in science_by_repetition.values()),
            f"{pair_id}: science changed with worker count",
        )

    require(summary_path.is_file(), "H6 summary absent")
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    require(
        summary["schema_version"] == 1
        and summary["status"] == "accepted_h6"
        and summary["target_count"] == 23
        and summary["observation_count"] == 805
        and summary["workers"] == WORKERS
        and summary["repetitions"] == 5
        and summary["raw_observations_sha256"] == sha256(raw_path)
        and summary["minimum_stage_attribution"] == minimum_attribution
        and summary["non_target_baselines_in_readiness"] == 0,
        "H6 summary identity or cardinality drift",
    )
    summary_by_pair = {item["pair_id"]: item for item in summary["targets"]}
    require(set(summary_by_pair) == set(by_pair), "summary target coverage")
    for pair_id, items in by_pair.items():
        target = summary_by_pair[pair_id]
        require(
            target["status"] == "accepted_h6"
            and target["backend"] == "cpu_hpc_v1"
            and target["observation_count"] == 35
            and target["selected_workers"] in WORKERS
            and target["all_visible_workers"] == 20,
            f"{pair_id}: H6 selection receipt invalid",
        )
        medians: dict[int, float] = {}
        for worker in WORKERS:
            values = [
                item["timing"]["algorithm_end_to_end_seconds"]
                for item in items
                if item["key"]["workers"] == worker
            ]
            require(len(values) == 5, f"{pair_id}/{worker}: repetitions")
            median = statistics.median(values)
            medians[worker] = median
            observed = target["worker_statistics"][str(worker)]
            require(
                observed["repetitions"] == 5
                and observed["median_seconds"] == median
                and observed["median_absolute_deviation_seconds"]
                == statistics.median(
                    [abs(value - median) for value in values]
                ),
                f"{pair_id}/{worker}: derived statistics drift",
            )
        for worker in WORKERS:
            speedup = medians[1] / medians[worker]
            observed = target["worker_statistics"][str(worker)]
            require(
                observed["speedup"] == speedup
                and observed["parallel_efficiency"] == speedup / worker,
                f"{pair_id}/{worker}: speedup/efficiency drift",
            )
        require(
            target["fastest_measured_workers"]
            == min(WORKERS, key=lambda value: medians[value]),
            f"{pair_id}: fastest topology drift",
        )
        fastest = target["fastest_measured_workers"]
        paired_ratio, paired_ci = paired_speed_ratio_ci(
            items,
            fastest,
            20,
            int(hashlib.sha256(pair_id.encode()).hexdigest()[:8], 16),
        )
        tied = fastest == 20 or paired_ci[0] >= 0.95
        require(
            target["all_visible_relative_to_fastest_paired_speed_ratio"]
            == paired_ratio
            and target[
                "all_visible_relative_to_fastest_paired_bootstrap_95_ci"
            ] == paired_ci
            and target["all_visible_tie_lower_ratio_threshold"] == 0.95
            and target["all_visible_statistically_tied_with_fastest"]
            is tied
            and target["selected_workers"] == (20 if tied else fastest),
            f"{pair_id}: paired all-visible selection drift",
        )
        row = row_by_pair[pair_id]
        analysis_path = ROOT / row["analysis_path"]
        analysis_document = json.loads(
            analysis_path.read_text(encoding="utf-8")
        )
        h2 = analysis_document["H2_dependency_and_parallel_width"]
        h3 = analysis_document["H3_performance_and_granularity"]
        proof = target["dependency_proof"]
        require(
            proof["analysis_path"] == row["analysis_path"]
            and proof["analysis_sha256"] == sha256(analysis_path)
            and proof["h2_dependency_edges"] == h2["dependency_edges"]
            and proof["h2_dependency_edges_sha256"]
            == canonical_sha256(h2["dependency_edges"])
            and proof["h2_ordered_sections"] == h2["ordered_sections"]
            and proof["h3_granularity_rule"] == h3["granularity_rule"]
            and proof["h3_dispatch_crossover_source"]
            == h3["dispatch_crossover_source"],
            f"{pair_id}: pair-specific dependency/granularity proof drift",
        )
        if target["serial_limited"]:
            require(
                fastest == 1
                and target["worker_statistics"]["20"]["speedup"] <= 1.10
                and bool(proof["h2_dependency_edges"])
                and bool(proof["h2_ordered_sections"])
                and proof["measured_crossover"]["fastest_workers"] == 1
                and proof["measured_crossover"]["all_visible_workers"] == 20
                and proof["measured_crossover"]["all_visible_speedup"]
                == target["worker_statistics"]["20"]["speedup"],
                f"{pair_id}: serial-limited proof is incomplete",
            )

    if verify_repository_state:
        summary_hash = sha256(summary_path)
        for row in rows:
            require(
                row["validation_status"] == "accepted_h5_h6",
                f"{row['pair_id']}: registry H6 state not accepted",
            )
            document = json.loads(
                validation_path(row).read_text(encoding="utf-8")
            )
            h6 = document["H6_performance_validation"]
            target = summary_by_pair[row["pair_id"]]
            require(
                document["H5_bounded_equivalence"]["status"] == "accepted_h5"
                and document["overall_status"] == "accepted_h5_h6"
                and h6["status"] == "accepted_h6"
                and h6["accepted_backend"] == target["backend"]
                and h6["selected_workers"] == target["selected_workers"]
                and h6["all_visible_characterized"] is True
                and h6["performance_receipt_sha256"] == summary_hash,
                f"{row['pair_id']}: H5/H6 validation chain drift",
            )
    return {
        "pairs": len(by_pair),
        "observations": len(observations),
        "minimum_stage_attribution": minimum_attribution,
        "learning_pairs": len(LEARNING),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.add_argument("--strict", action="store_true", required=True)
    parser.add_argument("--raw", type=Path, default=RAW)
    parser.add_argument("--summary", type=Path, default=SUMMARY)
    arguments = parser.parse_args()
    result = audit(
        raw_path=arguments.raw.resolve(),
        summary_path=arguments.summary.resolve(),
    )
    print(
        "plan005_h6_receipt_audit_pass "
        f"pairs={result['pairs']} observations={result['observations']} "
        f"learning_pairs={result['learning_pairs']} "
        "all_visible_characterized=yes "
        f"minimum_attribution={result['minimum_stage_attribution']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
