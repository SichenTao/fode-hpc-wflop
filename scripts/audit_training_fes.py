#!/usr/bin/env python3
"""Audit learned-method training, inference, and total physical-FES ledgers."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/full/hpc/wflop_cpp"


def run_json(command: list[str]) -> dict:
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true")
    arguments = parser.parse_args()
    if not arguments.all:
        parser.error("--all is required")

    fq_contract = json.loads(
        (
            ROOT
            / "shared/contracts/fqfode_seeded_training_reconstruction_contract.json"
        ).read_text(encoding="utf-8")
    )
    if fq_contract["learned_method_receipt"]["training_physical_fes"] != 39700:
        raise RuntimeError("FQFODE baseline training FES differs")
    if (
        fq_contract["independent_stage_sensitivity_receipt"][
            "training_physical_fes"
        ]
        != 158800
    ):
        raise RuntimeError("FQFODE independent-stage training FES differs")

    sugga = json.loads(
        (
            ROOT / "shared/models/sugga_native/training_receipt.json"
        ).read_text(encoding="utf-8")
    )
    expected_sugga = (
        int(sugga["case_count"]) * int(sugga["samples_per_case"])
    )
    if sugga["training_physical_fes"] != expected_sugga:
        raise RuntimeError("SUGGA physical training FES does not reconcile")

    binary = BUILD / "wflop_cpp_hpc"
    rpso_cases = (
        ROOT / ".source-cache/generated/rpso_source_problem/benchmark_cases.json"
    )
    learned_runs = []
    for algorithm in (
        "rlpso_paper_corrected_training_reconstruction_v1",
        "rlpso_literal_official_source_replay_v1",
    ):
        result = run_json(
            [
                str(binary),
                "--algorithm",
                algorithm,
                "--problem",
                "rpso2024_source_problem_ws1_ws4",
                "--cases",
                str(rpso_cases),
                "--case",
                "RPSO-WS1-tn30",
                "--physical-fes",
                "480",
                "--seed",
                "2026072901",
                "--workers",
                "20",
            ]
        )
        if (
            result["training_physical_fes"]
            + result["inference_physical_fes"]
            != result["physical_fes"]
        ):
            raise RuntimeError(f"{algorithm}: physical FES does not reconcile")
        learned_runs.append(result)

    alga = run_json(
        [
            str(binary),
            "--algorithm",
            "alga_attention_declared_reconstruction_v1",
            "--problem",
            "alga_guishan_3d_declared_proxy_v1",
            "--cases",
            str(
                ROOT
                / "shared/contracts/alga_guishan_3d_declared_proxy_cases.json"
            ),
            "--case",
            "ALGA_Guishan3D_SEASON1_tn40",
            "--physical-fes",
            "2430",
            "--seed",
            "20260729",
            "--workers",
            "20",
        ]
    )
    if (
        alga["physical_fes"] != 2430
        or alga["inference_physical_fes"] != 2430
        or alga["generations"] != 100
    ):
        raise RuntimeError("ALGA paper-generation FES mapping differs")

    taae = json.loads(
        (
            ROOT
            / "evidence/development/taae_paper_scale_cpu_feasibility_spark2_20260730.json"
        ).read_text(encoding="utf-8")
    )
    if taae["paper_scale_work"]["training_physical_fes"] != 0:
        raise RuntimeError("TAAE pretraining incorrectly consumes WFLOP FES")

    print(
        "training_fes_audit_pass_with_declared_stop "
        f"sugga_training_fes={expected_sugga} "
        "fqfode_training_fes=39700 "
        "fqfode_independent_training_fes=158800 "
        f"rlpso_runs={len(learned_runs)} alga_native_fes=2430 "
        "taae_training_physical_fes=0 taae_status=profile_specific_stop"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
