#!/usr/bin/env python3
"""End-to-end artifact replay tests against the three real optimizers."""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
    )


def load(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def train_artifacts(
    trainer: Path,
    directory: Path,
) -> dict[tuple[str, str], Path]:
    artifacts: dict[tuple[str, str], Path] = {}
    for method in ("taae", "alga", "rlpso"):
        for label, seed in (("a", 111), ("b", 222)):
            artifact = directory / f"{method}-{label}.pt"
            run(
                [
                    str(trainer),
                    "--method",
                    method,
                    "--backend",
                    "cpu",
                    "--artifact-out",
                    str(artifact),
                    "--seed",
                    str(seed),
                    "--torch-intraop-threads",
                    "1",
                    "--torch-interop-threads",
                    "1",
                ]
            )
            require(artifact.is_file(), f"{method}: artifact absent")
            artifacts[(method, label)] = artifact
    return artifacts


def run_taae(
    binary: Path,
    cases: Path,
    artifact: Path,
) -> dict[str, Any]:
    completed = run(
        [
            str(binary),
            "--cases",
            str(cases),
            "--case",
            "TAAE_Proxy_NC1_Budget600k_tn15",
            "--physical-fes",
            "103",
            "--seed",
            "777",
            "--workers",
            "1",
            "--learning-artifact",
            str(artifact),
            "--torch-intraop-threads",
            "1",
            "--torch-interop-threads",
            "1",
        ]
    )
    return json.loads(completed.stdout)


def run_wflop(
    binary: Path,
    directory: Path,
    *,
    method: str,
    artifact: Path,
    cases: Path,
) -> dict[str, Any]:
    if method == "alga":
        algorithm = "alga_attention_declared_reconstruction_v1"
        problem = "alga_guishan_3d_declared_proxy_v1"
        case = "ALGA_Guishan3D_IDEAL1_tn20"
        budget = "35"
    else:
        algorithm = "rlpso_paper_corrected_training_reconstruction_v1"
        problem = "rpso2024_source_problem_ws1_ws4"
        case = "RPSO-WS1-tn30"
        budget = "150"
    run_index = len(list(directory.glob(f"{method}-run-*.json")))
    output = directory / f"{method}-run-{run_index}.json"
    run(
        [
            str(binary),
            "--algorithm",
            algorithm,
            "--problem",
            problem,
            "--cases",
            str(cases),
            "--case",
            case,
            "--physical-fes",
            budget,
            "--seed",
            "777",
            "--workers",
            "1",
            "--training-artifact",
            str(artifact),
            "--torch-intraop-threads",
            "1",
            "--torch-interop-threads",
            "1",
            "--output",
            str(output),
        ]
    )
    return load(output)


def audit_triplet(
    method: str,
    first: dict[str, Any],
    replay: dict[str, Any],
    alternative: dict[str, Any],
    *,
    budget: int,
    state_key: str,
    deterministic_keys: tuple[str, ...],
) -> None:
    for label, result in (
        ("first", first),
        ("replay", replay),
        ("alternative", alternative),
    ):
        require(
            result.get("learning_artifact_consumed") is True,
            f"{method}/{label}: artifact was not consumed",
        )
        require(
            result.get("thread_topology") == {
                "outer_workers": 1,
                "torch_intraop_threads": 1,
                "torch_interop_threads": 1,
            },
            f"{method}/{label}: nested thread topology is uncontrolled",
        )
        require(
            result.get("physical_fes") == budget,
            f"{method}/{label}: inexact FES",
        )
        require(
            "partial" in result.get("terminal_partial_work", ""),
            f"{method}/{label}: terminal partial work unreported",
        )
        require(
            result.get("learning_decision_hash", "").startswith("fnv1a64:"),
            f"{method}/{label}: decision hash absent",
        )
        require(
            result.get(state_key, "").startswith("fnv1a64:"),
            f"{method}/{label}: learned state hash absent",
        )
    for key in deterministic_keys:
        require(
            first.get(key) == replay.get(key),
            f"{method}: same artifact/seed replay changed {key}",
        )
    require(
        first["learning_decision_hash"]
        != alternative["learning_decision_hash"],
        f"{method}: different artifact did not change learned decision",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--trainer", type=Path, required=True)
    parser.add_argument("--taae-binary", type=Path, required=True)
    parser.add_argument("--wflop-binary", type=Path, required=True)
    parser.add_argument("--taae-cases", type=Path, required=True)
    parser.add_argument("--alga-cases", type=Path, required=True)
    parser.add_argument("--rlpso-cases", type=Path, required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(
        prefix="plan004-full-optimizer-"
    ) as temporary:
        directory = Path(temporary)
        artifacts = train_artifacts(arguments.trainer, directory)

        taae_first = run_taae(
            arguments.taae_binary,
            arguments.taae_cases,
            artifacts[("taae", "a")],
        )
        taae_replay = run_taae(
            arguments.taae_binary,
            arguments.taae_cases,
            artifacts[("taae", "a")],
        )
        taae_alternative = run_taae(
            arguments.taae_binary,
            arguments.taae_cases,
            artifacts[("taae", "b")],
        )
        audit_triplet(
            "taae",
            taae_first,
            taae_replay,
            taae_alternative,
            budget=103,
            state_key="model_hash",
            deterministic_keys=(
                "physical_fes",
                "learning_decision_hash",
                "model_hash",
                "population_layout_hash",
                "front_hash",
                "proposal_work",
                "terminal_partial_work",
            ),
        )

        alga_first = run_wflop(
            arguments.wflop_binary,
            directory,
            method="alga",
            artifact=artifacts[("alga", "a")],
            cases=arguments.alga_cases,
        )
        alga_replay = run_wflop(
            arguments.wflop_binary,
            directory,
            method="alga",
            artifact=artifacts[("alga", "a")],
            cases=arguments.alga_cases,
        )
        alga_alternative = run_wflop(
            arguments.wflop_binary,
            directory,
            method="alga",
            artifact=artifacts[("alga", "b")],
            cases=arguments.alga_cases,
        )
        audit_triplet(
            "alga",
            alga_first,
            alga_replay,
            alga_alternative,
            budget=35,
            state_key="learned_state_hash",
            deterministic_keys=(
                "physical_fes",
                "learning_decision_hash",
                "learned_state_hash",
                "best_layout_1based",
                "best_expected_power_kw",
                "terminal_partial_work",
            ),
        )

        rlpso_first = run_wflop(
            arguments.wflop_binary,
            directory,
            method="rlpso",
            artifact=artifacts[("rlpso", "a")],
            cases=arguments.rlpso_cases,
        )
        rlpso_replay = run_wflop(
            arguments.wflop_binary,
            directory,
            method="rlpso",
            artifact=artifacts[("rlpso", "a")],
            cases=arguments.rlpso_cases,
        )
        rlpso_alternative = run_wflop(
            arguments.wflop_binary,
            directory,
            method="rlpso",
            artifact=artifacts[("rlpso", "b")],
            cases=arguments.rlpso_cases,
        )
        audit_triplet(
            "rlpso",
            rlpso_first,
            rlpso_replay,
            rlpso_alternative,
            budget=150,
            state_key="learned_state_hash",
            deterministic_keys=(
                "physical_fes",
                "learning_decision_hash",
                "learned_state_hash",
                "best_layout_1based",
                "best_expected_power_kw",
                "policy_interactions",
                "policy_updates",
                "terminal_partial_work",
            ),
        )

    print(json.dumps({
        "status": "pass",
        "methods": 3,
        "same_artifact_seed_replay": "exact",
        "different_artifact_changes_learned_decision": True,
        "exact_physical_fes": {
            "taae": 103,
            "alga": 35,
            "rlpso": 150,
        },
        "terminal_partial_work": True,
        "native_evaluator": True,
        "random_event_ownership": (
            "optimizer-owned seeded events replay exactly; artifact "
            "identity is varied independently"
        ),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
