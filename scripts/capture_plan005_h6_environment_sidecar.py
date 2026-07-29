#!/usr/bin/env python3
"""Capture immutable architecture and concurrent-load context for H6."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
from datetime import datetime
from pathlib import Path
from typing import Any
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
RAW = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_raw_observations_20260730.jsonl"
)
DEFAULT_OUTPUT = (
    ROOT
    / "evidence/performance/"
    "plan005_h6_performance_first_environment_sidecar_20260730.json"
)
WORKERS = [1, 2, 4, 8, 12, 16, 20]


def command_output(command: list[str]) -> str:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def read_optional(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except (FileNotFoundError, PermissionError):
        return None


def parse_cpu_rows() -> list[dict[str, Any]]:
    raw = command_output([
        "lscpu", "-p=CPU,CORE,MODELNAME,MAXMHZ,MINMHZ,ONLINE",
    ])
    rows = []
    for line in raw.splitlines():
        if not line or line.startswith("#"):
            continue
        cpu, core, model, maximum, minimum, online = line.split(",")
        rows.append({
            "cpu": int(cpu),
            "core": int(core),
            "model_name": model,
            "maximum_mhz": float(maximum),
            "minimum_mhz": float(minimum),
            "online": online == "Y",
        })
    return rows


def cache_rows(cpus: list[int]) -> list[dict[str, Any]]:
    rows = []
    fields = (
        "level",
        "type",
        "size",
        "coherency_line_size",
        "number_of_sets",
        "ways_of_associativity",
        "shared_cpu_list",
    )
    for cpu in cpus:
        root = Path(f"/sys/devices/system/cpu/cpu{cpu}/cache")
        for index in sorted(root.glob("index*")):
            rows.append({
                "cpu": cpu,
                "index": index.name,
                **{field: read_optional(index / field) for field in fields},
            })
    return rows


def frequency_rows(cpus: list[int]) -> list[dict[str, Any]]:
    fields = (
        "scaling_driver",
        "scaling_governor",
        "scaling_cur_freq",
        "scaling_min_freq",
        "scaling_max_freq",
        "cpuinfo_min_freq",
        "cpuinfo_max_freq",
        "energy_performance_preference",
    )
    return [
        {
            "cpu": cpu,
            **{
                field: read_optional(
                    Path(
                        f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/{field}"
                    )
                )
                for field in fields
            },
        }
        for cpu in cpus
    ]


def background_gpu_processes() -> list[dict[str, Any]]:
    output = subprocess.run(
        [
            "nvidia-smi",
            "--query-compute-apps=pid,process_name,used_memory",
            "--format=csv,noheader,nounits",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if output.returncode != 0:
        return []
    rows = []
    for line in output.stdout.splitlines():
        if not line.strip():
            continue
        pid_text, executable, memory = [
            value.strip() for value in line.split(",", 2)
        ]
        pid = int(pid_text)
        ps = subprocess.run(
            [
                "ps", "-p", str(pid),
                "-o", "pid=,ppid=,etime=,time=,pcpu=,nlwp=,psr=,rss=,stat=,args=",
            ],
            capture_output=True,
            text=True,
        ).stdout.strip()
        tokens = ps.split(maxsplit=9)
        command = tokens[9] if len(tokens) == 10 else ""
        try:
            affinity = sorted(os.sched_getaffinity(pid))
        except (PermissionError, ProcessLookupError):
            affinity = []
        rows.append({
            "pid": pid,
            "process_label": Path(executable).name,
            "gpu_memory_mib": int(memory),
            "parent_pid": int(tokens[1]) if len(tokens) == 10 else None,
            "elapsed": tokens[2] if len(tokens) == 10 else None,
            "cpu_time": tokens[3] if len(tokens) == 10 else None,
            "lifetime_average_cpu_percent": (
                float(tokens[4]) if len(tokens) == 10 else None
            ),
            "observed_cpu_core_equivalent": (
                float(tokens[4]) / 100.0 if len(tokens) == 10 else None
            ),
            "os_threads": int(tokens[5]) if len(tokens) == 10 else None,
            "last_cpu": int(tokens[6]) if len(tokens) == 10 else None,
            "resident_set_kib": int(tokens[7]) if len(tokens) == 10 else None,
            "state": tokens[8] if len(tokens) == 10 else None,
            "affinity_cpus": affinity,
            "command_sha256": sha256_text(command),
            "task_identity": (
                "pre-existing Isaac Lab reinforcement-learning training"
                if "Isaac" in command or "isaaclab" in command
                else "pre-existing GPU compute process"
            ),
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    output = arguments.output.resolve()
    if output.exists():
        raise RuntimeError(f"append-only environment sidecar exists: {output}")
    first_line = RAW.read_text(encoding="utf-8").splitlines()[0]
    header = json.loads(first_line)
    lscpu_raw = command_output(["lscpu"])
    cpu_rows = parse_cpu_rows()
    cpus = header["environment"]["affinity_visible_cpus"]
    row_by_cpu = {row["cpu"]: row for row in cpu_rows}
    core_type_groups: dict[str, list[int]] = {}
    for row in cpu_rows:
        core_type_groups.setdefault(row["model_name"], []).append(row["cpu"])
    performance_first_composition = {}
    for workers in WORKERS:
        selection_order = header["environment"]["worker_selection_order"][
            str(workers)
        ]
        affinity = header["environment"]["worker_affinity_sets"][str(workers)]
        selected = [row_by_cpu[cpu] for cpu in selection_order]
        counts: dict[str, int] = {}
        for row in selected:
            counts[row["model_name"]] = counts.get(row["model_name"], 0) + 1
        performance_first_composition[str(workers)] = {
            "selection_order": selection_order,
            "affinity_set": affinity,
            "core_type_counts": counts,
        }
    cmake_cache = (
        ROOT / "build/plan005-torch/CMakeCache.txt"
    ).read_text(encoding="utf-8")
    selected_cmake = [
        line
        for line in cmake_cache.splitlines()
        if line.startswith((
            "CMAKE_BUILD_TYPE:",
            "CMAKE_CXX_FLAGS:",
            "CMAKE_CXX_FLAGS_RELEASE:",
            "WFLOP_ENABLE_TORCH:",
        ))
    ]
    document = {
        "schema_version": 1,
        "sidecar_id": (
            "plan005_h6_performance_first_environment_spark_20260730"
        ),
        "captured_at": datetime.now(ZoneInfo("Asia/Tokyo")).isoformat(),
        "h6_campaign_id": header["campaign_id"],
        "h6_source_commit": header["source_commit"],
        "host": platform.node(),
        "architecture": platform.machine(),
        "lscpu_raw": lscpu_raw,
        "lscpu_raw_sha256": sha256_text(lscpu_raw),
        "logical_cpu_rows": cpu_rows,
        "core_type_groups": core_type_groups,
        "topology_policy": header["topology_policy"],
        "performance_first_cpu_order": header["environment"][
            "performance_first_cpu_order"
        ],
        "worker_selection_order": header["environment"][
            "worker_selection_order"
        ],
        "worker_affinity_sets": header["environment"]["worker_affinity_sets"],
        "performance_first_worker_composition": (
            performance_first_composition
        ),
        "cache_sysfs": cache_rows(cpus),
        "frequency_governor_sysfs": frequency_rows(cpus),
        "compiler": command_output(["c++", "--version"]).splitlines()[0],
        "cmake": command_output(["cmake", "--version"]).splitlines()[0],
        "selected_cmake_cache_entries": selected_cmake,
        "selected_cmake_cache_entries_sha256": sha256_text(
            "\n".join(selected_cmake) + "\n"
        ),
        "pre_existing_gpu_compute_processes": background_gpu_processes(),
        "measurement_noise_boundary": (
            "The H6 target process is affinity-limited to the frozen "
            "architecture-aware performance-first W-CPU mapping, while any "
            "pre-existing Isaac Lab process is not affinity isolated. Its "
            "observed lifetime CPU use is environmental noise. Heterogeneous "
            "core composition at W=12,16,20 and concurrent CPU/GPU load must "
            "be considered before attributing nonlinear scaling solely to an "
            "algorithm."
        ),
        "claim_boundary": (
            "This sidecar characterizes the observed host and concurrent "
            "load. It does not remove noise, establish GPU H6, or change any "
            "target observation, runner, binary, seed, work budget, or order."
        ),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as handle:
        handle.write(json.dumps(document, indent=2, sort_keys=True) + "\n")
    print(
        "plan005_h6_environment_sidecar_pass "
        f"cpus={len(cpu_rows)} core_types={len(core_type_groups)} "
        f"background_gpu_processes={len(document['pre_existing_gpu_compute_processes'])}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
