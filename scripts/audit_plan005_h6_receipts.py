#!/usr/bin/env python3
"""Independently audit Plan-005 exact-target H6 scaling receipts."""

from __future__ import annotations

import argparse
import copy
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
RAW = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_raw_observations_20260730.jsonl"
)
SUMMARY = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_summary_20260730.json"
)
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


def receipt_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else ROOT / path


def validate_performance_first_topology(
    header: dict[str, Any],
) -> tuple[dict[str, list[int]], dict[str, list[int]]]:
    require(
        header.get("campaign_id")
        == "plan005_h6_core_scaling_performance_first_spark_20260730"
        and header.get("topology_policy")
        == "architecture_aware_performance_first",
        "H6 campaign or topology policy drift",
    )
    environment = header["environment"]
    rows = environment["logical_cpu_topology"]
    visible = environment["affinity_visible_cpus"]
    require(
        len(rows) == len(visible)
        and sorted(row["cpu"] for row in rows) == visible
        and all(row["online"] for row in rows),
        "logical CPU topology does not match affinity-visible CPUs",
    )
    group_maximum = {
        model: max(
            row["maximum_mhz"]
            for row in rows
            if row["model_name"] == model
        )
        for model in {row["model_name"] for row in rows}
    }
    ordered_models = sorted(
        group_maximum,
        key=lambda model: (-group_maximum[model], model),
    )
    expected_order = [
        row["cpu"]
        for model in ordered_models
        for row in sorted(
            (item for item in rows if item["model_name"] == model),
            key=lambda item: (-item["maximum_mhz"], item["cpu"]),
        )
    ]
    selection = environment["worker_selection_order"]
    affinity_sets = environment["worker_affinity_sets"]
    require(
        environment["topology_policy"]
        == "architecture_aware_performance_first"
        and environment["performance_first_cpu_order"] == expected_order,
        "performance-first CPU order drift",
    )
    lookup = {row["cpu"]: row for row in rows}
    for worker in WORKERS:
        key = str(worker)
        expected_selection = expected_order[:worker]
        expected_set = sorted(expected_selection)
        counts: dict[str, int] = {}
        for cpu in expected_selection:
            model = lookup[cpu]["model_name"]
            counts[model] = counts.get(model, 0) + 1
        require(
            selection[key] == expected_selection
            and affinity_sets[key] == expected_set
            and environment["worker_core_type_composition"][key] == {
                "selection_order": expected_selection,
                "affinity_set": expected_set,
                "core_type_counts": counts,
            },
            f"W={worker}: frozen performance-first topology drift",
        )
    return selection, affinity_sets


def validate_environment_sidecar(
    summary: dict[str, Any],
    header: dict[str, Any],
    *,
    verify_repository_state: bool,
) -> None:
    receipt = summary["environment_sidecar"]
    path = receipt_path(receipt["logical_path"])
    require(
        path.is_file() and sha256(path) == receipt["sha256"],
        "H6 environment sidecar absent or changed",
    )
    document = json.loads(path.read_text(encoding="utf-8"))
    require(
        document["h6_campaign_id"] == header["campaign_id"]
        and document["h6_source_commit"] == header["source_commit"]
        and document["topology_policy"] == header["topology_policy"]
        and document["lscpu_raw_sha256"]
        == hashlib.sha256(document["lscpu_raw"].encode("utf-8")).hexdigest(),
        "H6 environment sidecar identity or lscpu hash drift",
    )
    environment = header["environment"]
    require(
        document["logical_cpu_rows"] == environment["logical_cpu_topology"]
        and document["performance_first_cpu_order"]
        == environment["performance_first_cpu_order"]
        and document["worker_selection_order"]
        == environment["worker_selection_order"]
        and document["worker_affinity_sets"]
        == environment["worker_affinity_sets"]
        and bool(document["cache_sysfs"])
        and bool(document["frequency_governor_sysfs"])
        and all(
            item["scaling_governor"] == "performance"
            for item in document["frequency_governor_sysfs"]
        )
        and bool(document["compiler"])
        and bool(document["cmake"])
        and bool(document["selected_cmake_cache_entries"]),
        "H6 environment sidecar topology, frequency, cache, or toolchain drift",
    )
    lookup = {
        row["cpu"]: row["model_name"]
        for row in document["logical_cpu_rows"]
    }
    for worker in WORKERS:
        key = str(worker)
        selection = document["worker_selection_order"][key]
        counts: dict[str, int] = {}
        for cpu in selection:
            model = lookup[cpu]
            counts[model] = counts.get(model, 0) + 1
        require(
            document["performance_first_worker_composition"][key] == {
                "selection_order": selection,
                "affinity_set": sorted(selection),
                "core_type_counts": counts,
            },
            f"W={worker}: sidecar topology composition drift",
        )
    require(
        "Heterogeneous" in document["measurement_noise_boundary"]
        and "concurrent" in document["measurement_noise_boundary"],
        "H6 environment sidecar noise boundary is incomplete",
    )
    if verify_repository_state:
        groups = document["core_type_groups"]
        require(
            len(groups.get("Cortex-X925", [])) == 10
            and len(groups.get("Cortex-A725", [])) == 10,
            "Spark X925/A725 topology count drift",
        )
        isaac = [
            item
            for item in document["pre_existing_gpu_compute_processes"]
            if item["task_identity"]
            == "pre-existing Isaac Lab GPU workload"
        ]
        require(
            any(
                item["observed_cpu_core_equivalent"] > 0.0
                and item["gpu_memory_mib"] >= 1000
                and item["affinity_cpus"] == list(range(20))
                and bool(item["workload_label"])
                and len(item["command_sha256"]) == 64
                for item in isaac
            ),
            "captured concurrent Isaac GPU workload fact is absent",
        )


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


def learning_state_receipt(
    row: dict[str, str],
    observations: list[dict[str, Any]],
) -> dict[str, Any] | None:
    corpus = row["corpus_id"]
    if corpus not in LEARNING:
        return None
    by_repetition: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for item in observations:
        by_repetition[item["key"]["repetition"]].append(item)
    if corpus == "Y36":
        parameter_count = None
        hashes_by_repetition = {}
        for repetition, items in by_repetition.items():
            baseline = next(
                item["raw_result"]["numerical_state"]
                for item in items
                if item["key"]["workers"] == 1
            )
            require(baseline["available"] is True, "TAAE state absent")
            if parameter_count is None:
                parameter_count = baseline["parameter_count"]
            hashes = {
                item["raw_result"]["model_hash"] for item in items
            }
            require(
                len(hashes) == 1,
                "TAAE raw model hash changed across workers",
            )
            hashes_by_repetition[str(repetition)] = next(iter(hashes))
            for item in items:
                observed = item["raw_result"]["numerical_state"]
                require(
                    observed == baseline
                    and observed["parameter_count"] == parameter_count,
                    "TAAE numerical state changed across workers",
                )
        return {
            "mode": "raw_bit_and_numerical_exact",
            "model_hashes_by_repetition": hashes_by_repetition,
            "parameter_count": parameter_count,
            "status": "accepted",
        }
    hashes_by_repetition = {}
    for repetition, items in by_repetition.items():
        hashes = {
            item["raw_result"]["learned_state_hash"] for item in items
        }
        require(
            len(hashes) == 1,
            f"{corpus}: learned state hash changed across workers",
        )
        hashes_by_repetition[str(repetition)] = next(iter(hashes))
    return {
        "mode": "raw_bit_hash_exact",
        "hashes_by_repetition": hashes_by_repetition,
        "status": "accepted",
    }


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
    _, worker_affinity_sets = validate_performance_first_topology(header)
    require(
        set(header["learning_artifacts"]) == {"taae", "alga", "rlpso"},
        "learning artifact coverage drift",
    )
    import_receipt = header["compatible_observation_import"]
    import_source_by_hash: dict[str, dict[str, Any]] = {}
    if import_receipt["status"] == "compatible_observations_imported":
        import_path = receipt_path(import_receipt["logical_path"])
        require(
            import_path.is_file()
            and sha256(import_path) == import_receipt["raw_sha256"],
            "compatible observation source is absent or changed",
        )
        import_records = read_jsonl(import_path)
        require(
            import_records[0]["campaign_id"]
            == import_receipt["source_campaign_id"]
            and import_records[0]["source_commit"]
            == import_receipt["source_campaign_source_commit"]
            and len(import_records) - 1
            == import_receipt["source_observation_count"],
            "compatible observation source identity drift",
        )
        import_source_by_hash = {
            canonical_sha256(item): item for item in import_records[1:]
        }
        require(
            len(import_receipt["observations"])
            == import_receipt["imported_observation_count"]
            and {
                item["source_observation_sha256"]
                for item in import_receipt["observations"]
            }
            <= set(import_source_by_hash),
            "compatible observation import manifest drift",
        )
    else:
        require(
            import_receipt["status"] == "no_compatible_prior_raw"
            and import_receipt["imported_observation_count"] == 0,
            "unknown compatible observation import status",
        )
    h5_revalidation = header["post_thread_control_h5_revalidation"]
    require(
        sha256(receipt_path(h5_revalidation["logical_path"]))
        == h5_revalidation["sha256"],
        "post-thread-control H5 revalidation changed",
    )
    topology_h5 = header["performance_first_topology_h5_revalidation"]
    require(
        sha256(receipt_path(topology_h5["logical_path"]))
        == topology_h5["sha256"],
        "performance-first topology H5 revalidation changed",
    )
    phase_h5 = header["learning_phase_topology_h5_revalidation"]
    require(
        sha256(receipt_path(phase_h5["logical_path"]))
        == phase_h5["sha256"],
        "learning phase-topology H5 revalidation changed",
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
        and topology["torch_intraop_thread_budget"]
        == "1 deterministic semantic lane"
        and topology["torch_interop_threads"] == 1
        and topology["affinity_allocated_cpus"] == "exactly W"
        and topology["maximum_os_threads"]
        == "W+8 conservative deterministic-lane runtime allowance"
        and topology["maximum_cpu_time_to_wall"] == "W+1.0"
        and "at most W+8"
        in topology["os_thread_sources"]
        and "no outer executor" in topology["phase_separation"]
        and "intra-op 1" in topology["phase_separation"],
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
    imported_key_hashes: set[str] = set()
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
        acquisition = observation.get("acquisition_provenance")
        if acquisition is not None:
            require(
                acquisition["mode"] == "compatible_observation_import"
                and acquisition["source_raw_sha256"]
                == import_receipt["raw_sha256"]
                and acquisition["source_campaign_id"]
                == import_receipt["source_campaign_id"]
                and all(acquisition["compatibility_proof"].values()),
                f"{pair_id}: compatible acquisition provenance drift",
            )
            source_hash = acquisition["source_observation_sha256"]
            require(
                source_hash in import_source_by_hash,
                f"{pair_id}: compatible source observation absent",
            )
            source = import_source_by_hash[source_hash]
            require(
                acquisition["source_observation_key"] == source["key"],
                f"{pair_id}: compatible source key drift",
            )
            comparable = copy.deepcopy(observation)
            comparable.pop("acquisition_provenance")
            comparable["key"] = source["key"]
            require(
                comparable == source,
                f"{pair_id}: imported observation changed beyond identity key",
            )
            imported_key_hashes.add(canonical_sha256(key))
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
        require(
            key["affinity_cpus"] == worker_affinity_sets[str(key["workers"])],
            f"{pair_id}: affinity key differs from frozen W topology",
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
            == worker_affinity_sets[str(key["workers"])],
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
                process["peak_os_threads"] <= key["workers"] + 8
                and process["cpu_time_to_wall"] <= key["workers"] + 1.0,
                f"{pair_id}: nested Torch oversubscription detected",
            )
            require(
                raw.get("thread_topology") == {
                    "outer_workers": key["workers"],
                    "torch_intraop_threads": 1,
                    "torch_interop_threads": 1,
                },
                f"{pair_id}: learned runtime topology mismatch",
            )
            command = observation["command"]
            require(
                command[command.index("--torch-intraop-threads") + 1]
                == "1"
                and command[command.index("--torch-interop-threads") + 1]
                == "1",
                f"{pair_id}: learned command topology mismatch",
            )
        by_pair[pair_id].append(observation)

    require(
        imported_key_hashes
        == {
            item["imported_key_sha256"]
            for item in import_receipt.get("observations", [])
        },
        "compatible imported observation coverage drift",
    )
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
        and summary["topology_policy"]
        == "architecture_aware_performance_first"
        and summary["raw_observations_sha256"] == sha256(raw_path)
        and summary["minimum_stage_attribution"] == minimum_attribution
        and summary["non_target_baselines_in_readiness"] == 0,
        "H6 summary identity or cardinality drift",
    )
    validate_environment_sidecar(
        summary,
        header,
        verify_repository_state=verify_repository_state,
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
        require(
            target["learning_state_cross_worker_receipt"]
            == learning_state_receipt(row_by_pair[pair_id], items),
            f"{pair_id}: learning-state cross-worker receipt drift",
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
