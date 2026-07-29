#!/usr/bin/env python3
"""Audit deterministic learned-artifact reconstruction and declared stops."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/full"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command, check=True, capture_output=True, text=True
    )


def check_fqfode(temporary: Path) -> int:
    trainer = BUILD / "hpc/wflop_cpp/rlfode_train_qtable"
    cases = ROOT / "shared/contracts/benchmark_cases.json"
    contract = json.loads(
        (
            ROOT
            / "shared/contracts/fqfode_seeded_training_reconstruction_contract.json"
        ).read_text(encoding="utf-8")
    )
    profiles = [
        (
            "baseline",
            contract["learned_method_receipt"],
            ROOT
            / "shared/models/fqfode_seeded/fqfode_shared_ws2tn50_v1.qtable.tsv",
        ),
        (
            "independent-stage-pretraining",
            contract["independent_stage_sensitivity_receipt"],
            ROOT
            / "shared/models/fqfode_seeded/fqfode_independent_stage_ws2tn50_v1.qtable.tsv",
        ),
    ]
    for profile, expected, frozen in profiles:
        fresh = temporary / f"fqfode-{profile}.tsv"
        receipt = json.loads(
            run(
                [
                    str(trainer),
                    "--cases",
                    str(cases),
                    "--case",
                    "WS2tn50",
                    "--output",
                    str(fresh),
                    "--workers",
                    "20",
                    "--sensitivity-profile",
                    profile,
                ]
            ).stdout
        )
        expected_hash = expected["checkpoint_hash"]
        if receipt["qtable_hash"] != expected_hash:
            raise RuntimeError(f"FQFODE {profile}: table hash differs")
        if sha256(fresh) != expected["artifact_sha256"]:
            raise RuntimeError(f"FQFODE {profile}: artifact SHA-256 differs")
        if fresh.read_bytes() != frozen.read_bytes():
            raise RuntimeError(f"FQFODE {profile}: frozen artifact differs")
    return len(profiles)


def check_sugga(temporary: Path) -> int:
    root = ROOT / "shared/models/sugga_native"
    receipt = json.loads(
        (root / "training_receipt.json").read_text(encoding="utf-8")
    )
    expected_case_count = sum(
        int(contract["case_count"]) for contract in receipt["contracts"]
    )
    if receipt["case_count"] != expected_case_count:
        raise RuntimeError("SUGGA native model count differs from contracts")
    by_pair = {
        (record["problem_id"], record["case_id"]): record
        for record in receipt["models"]
    }
    if len(by_pair) != receipt["case_count"]:
        raise RuntimeError("SUGGA problem/case model identities collide")
    for pair, record in by_pair.items():
        model = root / pair[0] / f"{pair[1]}.svr.tsv"
        if sha256(model) != record["model_sha256"]:
            raise RuntimeError(f"SUGGA model hash differs: {pair}")

    # Recreate one full 10000-layout model from each distinct native problem.
    # The complete model corpus has already been frozen; one representative
    # per distinct problem proves the generator and SVR path remains
    # executable without rerunning every case.
    checked = 0
    for index, contract_record in enumerate(receipt["contracts"]):
        source = ROOT / contract_record["path"]
        payload = json.loads(source.read_text(encoding="utf-8"))
        one = dict(payload)
        one["cases"] = [payload["cases"][0]]
        one["case_count"] = 1
        selected_contract = temporary / f"sugga-contract-{index}.json"
        selected_contract.write_text(
            json.dumps(one, indent=2) + "\n", encoding="utf-8"
        )
        output = temporary / f"sugga-models-{index}"
        run(
            [
                "python3",
                str(ROOT / "scripts/train_sugga_native_models.py"),
                "--generator",
                str(BUILD / "hpc/wflop_cpp/sugga_mc_response"),
                "--cases",
                str(selected_contract),
                "--output-dir",
                str(output),
                "--samples",
                "10000",
                "--workers",
                "20",
            ]
        )
        case_id = payload["cases"][0]["case_id"]
        problem_id = str(
            payload.get("problem_id") or payload["problem_semantic_id"]
        )
        fresh = output / problem_id / f"{case_id}.svr.tsv"
        frozen = root / problem_id / f"{case_id}.svr.tsv"
        if fresh.read_bytes() != frozen.read_bytes():
            raise RuntimeError(
                f"SUGGA representative artifact differs: "
                f"{problem_id}/{case_id}"
            )
        checked += 1
    return checked


def check_bounded_learned_paths() -> tuple[int, int]:
    run(
        [
            "python3",
            str(ROOT / "scripts/test_rlpso_reconstruction.py"),
            "--binary",
            str(BUILD / "hpc/wflop_cpp/wflop_cpp_hpc"),
            "--cases",
            str(
                ROOT
                / ".source-cache/generated/rpso_source_problem/benchmark_cases.json"
            ),
        ]
    )
    run(
        [
            "python3",
            str(ROOT / "scripts/test_alga_attention_reconstruction.py"),
            "--binary",
            str(BUILD / "hpc/wflop_cpp/wflop_cpp_hpc"),
            "--common-cases",
            str(ROOT / "shared/contracts/benchmark_cases.json"),
            "--transfer-cases",
            str(
                ROOT / "shared/contracts/alga_guishan_planar_transfer_cases.json"
            ),
            "--native-3d-cases",
            str(
                ROOT
                / "shared/contracts/alga_guishan_3d_declared_proxy_cases.json"
            ),
        ]
    )
    return 3, 1


def check_taae_stop() -> str:
    receipt = json.loads(
        (
            ROOT
            / "evidence/development/taae_paper_scale_cpu_feasibility_spark2_20260730.json"
        ).read_text(encoding="utf-8")
    )
    if (
        receipt["status"] != "profile_specific_stop"
        or receipt["bounded_fixed_work_probe"]["byte_reproducibility"]
        != "pass"
        or receipt["paper_scale_work"]["optimizer_steps"] != 781500
    ):
        raise RuntimeError("TAAE paper-scale STOP receipt is incomplete")
    return receipt["status"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true")
    arguments = parser.parse_args()
    if not arguments.all:
        parser.error("--all is required")
    with tempfile.TemporaryDirectory(
        prefix="wflop-training-reproducibility-"
    ) as temporary:
        root = Path(temporary)
        fqfode = check_fqfode(root)
        sugga = check_sugga(root)
        rlpso, alga = check_bounded_learned_paths()
    taae = check_taae_stop()
    print(
        "training_reproducibility_audit_pass_with_declared_stop "
        f"fqfode_artifacts={fqfode} sugga_problem_replays={sugga} "
        f"rlpso_profiles={rlpso} alga_profiles={alga} "
        f"taae={taae}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
