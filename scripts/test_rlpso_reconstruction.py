#!/usr/bin/env python3
"""Smoke and replay checks for the declared RPSO-derived compact proxy."""

from __future__ import annotations

import argparse
import json
import subprocess


def run(binary: str, cases: str, algorithm: str, workers: int) -> dict:
    completed = subprocess.run(
        [
            binary,
            "--algorithm", algorithm,
            "--problem", "rpso2024_source_problem_ws1_ws4",
            "--cases", cases,
            "--case", "RPSO-WS1-tn30",
            "--physical-fes", "480",
            "--workers", str(workers),
            "--seed", "2026072901",
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--cases", required=True)
    args = parser.parse_args()
    profiles = [
        "rlpso_compact_policy_declared_reconstruction_v1",
        "rlpso_paper_corrected_training_reconstruction_v1",
        "rlpso_literal_official_source_replay_v1",
    ]
    for profile in profiles:
        first = run(args.binary, args.cases, profile, 20)
        second = run(args.binary, args.cases, profile, 20)
        scalar = run(args.binary, args.cases, profile, 1)
        if first["physical_fes"] != 480:
            raise RuntimeError(f"{profile}: exact FES failed")
        if first["training_physical_fes"] <= 0:
            raise RuntimeError(f"{profile}: training FES was not exposed")
        if (
            first["training_physical_fes"] + first["inference_physical_fes"]
            != first["physical_fes"]
        ):
            raise RuntimeError(f"{profile}: FES ledger does not reconcile")
        if first["best_layout_1based"] != second["best_layout_1based"]:
            raise RuntimeError(f"{profile}: deterministic layout replay failed")
        if first["best_expected_power_kw"] != second["best_expected_power_kw"]:
            raise RuntimeError(f"{profile}: deterministic objective replay failed")
        if (
            first["best_layout_1based"] != scalar["best_layout_1based"]
            or first["best_expected_power_kw"]
            != scalar["best_expected_power_kw"]
        ):
            raise RuntimeError(f"{profile}: 1/20 worker semantics differ")
        if profile in {
            "rlpso_paper_corrected_training_reconstruction_v1",
            "rlpso_literal_official_source_replay_v1",
        }:
            if first["training_physical_fes"] != 430:
                raise RuntimeError(
                    f"{profile}: source training loop was not FES-truncated"
                )
            if first["inference_physical_fes"] != 50:
                raise RuntimeError(
                    f"{profile}: initialization inference ledger differs"
                )
            if first.get("policy_interactions") != 430:
                raise RuntimeError(
                    f"{profile}: policy interaction receipt differs"
                )
            expected_updates = (
                0
                if profile == "rlpso_literal_official_source_replay_v1"
                else 1
            )
            if first.get("policy_updates") != expected_updates:
                raise RuntimeError(
                    f"{profile}: PPO update lifecycle differs"
                )
            if first["timing_seconds"].get("policy_training", 0.0) <= 0.0:
                raise RuntimeError(
                    f"{profile}: policy training time was not exposed"
                )
            if (
                expected_updates > 0
                and first["timing_seconds"].get("policy_update", 0.0) <= 0.0
            ):
                raise RuntimeError(
                    f"{profile}: PPO update time was not exposed"
                )
            learned_hash = first.get("learned_state_hash", "")
            if not learned_hash.startswith("fnv1a64:"):
                raise RuntimeError(
                    f"{profile}: learned-state hash was not exposed"
                )
            if learned_hash != second.get("learned_state_hash"):
                raise RuntimeError(
                    f"{profile}: learned-state replay hash differs"
                )
            if learned_hash != scalar.get("learned_state_hash"):
                raise RuntimeError(
                    f"{profile}: 1/20 worker learned-state hash differs"
                )
            expected_hash = {
                "rlpso_paper_corrected_training_reconstruction_v1":
                    "fnv1a64:ff5a9a02bef4ccd5",
                "rlpso_literal_official_source_replay_v1":
                    "fnv1a64:0c6ecd6f4fc26aaf",
            }[profile]
            if learned_hash != expected_hash:
                raise RuntimeError(
                    f"{profile}: frozen seed/case/FES policy hash differs"
                )
    blocked = subprocess.run(
        [
            args.binary,
            "--algorithm", "rlpso",
            "--problem", "rpso2024_source_problem_ws1_ws4",
            "--cases", args.cases,
            "--case", "RPSO-WS1-tn30",
            "--physical-fes", "10",
            "--workers", "1",
        ],
        text=True,
        capture_output=True,
    )
    if blocked.returncode == 0 or "intentionally blocked at R2" not in blocked.stderr:
        raise RuntimeError("original rlpso identifier was not guarded")
    print("rlpso_reconstruction_test_pass profiles=3 workers=20 fes=480")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
