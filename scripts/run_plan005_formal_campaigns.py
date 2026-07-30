#!/usr/bin/env python3
"""Run admitted Plan-005 target-native campaigns sequentially and resumably.

The suite admits one optimization process at a time.  Parallelism exists only
inside the selected C++ backend, whose worker count and CPU affinity come from
the accepted H6 scaling result.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import resource
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any, Iterator

from plan005_formal_common import (
    FINAL_MANIFEST,
    RESULT_ROOT,
    ROOT,
    formal_command,
    require,
    result_key,
    result_path,
    sha256,
    validate_manifest,
)


THREAD_ENVIRONMENT = {
    "OMP_DYNAMIC": "FALSE",
    "OMP_PROC_BIND": "TRUE",
    "OMP_PLACES": "cores",
    "MKL_DYNAMIC": "FALSE",
    "OPENBLAS_NUM_THREADS": "1",
    "VECLIB_MAXIMUM_THREADS": "1",
    "NUMEXPR_NUM_THREADS": "1",
}


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def atomic_write(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=path.name + ".",
        suffix=".tmp",
        dir=path.parent,
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as handle:
            json.dump(
                document,
                handle,
                indent=2,
                sort_keys=True,
                allow_nan=False,
            )
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        temporary.unlink(missing_ok=True)


def decode_json(stdout: str) -> dict[str, Any]:
    stripped = stdout.strip()
    require(bool(stripped), "optimizer produced empty stdout")
    try:
        result = json.loads(stripped)
    except json.JSONDecodeError as error:
        lines = [line for line in stripped.splitlines() if line.strip()]
        require(
            len(lines) == 1,
            "optimizer stdout is not one JSON document",
        )
        try:
            result = json.loads(lines[0])
        except json.JSONDecodeError as nested:
            raise RuntimeError(
                "optimizer stdout is not valid JSON"
            ) from nested
    require(isinstance(result, dict), "optimizer result is not a JSON object")
    return result


def observed_physical_fes(result: dict[str, Any]) -> int:
    for field in (
        "physical_fes",
        "complete_layout_evaluations",
        "completed_physical_fes",
    ):
        value = result.get(field)
        if isinstance(value, int):
            return value
    raise RuntimeError("optimizer result has no physical-FES receipt")


def validate_existing(
    path: Path,
    expected_key: dict[str, Any],
) -> bool:
    if not path.is_file():
        return False
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return bool(
        document.get("status") == "validated_complete"
        and document.get("result_key") == expected_key
        and document.get("observed_physical_fes")
        == expected_key["physical_fes_per_run"]
        and document.get("binary_sha256")
        == expected_key["binary_sha256"]
    )


def selected_campaigns(
    manifest: dict[str, Any],
    *,
    only_pair: str | None,
) -> list[dict[str, Any]]:
    campaigns = [
        campaign
        for campaign in manifest["campaigns"]
        if campaign["execution_admission"] == "ready_cpu"
    ]
    if only_pair is not None:
        campaigns = [
            campaign
            for campaign in campaigns
            if campaign["pair_id"] == only_pair
        ]
        require(campaigns, f"ready CPU pair not found: {only_pair}")
    # Fastest non-learning campaigns provide early end-to-end evidence.
    return sorted(
        campaigns,
        key=lambda item: (
            sum(
                case["physical_fes_per_run"]
                for case in item["cases"]
            )
            * item["optimization_seed_count"],
            item["pair_id"],
        ),
    )


def tasks(
    manifest: dict[str, Any],
    campaigns: list[dict[str, Any]],
) -> Iterator[
    tuple[dict[str, Any], dict[str, Any], int, dict[str, Any], Path]
]:
    for campaign in campaigns:
        for case in campaign["cases"]:
            for seed in campaign["optimization_seeds"]:
                key = result_key(manifest, campaign, case, seed)
                yield campaign, case, seed, key, result_path(
                    campaign, case, seed
                )


def run_one(
    manifest: dict[str, Any],
    campaign: dict[str, Any],
    case: dict[str, Any],
    seed: int,
    key: dict[str, Any],
    output: Path,
) -> dict[str, Any]:
    backend = campaign["backend"]
    binary = ROOT / backend["binary_logical_path"]
    require(binary.is_file(), f"optimizer binary absent: {binary}")
    require(
        sha256(binary) == backend["binary_sha256"],
        f"optimizer binary hash drift: {binary}",
    )
    affinity = backend["selected_affinity_cpus"]
    require(
        isinstance(affinity, list)
        and len(affinity) == backend["selected_workers"]
        and all(isinstance(cpu, int) and cpu >= 0 for cpu in affinity),
        f"{campaign['pair_id']}: invalid H6 affinity",
    )
    front_path = (
        output.with_suffix(".front.json")
        if campaign["objective_mode"] == "multiobjective"
        else None
    )
    command = formal_command(
        campaign,
        case,
        seed=seed,
        front_path=front_path,
    )
    environment = dict(os.environ)
    environment.update(THREAD_ENVIRONMENT)
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        preexec_fn=lambda: os.sched_setaffinity(0, set(affinity)),
    )
    elapsed = time.perf_counter() - started
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    require(
        completed.returncode == 0,
        "optimizer failed: "
        + json.dumps(command)
        + "\nstderr:\n"
        + completed.stderr[-4000:],
    )
    raw = decode_json(completed.stdout)
    observed_fes = observed_physical_fes(raw)
    require(
        observed_fes == case["physical_fes_per_run"],
        f"{campaign['pair_id']} {case['case_id']}: exact physical FES "
        f"required {case['physical_fes_per_run']}, observed {observed_fes}",
    )
    front_receipt = None
    if front_path is not None and front_path.is_file():
        front_receipt = {
            "logical_path": str(front_path.resolve().relative_to(ROOT)),
            "sha256": sha256(front_path),
            "bytes": front_path.stat().st_size,
        }
    if campaign["corpus_id"] == "T46":
        require(front_receipt is not None, "PBEA front artifact absent")
    document = {
        "schema_version": 1,
        "status": "validated_complete",
        "result_key": key,
        "case_id": case["case_id"],
        "command": command,
        "command_sha256": sha256_bytes(
            json.dumps(
                command,
                separators=(",", ":"),
            ).encode("utf-8")
        ),
        "binary_sha256": sha256(binary),
        "observed_physical_fes": observed_fes,
        "raw_result": raw,
        "front_artifact": front_receipt,
        "process": {
            "external_wall_seconds": elapsed,
            "user_cpu_seconds": after.ru_utime - before.ru_utime,
            "system_cpu_seconds": after.ru_stime - before.ru_stime,
            "affinity_cpus": affinity,
            "requested_workers": backend["selected_workers"],
            "stdout_sha256": sha256_bytes(
                completed.stdout.encode("utf-8")
            ),
            "stderr_sha256": sha256_bytes(
                completed.stderr.encode("utf-8")
            ),
            "stderr_tail": completed.stderr[-4000:],
        },
    }
    atomic_write(output, document)
    return document


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all-admissible", action="store_true", required=True)
    parser.add_argument("--backend-parallelism", type=int, choices=(1,), required=True)
    parser.add_argument("--manifest", type=Path, default=FINAL_MANIFEST)
    parser.add_argument("--only-pair")
    parser.add_argument("--max-runs", type=int)
    parser.add_argument("--dry-run", action="store_true")
    arguments = parser.parse_args()
    require(
        arguments.max_runs is None or arguments.max_runs > 0,
        "--max-runs must be positive",
    )
    manifest_path = arguments.manifest.resolve()
    require(manifest_path.is_file(), f"manifest absent: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    validate_manifest(manifest, prepared=False)
    require(
        manifest["status"] == "frozen_ready"
        and manifest["backend_parallelism"] == 1,
        "formal manifest is not frozen for sequential execution",
    )
    campaigns = selected_campaigns(
        manifest,
        only_pair=arguments.only_pair,
    )
    all_tasks = list(tasks(manifest, campaigns))
    if arguments.max_runs is not None:
        all_tasks = all_tasks[: arguments.max_runs]
    pending = [
        item
        for item in all_tasks
        if not validate_existing(item[4], item[3])
    ]
    if arguments.dry_run:
        print(
            json.dumps(
                {
                    "status": "dry_run",
                    "campaign_count": len(campaigns),
                    "selected_run_count": len(all_tasks),
                    "reusable_complete_count": len(all_tasks) - len(pending),
                    "pending_count": len(pending),
                    "pair_ids": [
                        campaign["pair_id"] for campaign in campaigns
                    ],
                },
                sort_keys=True,
            )
        )
        return 0
    control = RESULT_ROOT / "control"
    control.mkdir(parents=True, exist_ok=True)
    lock_path = control / "suite.lock"
    with lock_path.open("a+", encoding="utf-8") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("another formal campaign runner is active") from error
        started = time.time()
        completed_count = len(all_tasks) - len(pending)
        for index, (campaign, case, seed, key, output) in enumerate(
            pending,
            start=1,
        ):
            atomic_write(
                control / "status.json",
                {
                    "schema_version": 1,
                    "status": "running",
                    "suite_id": manifest["suite_id"],
                    "pid": os.getpid(),
                    "started_unix_seconds": started,
                    "selected_run_count": len(all_tasks),
                    "validated_complete_count": completed_count,
                    "pending_index": index,
                    "pending_count": len(pending),
                    "pair_id": campaign["pair_id"],
                    "case_id": case["case_id"],
                    "optimization_seed": seed,
                },
            )
            run_one(
                manifest,
                campaign,
                case,
                seed,
                key,
                output,
            )
            completed_count += 1
            print(
                "plan005_formal_run_complete "
                f"completed={completed_count}/{len(all_tasks)} "
                f"pair={campaign['pair_id']} case={case['case_id']} "
                f"seed={seed}",
                flush=True,
            )
        atomic_write(
            control / "status.json",
            {
                "schema_version": 1,
                "status": "complete",
                "suite_id": manifest["suite_id"],
                "pid": os.getpid(),
                "started_unix_seconds": started,
                "finished_unix_seconds": time.time(),
                "selected_run_count": len(all_tasks),
                "validated_complete_count": completed_count,
            },
        )
    print(
        "plan005_formal_campaigns_pass "
        f"campaigns={len(campaigns)} runs={len(all_tasks)} "
        f"validated_complete={completed_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
