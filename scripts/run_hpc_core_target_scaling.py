#!/usr/bin/env python3
"""Run append-only Plan-005 H6 scaling for the exact 23 target pairs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import random
import resource
import statistics
import subprocess
import tempfile
import time
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs/hpc_core_target_pairs.tsv"
BUILD = ROOT / "build/plan005-torch"
DEFAULT_RAW = (
    ROOT / "evidence/performance/plan005_h6_raw_observations_20260730.jsonl"
)
DEFAULT_SUMMARY = (
    ROOT / "evidence/performance/plan005_h6_summary_20260730.json"
)
ARTIFACT_DIR = BUILD / "plan005-h6-artifacts"
TMP_DIR = BUILD / "plan005-h6-tmp"
H5_REVALIDATION = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_thread_topology_revalidation_20260730.json"
)
LEARNING = {"Y36": "taae", "T45": "alga", "T42": "rlpso"}
SCALAR = {
    "S01", "S02", "S03", "S04", "S05", "L0608", "T37", "T38",
    "T39", "T40", "T41", "T47", "Y34", "Y35", "T42", "T45",
}
PERFORMANCE_FIELDS = {
    "requested_workers",
    "resolved_workers",
    "observed_workers",
    "workers",
    "thread_topology",
    "timing_seconds",
    "total_wall_seconds",
    "evaluator_wall_seconds",
    "evaluator_seconds",
    "algorithm_seconds",
    "end_to_end_seconds",
    "stage_receipts",
    "stages",
    "wall_seconds",
}
ENVIRONMENT_LIMITS = {
    "OMP_NUM_THREADS": "1",
    "OMP_MAX_ACTIVE_LEVELS": "1",
    "MKL_NUM_THREADS": "1",
    "OPENBLAS_NUM_THREADS": "1",
    "VECLIB_MAXIMUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def canonical_sha256(value: Any) -> str:
    return sha256_bytes(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    )


def relative(path: Path) -> str:
    return str(path.resolve().relative_to(ROOT))


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def append_jsonl(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (
        json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)
        + "\n"
    )
    with path.open("a", encoding="utf-8") as handle:
        handle.write(encoded)
        handle.flush()
        os.fsync(handle.fileno())


def read_registry() -> list[dict[str, str]]:
    with REGISTRY.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    require(len(rows) == 23, f"expected 23 target pairs, found {len(rows)}")
    require(
        all(row["role"] == "target" for row in rows),
        "non-target row entered the Plan-005 H6 scope",
    )
    return rows


def analysis(row: dict[str, str]) -> dict[str, Any]:
    return json.loads((ROOT / row["analysis_path"]).read_text(encoding="utf-8"))


def representative(row: dict[str, str]) -> tuple[str, int]:
    data = analysis(row)["H1_work_and_data_movement"]
    case_id = data["native_size_provenance"]["selected_native_cases"][
        "representative"
    ]
    physical_fes = data["actual_values"]["representative"]["FES"]
    require(
        isinstance(case_id, str) and case_id,
        f"{row['pair_id']}: representative case absent",
    )
    require(
        isinstance(physical_fes, int) and physical_fes > 0,
        f"{row['pair_id']}: representative FES absent",
    )
    return case_id, physical_fes


def binaries() -> dict[str, Path]:
    return {
        "trainer": (
            BUILD
            / "hpc/learning_libtorch/plan004_learning_target_hpc"
        ),
        "wflop": BUILD / "hpc/wflop_cpp/wflop_cpp_hpc",
        "taae": BUILD / "hpc/taae_cpp/taae_evolution_hpc",
        "ppga": BUILD / "hpc/ppga_cpp/ppga_nantong_hpc",
        "bde": BUILD / "hpc/bde_ws56_cpp/bde_ws56_hpc",
        "geoga": BUILD / "hpc/geoga_cpp/geoga_anholt_hpc",
        "gga": BUILD / "hpc/gga_cpp/gga_cpp_hpc",
        "pbea": BUILD / "hpc/pbea_cpp/pbea_cpp_hpc",
    }


def binary_for(corpus_id: str) -> str:
    if corpus_id in SCALAR:
        return "wflop"
    return {
        "Y36": "taae",
        "T43": "ppga",
        "T44": "bde",
        "L0726": "geoga",
        "Y06": "gga",
        "T36": "gga",
        "T46": "pbea",
    }[corpus_id]


def command_for(
    row: dict[str, str],
    *,
    case_id: str,
    physical_fes: int,
    workers: int,
    seed: int,
    artifact_paths: dict[str, Path],
    front_path: Path,
) -> list[str]:
    corpus = row["corpus_id"]
    binary = relative(binaries()[binary_for(corpus)])
    if corpus in SCALAR:
        command = [
            binary,
            "--algorithm", row["algorithm_id"],
            "--problem", row["problem_id"],
            "--cases", row["native_asset"],
            "--case", case_id,
            "--paper-protocol", row["paper_protocol_id"],
            "--physical-fes", str(physical_fes),
            "--seed", str(seed),
            "--workers", str(workers),
            "--compute-backend", "cpu",
        ]
        if corpus == "S04":
            command.extend(
                ["--rlfode-models", "shared/models/fqfode_seeded"]
            )
        if corpus in LEARNING:
            command.extend([
                "--training-artifact",
                relative(artifact_paths[LEARNING[corpus]]),
                "--torch-intraop-threads", str(workers),
                "--torch-interop-threads", "1",
            ])
        return command
    if corpus == "Y36":
        return [
            binary,
            "--cases", row["native_asset"],
            "--case", case_id,
            "--profile", "bounded",
            "--physical-fes", str(physical_fes),
            "--seed", str(seed),
            "--workers", str(workers),
            "--backend", "cpu",
            "--learning-artifact", relative(artifact_paths["taae"]),
            "--torch-intraop-threads", str(workers),
            "--torch-interop-threads", "1",
        ]
    if corpus == "T43":
        return [
            binary,
            "--cases", row["native_asset"],
            "--case", case_id,
            "--seed", str(seed),
            "--physical-fes", str(physical_fes),
            "--workers", str(workers),
            "--backend", "cpu",
        ]
    if corpus == "T44":
        return [
            binary,
            "--cases", row["native_asset"],
            "--case", case_id,
            "--seed", str(seed),
            "--physical-fes", str(physical_fes),
            "--workers", str(workers),
            "--execution-mode", "cpu",
        ]
    if corpus == "L0726":
        return [
            binary,
            "--case", row["native_asset"],
            "--seed", str(seed),
            "--physical-fes", str(physical_fes),
            "--workers", str(workers),
            "--backend", "cpu",
        ]
    if corpus in {"Y06", "T36"}:
        command = [
            binary,
            "--problem", ".source-cache/generated/gga_repaired/Denmark_Nysted.wfp",
            "--physical-fes", str(physical_fes),
            "--workers", str(workers),
            "--seed", str(seed),
            "--algorithm", "gga" if corpus == "Y06" else "tmoea",
            "--execution-mode", "cpu",
        ]
        if corpus == "T36":
            command.extend(["--tmoea-profile", "paper-eq16-v2"])
        return command
    if corpus == "T46":
        return [
            binary,
            "--algorithm", "moead_p",
            "--scenario", "ws1",
            "--turbines", "23",
            "--population", "100",
            "--generations", "100",
            "--workers", str(workers),
            "--seed", str(seed),
            "--execution-mode", "cpu",
            "--output-front", str(front_path),
        ]
    raise RuntimeError(f"{corpus}: no production-representative route")


def parse_sched_migrations(path: Path) -> int:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.lstrip().startswith("se.nr_migrations"):
                return int(line.rsplit(":", 1)[1].strip())
    except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
        pass
    return 0


def status_value(path: Path, name: str) -> int:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith(name + ":"):
                return int(line.split()[1])
    except (FileNotFoundError, PermissionError, ProcessLookupError, ValueError):
        pass
    return 0


def decode_json(output: str, label: str) -> dict[str, Any]:
    try:
        value = json.loads(output)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{label}: stdout is not one JSON document") from error
    require(isinstance(value, dict), f"{label}: JSON result is not an object")
    return value


def measured_process(
    command: list[str],
    cpus: list[int],
    *,
    front_path: Path,
) -> tuple[dict[str, Any], dict[str, Any]]:
    environment = dict(os.environ)
    environment.update(ENVIRONMENT_LIMITS)
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter()
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w+", encoding="utf-8", dir=TMP_DIR, prefix="stdout-"
    ) as stdout_handle, tempfile.NamedTemporaryFile(
        mode="w+", encoding="utf-8", dir=TMP_DIR, prefix="stderr-"
    ) as stderr_handle:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            env=environment,
            stdout=stdout_handle,
            stderr=stderr_handle,
            text=True,
            preexec_fn=lambda: os.sched_setaffinity(0, set(cpus)),
        )
        peak_threads = 0
        peak_rss_kib = 0
        peak_migrations = 0
        affinity_union: set[int] = set()
        while process.poll() is None:
            task_root = Path(f"/proc/{process.pid}/task")
            tasks = list(task_root.iterdir()) if task_root.exists() else []
            peak_threads = max(peak_threads, len(tasks))
            peak_rss_kib = max(
                peak_rss_kib,
                status_value(Path(f"/proc/{process.pid}/status"), "VmRSS"),
            )
            migrations = 0
            for task in tasks:
                try:
                    affinity_union.update(os.sched_getaffinity(int(task.name)))
                except (FileNotFoundError, PermissionError, ProcessLookupError):
                    pass
                migrations += parse_sched_migrations(task / "sched")
            peak_migrations = max(peak_migrations, migrations)
            time.sleep(0.01)
        return_code = process.wait()
        elapsed = time.perf_counter() - started
        stdout_handle.seek(0)
        stderr_handle.seek(0)
        stdout = stdout_handle.read()
        stderr = stderr_handle.read()
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    require(
        return_code == 0,
        "command failed "
        + json.dumps(command)
        + f"\nstderr:\n{stderr[-4000:]}",
    )
    raw = decode_json(stdout, command[0])
    if front_path.is_file():
        raw["front_artifact_sha256"] = sha256(front_path)
        raw["front_artifact_bytes"] = front_path.stat().st_size
    process_receipt = {
        "external_wall_seconds": elapsed,
        "user_cpu_seconds": after.ru_utime - before.ru_utime,
        "system_cpu_seconds": after.ru_stime - before.ru_stime,
        "cpu_time_to_wall": (
            (after.ru_utime - before.ru_utime + after.ru_stime - before.ru_stime)
            / elapsed
        ),
        "maximum_resident_set_kib": peak_rss_kib,
        "voluntary_context_switches": after.ru_nvcsw - before.ru_nvcsw,
        "involuntary_context_switches": after.ru_nivcsw - before.ru_nivcsw,
        "peak_os_threads": peak_threads,
        "cpu_migrations": peak_migrations,
        "cpu_migration_measurement": (
            "sum of per-thread se.nr_migrations sampled from "
            "/proc/PID/task/TID/sched; perf_event_paranoid=4"
        ),
        "affinity_cpu_union": sorted(affinity_union),
        "stderr_sha256": sha256_bytes(stderr.encode("utf-8")),
        "stdout_sha256": sha256_bytes(stdout.encode("utf-8")),
    }
    return raw, process_receipt


def scientific_payload(value: Any) -> Any:
    if isinstance(value, dict):
        return {
            key: scientific_payload(item)
            for key, item in value.items()
            if key not in PERFORMANCE_FIELDS
        }
    if isinstance(value, list):
        return [scientific_payload(item) for item in value]
    return value


def internal_end_to_end(raw: dict[str, Any]) -> float:
    timing = raw.get("timing_seconds", {})
    candidates = (
        raw.get("total_wall_seconds"),
        raw.get("end_to_end_seconds"),
        timing.get("end_to_end") if isinstance(timing, dict) else None,
    )
    for value in candidates:
        if isinstance(value, (int, float)) and value > 0.0:
            return float(value)
    raise RuntimeError("end-to-end timing is absent")


def normalized_stages(raw: dict[str, Any]) -> dict[str, float]:
    stages = raw.get("stage_receipts", raw.get("stages"))
    if isinstance(stages, dict):
        result = {
            name: float(receipt["wall_seconds"])
            for name, receipt in stages.items()
        }
        require(all(value >= 0.0 for value in result.values()), "negative stage")
        return result
    timing = raw.get("timing_seconds")
    if isinstance(timing, dict):
        evaluator = timing.get("evaluator")
        algorithm = timing.get("algorithm")
        if isinstance(evaluator, (int, float)) and isinstance(
            algorithm, (int, float)
        ):
            return {
                "evaluator": float(evaluator),
                "algorithm_update_repair_selection_sync_allocation_serialization": (
                    float(algorithm)
                ),
            }
    if all(
        isinstance(raw.get(field), (int, float))
        for field in ("evaluator_seconds", "algorithm_seconds")
    ):
        return {
            "evaluator": float(raw["evaluator_seconds"]),
            "algorithm_update_repair_selection_sync_allocation_serialization": (
                float(raw["algorithm_seconds"])
            ),
        }
    raise RuntimeError("named H0 stage timings are absent")


def active_worker_receipt(
    raw: dict[str, Any],
    requested_workers: int,
) -> dict[str, Any]:
    observed = raw.get(
        "observed_workers",
        raw.get("resolved_workers", raw.get("workers", requested_workers)),
    )
    stages = raw.get("stage_receipts", raw.get("stages", {}))
    parallel_regions = 0
    participant_activations = 0
    peak_participants = 0
    distinct_participants = 0
    if isinstance(stages, dict):
        for receipt in stages.values():
            parallel_regions += int(receipt.get("parallel_regions", 0))
            participant_activations += int(
                receipt.get("participant_activations", 0)
            )
            peak_participants = max(
                peak_participants,
                int(receipt.get("peak_region_participants", 0)),
            )
            distinct_participants = max(
                distinct_participants,
                int(receipt.get("distinct_participants", 0)),
            )
    utilization = (
        participant_activations / (parallel_regions * requested_workers)
        if parallel_regions
        else None
    )
    return {
        "requested_outer_workers": requested_workers,
        "observed_outer_workers": int(observed),
        "parallel_regions": parallel_regions,
        "barrier_completions": parallel_regions,
        "participant_activations": participant_activations,
        "distinct_participants": distinct_participants,
        "peak_region_participants": peak_participants,
        "participant_activation_utilization": utilization,
        "imbalance_proxy": (
            1.0 - min(1.0, utilization)
            if utilization is not None
            else None
        ),
    }


def train_artifacts(
    source_commit: str,
    cpus: list[int],
) -> tuple[dict[str, Path], dict[str, Any]]:
    ARTIFACT_DIR.mkdir(parents=True, exist_ok=True)
    trainer = binaries()["trainer"]
    require(trainer.is_file(), f"missing trainer: {trainer}")
    artifact_paths: dict[str, Path] = {}
    records: dict[str, Any] = {}
    for index, method in enumerate(("taae", "alga", "rlpso")):
        artifact = ARTIFACT_DIR / f"{method}-bounded-seed-2026073000.pt"
        sidecar = artifact.with_suffix(".receipt.json")
        if artifact.is_file() and sidecar.is_file():
            receipt = json.loads(sidecar.read_text(encoding="utf-8"))
            require(
                receipt["artifact_sha256"] == sha256(artifact),
                f"{method}: local H6 artifact hash changed",
            )
        else:
            command = [
                relative(trainer),
                "--method", method,
                "--backend", "cpu",
                "--artifact-out", relative(artifact),
                "--seed", "2026073000",
                "--torch-intraop-threads", "1",
                "--torch-interop-threads", "1",
            ]
            environment = dict(os.environ)
            environment.update(ENVIRONMENT_LIMITS)
            completed = subprocess.run(
                command,
                cwd=ROOT,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
                preexec_fn=lambda: os.sched_setaffinity(0, {cpus[0]}),
            )
            result = decode_json(completed.stdout, f"{method} trainer")
            require(
                result.get("thread_topology") == {
                    "torch_intraop_threads": 1,
                    "torch_interop_threads": 1,
                },
                f"{method}: trainer thread topology mismatch",
            )
            receipt = {
                "method": method,
                "source_commit": source_commit,
                "trainer_binary_sha256": sha256(trainer),
                "command": command,
                "command_sha256": canonical_sha256(command),
                "artifact_logical_path": relative(artifact),
                "artifact_sha256": sha256(artifact),
                "artifact_bytes": artifact.stat().st_size,
                "training_result": result,
            }
            atomic_json(sidecar, receipt)
        artifact_paths[method] = artifact
        records[method] = receipt
    return artifact_paths, records


def load_jsonl(path: Path) -> tuple[dict[str, Any] | None, list[dict[str, Any]]]:
    if not path.is_file():
        return None, []
    records = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    require(records and records[0]["record_type"] == "campaign_header", "header")
    return records[0], records[1:]


def bootstrap_ci(values: list[float], seed: int) -> list[float]:
    generator = random.Random(seed)
    medians = []
    for _ in range(10000):
        sample = [generator.choice(values) for _ in values]
        medians.append(statistics.median(sample))
    medians.sort()
    return [medians[249], medians[9749]]


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
        sample = [generator.choice(ratios) for _ in ratios]
        bootstrapped.append(statistics.median(sample))
    bootstrapped.sort()
    return statistics.median(ratios), [
        bootstrapped[249],
        bootstrapped[9749],
    ]


def amdahl_fit(worker_medians: dict[int, float]) -> tuple[float, dict[int, float]]:
    baseline = worker_medians[1]
    best_s = 1.0
    best_error = math.inf
    for index in range(10001):
        serial = index / 10000.0
        error = 0.0
        for workers, observed in worker_medians.items():
            predicted = baseline * (serial + (1.0 - serial) / workers)
            error += ((predicted - observed) / observed) ** 2
        if error < best_error:
            best_error = error
            best_s = serial
    prediction_error = {}
    for workers, observed in worker_medians.items():
        predicted = baseline * (best_s + (1.0 - best_s) / workers)
        prediction_error[workers] = abs(predicted - observed) / observed
    return best_s, prediction_error


def summarize_target(
    row: dict[str, str],
    observations: list[dict[str, Any]],
    workers: list[int],
) -> dict[str, Any]:
    grouped: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for observation in observations:
        grouped[observation["key"]["workers"]].append(observation)
    require(set(grouped) == set(workers), f"{row['pair_id']}: worker coverage")
    statistics_by_worker: dict[str, Any] = {}
    medians: dict[int, float] = {}
    cis: dict[int, list[float]] = {}
    for worker in workers:
        values = sorted(
            item["timing"]["algorithm_end_to_end_seconds"]
            for item in grouped[worker]
        )
        median = statistics.median(values)
        medians[worker] = median
        cis[worker] = bootstrap_ci(values, 5000 + worker)
        statistics_by_worker[str(worker)] = {
            "repetitions": len(values),
            "median_seconds": median,
            "median_absolute_deviation_seconds": statistics.median(
                [abs(value - median) for value in values]
            ),
            "bootstrap_median_95_ci_seconds": cis[worker],
            "minimum_seconds": min(values),
            "maximum_seconds": max(values),
            "median_throughput_fes_per_second": statistics.median(
                item["timing"]["throughput_fes_per_second"]
                for item in grouped[worker]
            ),
            "median_cpu_time_to_wall": statistics.median(
                item["process"]["cpu_time_to_wall"]
                for item in grouped[worker]
            ),
            "maximum_resident_set_kib": max(
                item["process"]["maximum_resident_set_kib"]
                for item in grouped[worker]
            ),
            "maximum_cpu_migrations": max(
                item["process"]["cpu_migrations"]
                for item in grouped[worker]
            ),
        }
    baseline = medians[1]
    for worker in workers:
        speedup = baseline / medians[worker]
        statistics_by_worker[str(worker)]["speedup"] = speedup
        statistics_by_worker[str(worker)]["parallel_efficiency"] = (
            speedup / worker
        )
    best_worker = min(workers, key=lambda value: medians[value])
    paired_ratio, paired_ratio_ci = paired_speed_ratio_ci(
        observations,
        best_worker,
        workers[-1],
        int(hashlib.sha256(row["pair_id"].encode()).hexdigest()[:8], 16),
    )
    all_visible_tied = (
        best_worker == workers[-1] or paired_ratio_ci[0] >= 0.95
    )
    selected_worker = workers[-1] if all_visible_tied else best_worker
    serial_fraction, errors = amdahl_fit(medians)
    for worker, error in errors.items():
        statistics_by_worker[str(worker)][
            "h0_amdahl_prediction_relative_error"
        ] = error
    science_by_repetition: dict[int, set[str]] = defaultdict(set)
    for item in observations:
        science_by_repetition[item["key"]["repetition"]].add(
            item["scientific_output_sha256"]
        )
    require(
        all(len(values) == 1 for values in science_by_repetition.values()),
        f"{row['pair_id']}: scientific result changed across worker counts",
    )
    attribution_minimum = min(
        item["timing"]["named_h0_stage_attribution"]
        for item in observations
    )
    require(
        attribution_minimum >= 0.95,
        f"{row['pair_id']}: stage attribution below 95 percent",
    )
    h2 = analysis(row)["H2_dependency_and_parallel_width"]
    h3 = analysis(row)["H3_performance_and_granularity"]
    analysis_path = ROOT / row["analysis_path"]
    serial_limited = (
        best_worker == 1
        and statistics_by_worker[str(workers[-1])]["speedup"] <= 1.10
    )
    return {
        "pair_id": row["pair_id"],
        "corpus_id": row["corpus_id"],
        "algorithm_id": row["algorithm_id"],
        "method_semantic_id": row["method_semantic_id"],
        "problem_id": row["problem_id"],
        "problem_semantic_id": row["problem_semantic_id"],
        "paper_protocol_id": row["paper_protocol_id"],
        "native_asset": row["native_asset"],
        "representative_case": observations[0]["key"]["case_id"],
        "fixed_physical_fes": observations[0]["key"]["physical_fes"],
        "backend": "cpu_hpc_v1",
        "worker_statistics": statistics_by_worker,
        "measured_serial_fraction": serial_fraction,
        "minimum_named_h0_stage_attribution": attribution_minimum,
        "fastest_measured_workers": best_worker,
        "all_visible_workers": workers[-1],
        "all_visible_statistically_tied_with_fastest": all_visible_tied,
        "all_visible_relative_to_fastest_paired_speed_ratio": paired_ratio,
        "all_visible_relative_to_fastest_paired_bootstrap_95_ci": (
            paired_ratio_ci
        ),
        "all_visible_tie_lower_ratio_threshold": 0.95,
        "selected_workers": selected_worker,
        "selection_rule": (
            "all-visible selected when it is fastest or the paired bootstrap "
            "95 percent lower bound of T_fastest/T_all_visible is at least "
            "0.95; otherwise the fastest measured topology is selected"
        ),
        "serial_limited": serial_limited,
        "dependency_proof": {
            "analysis_path": row["analysis_path"],
            "analysis_sha256": sha256(analysis_path),
            "h2_dependency_edges": h2["dependency_edges"],
            "h2_dependency_edges_sha256": canonical_sha256(
                h2["dependency_edges"]
            ),
            "h2_ordered_sections": h2["ordered_sections"],
            "h3_granularity_rule": h3["granularity_rule"],
            "h3_dispatch_crossover_source": h3[
                "dispatch_crossover_source"
            ],
            "measured_crossover": {
                "fastest_workers": best_worker,
                "all_visible_workers": workers[-1],
                "all_visible_speedup": statistics_by_worker[
                    str(workers[-1])
                ]["speedup"],
                "measured_serial_fraction": serial_fraction,
            },
        },
        "observation_count": len(observations),
        "status": "accepted_h6",
    }


def update_acceptance(
    rows: list[dict[str, str]],
    targets: list[dict[str, Any]],
    raw_path: Path,
    summary_path: Path,
) -> None:
    by_pair = {item["pair_id"]: item for item in targets}
    summary_hash = sha256(summary_path)
    for row in rows:
        analysis_path = ROOT / row["analysis_path"]
        validation_path = analysis_path.with_name(
            analysis_path.name.replace(
                "_hpc_analysis.json", "_hpc_validation.json"
            )
        )
        document = json.loads(validation_path.read_text(encoding="utf-8"))
        require(
            document["H5_bounded_equivalence"]["status"] == "accepted_h5",
            f"{row['pair_id']}: accepted H5 was not preserved",
        )
        target = by_pair[row["pair_id"]]
        document["H6_performance_validation"] = {
            "status": "accepted_h6",
            "accepted_backend": target["backend"],
            "selected_workers": target["selected_workers"],
            "all_visible_workers": target["all_visible_workers"],
            "all_visible_characterized": True,
            "measured_serial_fraction": target["measured_serial_fraction"],
            "minimum_named_h0_stage_attribution": target[
                "minimum_named_h0_stage_attribution"
            ],
            "performance_receipt": relative(summary_path),
            "performance_receipt_sha256": summary_hash,
            "raw_observation_receipt": relative(raw_path),
            "claim_boundary": (
                "Measured fixed-work H6 backend selection only; failed "
                "speedup hypotheses remain measured results and no formal "
                "quality claim is made."
            ),
        }
        document["overall_status"] = "accepted_h5_h6"
        atomic_json(validation_path, document)
    fields = list(rows[0])
    for row in rows:
        row["validation_status"] = "accepted_h5_h6"
    temporary = REGISTRY.with_suffix(".tsv.tmp")
    with temporary.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, delimiter="\t", fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(REGISTRY)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workers", default="1,2,4,8,12,16,20")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--balanced-order", action="store_true")
    parser.add_argument("--scope", choices=("core",), required=True)
    parser.add_argument("--production-representative", action="store_true")
    parser.add_argument("--receipt", type=Path, default=DEFAULT_RAW)
    parser.add_argument("--summary", type=Path, default=DEFAULT_SUMMARY)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--only-corpus")
    arguments = parser.parse_args()
    workers = [int(value) for value in arguments.workers.split(",")]
    require(
        workers == [1, 2, 4, 8, 12, 16, 20],
        "Plan-005 H6 requires worker counts 1,2,4,8,12,16,20",
    )
    require(arguments.repetitions >= 5, "Plan-005 H6 requires five repetitions")
    require(arguments.balanced_order, "--balanced-order is required")
    require(
        arguments.production_representative,
        "--production-representative is required",
    )
    available_cpus = sorted(os.sched_getaffinity(0))
    require(
        len(available_cpus) >= workers[-1],
        "fewer than 20 affinity-visible CPUs",
    )
    rows = read_registry()
    if arguments.only_corpus:
        rows = [row for row in rows if row["corpus_id"] == arguments.only_corpus]
        require(len(rows) == 1, "unknown --only-corpus")
    binary_paths = binaries()
    require(
        all(path.is_file() for path in binary_paths.values()),
        "fresh Torch build is incomplete",
    )
    source_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    artifact_placeholders = {
        method: ARTIFACT_DIR / f"{method}-bounded-seed-2026073000.pt"
        for method in ("taae", "alga", "rlpso")
    }
    if arguments.dry_run:
        specifications = []
        for pair_index, row in enumerate(rows):
            case_id, physical_fes = representative(row)
            for repetition in range(arguments.repetitions):
                rotation = repetition % len(workers)
                ordered = workers[rotation:] + workers[:rotation]
                for order_index, worker in enumerate(ordered):
                    command = command_for(
                        row,
                        case_id=case_id,
                        physical_fes=physical_fes,
                        workers=worker,
                        seed=2026073000 + pair_index * 100 + repetition,
                        artifact_paths=artifact_placeholders,
                        front_path=TMP_DIR / "front.json",
                    )
                    specifications.append({
                        "pair_id": row["pair_id"],
                        "workers": worker,
                        "repetition": repetition,
                        "order_index": order_index,
                        "affinity_cpus": available_cpus[:worker],
                        "torch_intraop_threads": (
                            worker if row["corpus_id"] in LEARNING else 0
                        ),
                        "torch_interop_threads": (
                            1 if row["corpus_id"] in LEARNING else 0
                        ),
                        "command": command,
                    })
        print(json.dumps({
            "status": "dry_run",
            "pair_count": len(rows),
            "observation_count": len(specifications),
            "specifications": specifications,
        }, sort_keys=True))
        return 0

    artifact_paths, artifact_records = train_artifacts(
        source_commit,
        available_cpus,
    )
    require(
        H5_REVALIDATION.is_file(),
        "Plan-005 post-thread-control H5 revalidation is absent",
    )
    binary_receipts = {
        name: {
            "logical_path": relative(path),
            "sha256": sha256(path),
        }
        for name, path in binary_paths.items()
    }
    environment = {
        "hostname": platform.node(),
        "architecture": platform.machine(),
        "kernel": platform.release(),
        "logical_cpu_count": os.cpu_count(),
        "affinity_visible_cpus": available_cpus,
        "cpu_model": next(
            (
                line.split(":", 1)[1].strip()
                for line in Path("/proc/cpuinfo").read_text(
                    encoding="utf-8"
                ).splitlines()
                if line.startswith("model name")
            ),
            "unknown",
        ),
        "thread_pool_environment": ENVIRONMENT_LIMITS,
    }
    environment["sha256"] = canonical_sha256(environment)
    header_expected = {
        "record_type": "campaign_header",
        "schema_version": 1,
        "campaign_id": "plan005_h6_core_scaling_spark_20260730",
        "source_commit": source_commit,
        "scope": "exact 23 target-only pairs",
        "workers": workers,
        "repetitions": arguments.repetitions,
        "measurement_order_policy": "balanced_rotation",
        "backend_parallelism": 1,
        "environment": environment,
        "binaries": binary_receipts,
        "learning_artifacts": artifact_records,
        "post_thread_control_h5_revalidation": {
            "logical_path": relative(H5_REVALIDATION),
            "sha256": sha256(H5_REVALIDATION),
        },
        "learning_thread_topology_contract": {
            "outer_persistent_workers": "W",
            "torch_intraop_threads": "W",
            "torch_interop_threads": 1,
            "affinity_allocated_cpus": "exactly W",
            "phase_separation": (
                "Torch calls execute between PersistentExecutor regions; "
                "the two pools never perform CPU work concurrently"
            ),
            "source_symbols": [
                "hpc/taae_cpp/src/evolution.cpp::LibTorchLearningModel",
                "hpc/wflop_cpp/src/algorithms/alga_attention_declared_reconstruction.cpp::train_libtorch_full_batch_step",
                "hpc/wflop_cpp/src/algorithms/rlpso.cpp::LibTorchRlpsoPolicy",
            ],
        },
        "claim_boundary": (
            "Production-representative fixed-work CPU H6 only; this receipt "
            "does not establish formal 25-seed quality or GPU performance."
        ),
    }
    header, existing = load_jsonl(arguments.receipt)
    if header is None:
        append_jsonl(arguments.receipt, header_expected)
        header = header_expected
    else:
        require(header == header_expected, "existing H6 header drifted")
    completed_keys = {
        canonical_sha256(item["key"]): item for item in existing
    }
    expected_count = len(rows) * len(workers) * arguments.repetitions
    for pair_index, row in enumerate(rows):
        case_id, physical_fes = representative(row)
        binary_name = binary_for(row["corpus_id"])
        binary_hash = binary_receipts[binary_name]["sha256"]
        for repetition in range(arguments.repetitions):
            rotation = repetition % len(workers)
            ordered = workers[rotation:] + workers[:rotation]
            for order_index, worker in enumerate(ordered):
                key = {
                    "pair_id": row["pair_id"],
                    "native_asset": row["native_asset"],
                    "case_id": case_id,
                    "physical_fes": physical_fes,
                    "source_commit": source_commit,
                    "binary_sha256": binary_hash,
                    "architecture": platform.machine(),
                    "workers": worker,
                    "repetition": repetition,
                    "order_index": order_index,
                    "seed": 2026073000 + pair_index * 100 + repetition,
                    "environment_sha256": environment["sha256"],
                }
                key_hash = canonical_sha256(key)
                if key_hash in completed_keys:
                    continue
                with tempfile.TemporaryDirectory(
                    prefix="run-", dir=TMP_DIR
                ) as temporary:
                    front_path = Path(temporary) / "front.json"
                    command = command_for(
                        row,
                        case_id=case_id,
                        physical_fes=physical_fes,
                        workers=worker,
                        seed=key["seed"],
                        artifact_paths=artifact_paths,
                        front_path=front_path,
                    )
                    raw, process_receipt = measured_process(
                        command,
                        available_cpus[:worker],
                        front_path=front_path,
                    )
                observed_fes = raw.get(
                    "physical_fes",
                    raw.get("complete_layout_evaluations"),
                )
                require(
                    observed_fes == physical_fes,
                    f"{row['pair_id']}: inexact physical FES",
                )
                active = active_worker_receipt(raw, worker)
                require(
                    active["observed_outer_workers"] == worker,
                    f"{row['pair_id']}: outer worker mismatch",
                )
                if active["parallel_regions"] > 0:
                    require(
                        active["participant_activations"] > 0
                        and 0 < active["distinct_participants"] <= worker
                        and 0 < active["peak_region_participants"] <= worker
                        and active["participant_activation_utilization"]
                        is not None
                        and 0.0
                        < active["participant_activation_utilization"]
                        <= 1.0 + 1.0e-12,
                        f"{row['pair_id']}: active-worker receipt invalid",
                    )
                require(
                    process_receipt["affinity_cpu_union"]
                    == available_cpus[:worker],
                    f"{row['pair_id']}: affinity escaped selected topology",
                )
                if row["corpus_id"] in LEARNING:
                    require(
                        raw.get("thread_topology") == {
                            "outer_workers": worker,
                            "torch_intraop_threads": worker,
                            "torch_interop_threads": 1,
                        },
                        f"{row['pair_id']}: learned thread topology mismatch",
                    )
                end_to_end = internal_end_to_end(raw)
                stages = normalized_stages(raw)
                accounted = sum(stages.values())
                attribution = accounted / end_to_end
                overlap = max(0.0, accounted - end_to_end)
                require(
                    attribution >= 0.95,
                    f"{row['pair_id']}: stage attribution below 95 percent",
                )
                require(
                    overlap <= max(1.0e-6, 0.01 * end_to_end),
                    f"{row['pair_id']}: stage timers overlap excessively",
                )
                observation = {
                    "record_type": "observation",
                    "schema_version": 1,
                    "key": key,
                    "command": command,
                    "command_sha256": canonical_sha256(command),
                    "process": process_receipt,
                    "active_workers": active,
                    "timing": {
                        "algorithm_end_to_end_seconds": end_to_end,
                        "external_process_wall_seconds": process_receipt[
                            "external_wall_seconds"
                        ],
                        "named_h0_stages_seconds": stages,
                        "named_h0_stage_attribution": attribution,
                        "unattributed_seconds": max(
                            0.0, end_to_end - accounted
                        ),
                        "stage_overlap_seconds": overlap,
                        "maximum_admissible_stage_overlap_seconds": max(
                            1.0e-6, 0.01 * end_to_end
                        ),
                        "throughput_fes_per_second": (
                            physical_fes / end_to_end
                        ),
                        "allocation_serialization_measurement": (
                            "separate counter when emitted; otherwise "
                            "included in the algorithm/selection_other stage"
                        ),
                        "synchronization_measurement": (
                            "parallel-region completion barriers plus any "
                            "combined algorithm stage"
                        ),
                    },
                    "scientific_output_sha256": canonical_sha256(
                        scientific_payload(raw)
                    ),
                    "raw_result": raw,
                }
                append_jsonl(arguments.receipt, observation)
                completed_keys[key_hash] = observation
                print(
                    "plan005_h6_observation "
                    f"pair={row['corpus_id']} workers={worker} "
                    f"repetition={repetition + 1}/{arguments.repetitions} "
                    f"order={order_index + 1}/{len(workers)} "
                    f"seconds={end_to_end:.6f}",
                    flush=True,
                )
    _, observations = load_jsonl(arguments.receipt)
    require(
        len(observations) == expected_count,
        f"expected {expected_count} observations, found {len(observations)}",
    )
    by_pair: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for item in observations:
        by_pair[item["key"]["pair_id"]].append(item)
    targets = [
        summarize_target(row, by_pair[row["pair_id"]], workers)
        for row in rows
    ]
    summary = {
        "schema_version": 1,
        "summary_id": "plan005_h6_core_scaling_summary_spark_20260730",
        "source_commit": source_commit,
        "raw_observations": relative(arguments.receipt),
        "raw_observations_sha256": sha256(arguments.receipt),
        "target_count": len(targets),
        "observation_count": len(observations),
        "workers": workers,
        "repetitions": arguments.repetitions,
        "measurement_order_policy": "balanced_rotation",
        "minimum_stage_attribution": min(
            item["minimum_named_h0_stage_attribution"] for item in targets
        ),
        "targets": targets,
        "non_target_baselines_in_readiness": 0,
        "status": "accepted_h6",
        "claim_boundary": header["claim_boundary"],
    }
    atomic_json(arguments.summary, summary)
    update_acceptance(rows, targets, arguments.receipt, arguments.summary)
    print(
        "plan005_h6_scaling_pass "
        f"pairs={len(targets)} observations={len(observations)} "
        f"minimum_attribution={summary['minimum_stage_attribution']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
