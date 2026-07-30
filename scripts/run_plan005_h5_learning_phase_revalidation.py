#!/usr/bin/env python3
"""Create append-only H5 evidence after learning phase-topology repair."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import re
import subprocess
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PRIOR_H5 = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_thread_topology_revalidation_20260730.json"
)
DEFAULT_RECEIPT = (
    ROOT
    / "evidence/development/"
    "plan005_h5_post_learning_phase_topology_revalidation_20260730.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str]) -> dict[str, Any]:
    started = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    elapsed = time.perf_counter() - started
    output = completed.stdout + completed.stderr
    require(
        completed.returncode == 0,
        f"command failed: {json.dumps(command)}\n{output[-8000:]}",
    )
    return {
        "command": command,
        "return_code": completed.returncode,
        "measured_wall_seconds": elapsed,
        "output_sha256": hashlib.sha256(output.encode("utf-8")).hexdigest(),
        "output": output,
    }


def ctest_count(receipt: dict[str, Any], label: str) -> int:
    match = re.search(
        r"100% tests passed, 0 tests failed out of (\d+)",
        receipt["output"],
    )
    require(match is not None, f"{label}: clean CTest summary absent")
    return int(match.group(1))


def structured(output: str) -> list[dict[str, Any]]:
    records = []
    for line in output.splitlines():
        opening = line.find("{")
        if opening < 0:
            continue
        try:
            value = json.loads(line[opening:])
        except json.JSONDecodeError:
            continue
        if isinstance(value, dict):
            records.append(value)
    return records


def validate_phase_report(report: dict[str, Any]) -> None:
    require(report.get("status") == "pass", "phase topology report failed")
    require(
        report["static"] == {
            "synthetic_nested_negative_control": "rejected",
            "taae_batch_encode_decode": "present",
            "alga_full_batch_before_outer_cpu": "present",
            "rlpso_sequential_inference_and_batch_update": "present",
        },
        "static phase-topology proof drift",
    )
    taae = report["taae"]
    require(
        taae["discrete_status"] == "exact"
        and taae["scientific_state"]["physical_fes"] == 350
        and taae["numerical_state"]["status"] == "accepted"
        and taae["numerical_state"]["relative_tolerance"] == 1.0e-12
        and taae["numerical_state"]["absolute_tolerance"] == 1.0e-9,
        "TAAE H5 exact-FES/science/numerical gate failed",
    )
    for item in taae["runs"]:
        workers = item["workers"]
        require(
            item["peak_os_threads"] <= 3 * workers + 4
            and item["cpu_time_to_wall"] <= workers + 1.0,
            "TAAE H5 thread budget failed",
        )
    for method, physical_fes in (("alga", 90), ("rlpso", 100)):
        section = report[method]
        require(
            section["status"] == "raw_bit_and_discrete_exact"
            and section["scientific_state"]["physical_fes"] == physical_fes,
            f"{method}: H5 exact-state/FES gate failed",
        )
        for item in section["runs"]:
            workers = item["workers"]
            require(
                item["peak_os_threads"] <= 3 * workers + 4
                and item["cpu_time_to_wall"] <= workers + 1.0,
                f"{method}: H5 thread budget failed",
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--torch-build-dir", type=Path, required=True)
    parser.add_argument("--cpu-build-dir", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT)
    arguments = parser.parse_args()
    torch_build = arguments.torch_build_dir.resolve()
    cpu_build = arguments.cpu_build_dir.resolve()
    receipt = arguments.receipt.resolve()
    require(not receipt.exists(), f"append-only H5 receipt exists: {receipt}")
    require(PRIOR_H5.is_file(), "prior Plan-005 H5 receipt absent")

    cpu_suite = run([
        "ctest", "--test-dir", str(cpu_build), "--output-on-failure",
    ])
    torch_suite = run([
        "ctest", "--test-dir", str(torch_build), "--output-on-failure",
    ])
    cpu_count = ctest_count(cpu_suite, "CPU")
    torch_count = ctest_count(torch_suite, "Torch")
    require(cpu_count >= 74, "fresh CPU test count regressed")
    require(torch_count >= 86, "fresh Torch test count regressed")

    phase_test = run([
        "ctest", "--test-dir", str(torch_build), "-V",
        "--output-on-failure", "-R", "^plan005_learning_phase_topology$",
    ])
    reports = [
        item
        for item in structured(phase_test["output"])
        if item.get("status") == "pass" and "taae" in item
    ]
    require(len(reports) == 1, "structured phase-topology report absent")
    validate_phase_report(reports[0])

    receipt_fixture = run([
        "python3", "scripts/test_plan005_h6_receipt_audit.py",
    ])
    require(
        "rejected_nested_threads=1" in receipt_fixture["output"]
        and "rejected_numerical_state=1" in receipt_fixture["output"],
        "H6 topology/numerical tamper fixture did not reject both controls",
    )

    source_commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    binaries = {
        "trainer": (
            torch_build
            / "hpc/learning_libtorch/plan004_learning_target_hpc"
        ),
        "taae": torch_build / "hpc/taae_cpp/taae_evolution_hpc",
        "wflop": torch_build / "hpc/wflop_cpp/wflop_cpp_hpc",
        "cpu_taae": cpu_build / "hpc/taae_cpp/taae_evolution_hpc",
        "cpu_wflop": cpu_build / "hpc/wflop_cpp/wflop_cpp_hpc",
    }
    caches = {
        "torch": torch_build / "CMakeCache.txt",
        "cpu": cpu_build / "CMakeCache.txt",
    }
    for name, path in {**binaries, **caches}.items():
        require(path.is_file(), f"{name}: fresh-build artifact absent")

    document = {
        "schema_version": 1,
        "receipt_id": (
            "plan005_h5_post_learning_phase_topology_"
            "revalidation_spark_20260730"
        ),
        "source_commit": source_commit,
        "host": platform.node(),
        "architecture": platform.machine(),
        "prior_plan005_h5_receipt": str(PRIOR_H5.relative_to(ROOT)),
        "prior_plan005_h5_receipt_sha256": sha256(PRIOR_H5),
        "fresh_builds": {
            name: {
                "logical_path": str(path.relative_to(ROOT)),
                "cmake_cache_sha256": sha256(path),
            }
            for name, path in caches.items()
        },
        "full_cpu_ctest": cpu_suite,
        "full_cpu_test_count": cpu_count,
        "full_torch_ctest": torch_suite,
        "full_torch_test_count": torch_count,
        "phase_topology_test": phase_test,
        "phase_topology_report": reports[0],
        "h6_receipt_tamper_fixture": receipt_fixture,
        "binary_receipts": {
            name: {
                "logical_path": str(path.relative_to(ROOT)),
                "sha256": sha256(path),
            }
            for name, path in binaries.items()
        },
        "status": (
            "accepted_h5_revalidated_after_learning_phase_topology_repair"
        ),
        "claim_boundary": (
            "This H5 receipt proves fresh CPU/Torch regression success, "
            "exact discrete science and physical FES across tested worker "
            "counts, bounded TAAE numerical-state parity, raw-bit exact "
            "ALGA/RLPSO learned state, and rejection of nonlinear nested "
            "thread growth. It does not establish H6 scaling."
        ),
    }
    receipt.parent.mkdir(parents=True, exist_ok=True)
    with receipt.open("x", encoding="utf-8") as handle:
        handle.write(json.dumps(document, indent=2, sort_keys=True) + "\n")
    print(
        "plan005_h5_learning_phase_revalidation_pass "
        f"cpu_tests={cpu_count} torch_tests={torch_count} "
        "taae_numerical=accepted alga_rlpso_state=exact"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
