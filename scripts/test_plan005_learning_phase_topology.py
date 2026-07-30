#!/usr/bin/env python3
"""Validate phase-separated learning topology and bounded numerical parity."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import resource
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WFL = ROOT / "build/plan005-torch/hpc/wflop_cpp/wflop_cpp_hpc"
DEFAULT_TAAE = ROOT / "build/plan005-torch/hpc/taae_cpp/taae_evolution_hpc"
DEFAULT_TRAINER = (
    ROOT
    / "build/plan005-torch/hpc/learning_libtorch/"
    "plan004_learning_target_hpc"
)
AFFINITY = {
    1: [19],
    4: [15, 16, 17, 19],
    20: list(range(20)),
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run(command: list[str], workers: int) -> dict[str, Any]:
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter()
    process = subprocess.Popen(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        preexec_fn=lambda: os.sched_setaffinity(0, set(AFFINITY[workers])),
    )
    peak_threads = 0
    while process.poll() is None:
        try:
            peak_threads = max(
                peak_threads,
                len(os.listdir(f"/proc/{process.pid}/task")),
            )
        except FileNotFoundError:
            pass
        time.sleep(0.005)
    stdout, stderr = process.communicate()
    wall = time.perf_counter() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    require(
        process.returncode == 0,
        f"command failed: {command}\n{stderr[-4000:]}",
    )
    result = json.loads(stdout)
    cpu_ratio = (
        after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime
    ) / wall
    require(
        peak_threads <= 3 * workers + 4,
        f"W={workers}: nonlinear OS thread growth peak={peak_threads}",
    )
    require(
        cpu_ratio <= workers + 1.0,
        f"W={workers}: CPU-time/wall exceeds total thread budget",
    )
    return {
        "workers": workers,
        "peak_os_threads": peak_threads,
        "cpu_time_to_wall": cpu_ratio,
        "external_wall_seconds": wall,
        "result": result,
    }


def train_artifacts(trainer: Path, directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for method in ("taae", "alga", "rlpso"):
        artifact = directory / f"{method}-bounded-seed-2026073000.pt"
        completed = subprocess.run(
            [
                str(trainer.resolve()),
                "--method", method,
                "--backend", "cpu",
                "--artifact-out", str(artifact),
                "--seed", "2026073000",
                "--torch-intraop-threads", "1",
                "--torch-interop-threads", "1",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        require(
            completed.returncode == 0 and artifact.is_file(),
            f"{method}: phase-topology artifact training failed\n"
            f"{completed.stderr[-4000:]}",
        )


def static_scan() -> dict[str, Any]:
    taae = (ROOT / "hpc/taae_cpp/src/evolution.cpp").read_text()
    alga = (
        ROOT
        / "hpc/wflop_cpp/src/algorithms/"
        "alga_attention_declared_reconstruction.cpp"
    ).read_text()
    rlpso = (
        ROOT / "hpc/wflop_cpp/src/algorithms/rlpso.cpp"
    ).read_text()
    nested_pattern = re.compile(
        r"executor\.parallel_for\([\s\S]{0,1600}"
        r"(?:model\.encode|model\.decode_argmax)\("
    )
    synthetic_bad = (
        "executor.parallel_for(0, n, [&](int i) { "
        "model.decode_argmax(latent[i]); });"
    )
    require(
        nested_pattern.search(synthetic_bad) is not None,
        "static negative control did not detect nested Torch call",
    )
    require(
        nested_pattern.search(taae) is None,
        "TAAE outer executor still calls scalar Torch encode/decode",
    )
    for token in (
        "encode_batch(",
        "decode_argmax_batch(",
        "model_->encode(input)",
        "model_->decode_argmax(input)",
    ):
        require(token in taae, f"TAAE batch token absent: {token}")
    require(
        alga.index("train_libtorch_full_batch_step(")
        < alga.index("executor.parallel_for(0, offspring_count"),
        "ALGA full-batch Torch step is not separated from outer CPU work",
    )
    for token in (
        "set_torch_intraop_threads(1)",
        "set_torch_intraop_threads(batch_threads_)",
    ):
        require(token in rlpso, f"RLPSO phase topology token absent: {token}")
    return {
        "synthetic_nested_negative_control": "rejected",
        "taae_batch_encode_decode": "present",
        "alga_full_batch_before_outer_cpu": "present",
        "rlpso_sequential_inference_and_batch_update": "present",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--static-only", action="store_true")
    parser.add_argument("--taae-binary", type=Path, default=DEFAULT_TAAE)
    parser.add_argument("--wflop-binary", type=Path, default=DEFAULT_WFL)
    parser.add_argument("--trainer", type=Path, default=DEFAULT_TRAINER)
    parser.add_argument("--artifact-dir", type=Path)
    arguments = parser.parse_args()
    static = static_scan()
    if arguments.static_only:
        print(json.dumps({"status": "pass", "static": static}, sort_keys=True))
        return 0

    temporary_artifacts = None
    if arguments.artifact_dir is None:
        temporary_artifacts = tempfile.TemporaryDirectory(
            prefix="plan005-learning-phase-topology-"
        )
        artifact_dir = Path(temporary_artifacts.name)
        train_artifacts(arguments.trainer, artifact_dir)
    else:
        artifact_dir = arguments.artifact_dir.resolve()

    taae_base = [
        str(arguments.taae_binary.resolve()),
        "--cases", "shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json",
        "--case", "TAAE_Proxy_NC1_Budget600k_tn15",
        "--profile", "bounded",
        "--physical-fes", "350",
        "--seed", "2026073000",
        "--backend", "cpu",
        "--learning-artifact",
        str(
            artifact_dir / "taae-bounded-seed-2026073000.pt"
        ),
        "--torch-interop-threads", "1",
    ]
    taae_runs = [
        run(
            taae_base
            + ["--workers", str(worker), "--torch-intraop-threads", str(worker)],
            worker,
        )
        for worker in (1, 4)
    ]
    discrete_fields = (
        "physical_fes",
        "front_hash",
        "population_layout_hash",
        "learning_decision_hash",
    )
    baseline = taae_runs[0]["result"]
    require(
        all(
            item["result"][field] == baseline[field]
            for item in taae_runs
            for field in discrete_fields
        ),
        "TAAE discrete science changed across workers",
    )
    numerical_fields = ("l2_norm", "linf_norm", "weighted_checksum")
    numerical_baseline = baseline["numerical_state"]
    numerical_differences = {}
    for item in taae_runs:
        observed = item["result"]["numerical_state"]
        require(
            observed["parameter_count"] == numerical_baseline["parameter_count"],
            "TAAE parameter count changed across workers",
        )
        for field in numerical_fields:
            difference = abs(observed[field] - numerical_baseline[field])
            numerical_differences[field] = max(
                numerical_differences.get(field, 0.0),
                difference,
            )
            require(
                math.isclose(
                    observed[field],
                    numerical_baseline[field],
                    rel_tol=1.0e-12,
                    abs_tol=1.0e-9,
                ),
                f"TAAE numerical state drift: {field}",
            )

    common = [
        str(arguments.wflop_binary.resolve()),
        "--seed", "2026073000",
        "--compute-backend", "cpu",
        "--torch-interop-threads", "1",
    ]
    algorithm_specs = {
        "alga": [
            "--algorithm", "alga_attention_declared_reconstruction_v1",
            "--problem", "alga_guishan_3d_declared_proxy_v1",
            "--cases", "shared/contracts/alga_guishan_3d_declared_proxy_cases.json",
            "--case", "ALGA_Guishan3D_IDEAL1_tn20",
            "--paper-protocol", "swevo2025_alga_native_25_v1",
            "--physical-fes", "90",
            "--training-artifact",
            str(
                artifact_dir / "alga-bounded-seed-2026073000.pt"
            ),
        ],
        "rlpso": [
            "--algorithm", "rlpso_paper_corrected_training_reconstruction_v1",
            "--problem", "rpso2024_source_problem_ws1_ws4",
            "--cases", ".source-cache/generated/rpso_source_problem/benchmark_cases.json",
            "--case", "RPSO-WS1-tn30",
            "--paper-protocol", "energy2024_rlpso_native_25_v1",
            "--physical-fes", "100",
            "--training-artifact",
            str(
                artifact_dir / "rlpso-bounded-seed-2026073000.pt"
            ),
        ],
    }
    exact_runs = {}
    for name, specification in algorithm_specs.items():
        runs = [
            run(
                common
                + specification
                + [
                    "--workers", str(worker),
                    "--torch-intraop-threads", str(worker),
                ],
                worker,
            )
            for worker in (1, 20)
        ]
        state_hashes = {
            item["result"]["learned_state_hash"] for item in runs
        }
        require(
            len(state_hashes) == 1,
            f"{name}: learned state raw hash changed across workers",
        )
        stable_fields = (
            "physical_fes",
            "best_expected_power_kw",
            "best_layout_1based",
            "learning_decision_hash",
            "terminal_partial_work",
            "policy_interactions",
            "policy_updates",
            "generations",
        )
        require(
            all(
                item["result"][field] == runs[0]["result"][field]
                for item in runs
                for field in stable_fields
            ),
            f"{name}: discrete science changed across workers",
        )
        exact_runs[name] = {
            "runs": [
                {
                    key: value
                    for key, value in item.items()
                    if key != "result"
                }
                for item in runs
            ],
            "learned_state_hash": next(iter(state_hashes)),
            "scientific_state": {
                field: runs[0]["result"][field]
                for field in stable_fields
            },
            "status": "raw_bit_and_discrete_exact",
        }
    report = {
        "schema_version": 1,
        "static": static,
        "thread_bound": {
            "peak_os_threads": "3*W+4",
            "cpu_time_to_wall": "W+1.0",
        },
        "taae": {
            "runs": [
                {
                    key: value
                    for key, value in item.items()
                    if key != "result"
                }
                for item in taae_runs
            ],
            "discrete_fields": list(discrete_fields),
            "discrete_status": "exact",
            "scientific_state": {
                field: baseline[field] for field in discrete_fields
            },
            "raw_model_hashes": [
                item["result"]["model_hash"] for item in taae_runs
            ],
            "numerical_state": {
                "parameter_count": numerical_baseline["parameter_count"],
                "relative_tolerance": 1.0e-12,
                "absolute_tolerance": 1.0e-9,
                "maximum_absolute_difference": numerical_differences,
                "status": "accepted",
            },
        },
        **exact_runs,
        "status": "pass",
    }
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
