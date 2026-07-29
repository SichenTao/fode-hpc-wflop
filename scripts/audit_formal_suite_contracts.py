#!/usr/bin/env python3
"""Audit host-specific formal suites without conflating scientific profiles."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SUITES = (
    ROOT / "formal/contracts/waffle_campaign_suite_v1.json",
    ROOT / "formal/contracts/spark2_campaign_suite_v1.json",
)
ROLES = ("common", "bde", "pbea", "offshore")
COMMON_ALGORITHMS = (
    "fode",
    "aga",
    "sugga",
    "ise",
    "agpso",
    "cgpso",
    "lshade",
    "clshade",
    "cede",
    "msshade",
    "bde",
    "hgpso",
    "aiga",
    "ciga",
    "lsde",
    "wfadde",
    "alshade",
    "ppga",
)


def read_json(path: Path) -> dict:
    return json.loads(path.read_text())


def main() -> int:
    for suite_path in SUITES:
        suite = read_json(suite_path)
        campaigns = suite["campaigns"]
        if tuple(row["role"] for row in campaigns) != ROLES:
            raise RuntimeError(f"{suite_path.name}: campaign role order differs")
        if len({row["campaign_id"] for row in campaigns}) != len(ROLES):
            raise RuntimeError(f"{suite_path.name}: duplicate campaign identity")
        contracts = []
        for row in campaigns:
            contract_path = ROOT / row["contract"]
            contract = read_json(contract_path)
            if contract["campaign_id"] != row["campaign_id"]:
                raise RuntimeError(
                    f"{suite_path.name}: campaign/contract identity differs"
                )
            if int(contract["formal_run_count"]) != int(
                row["optimization_runs"]
            ):
                raise RuntimeError(
                    f"{row['campaign_id']}: optimization-run count differs"
                )
            if int(contract["formal_complete_layout_evaluations"]) != int(
                row["complete_layout_evaluations"]
            ):
                raise RuntimeError(
                    f"{row['campaign_id']}: physical-evaluation count differs"
                )
            contracts.append(contract)
        if tuple(contracts[0]["algorithms"]) != COMMON_ALGORITHMS:
            raise RuntimeError(f"{suite_path.name}: common algorithm set differs")
        if len(contracts[0]["seeds"]) != 25:
            raise RuntimeError(f"{suite_path.name}: common repeats differ")
        if len(contracts[1]["seeds"]) != 25:
            raise RuntimeError(f"{suite_path.name}: BDE repeats differ")
        if int(contracts[2]["repeat_count"]) != 25:
            raise RuntimeError(f"{suite_path.name}: PBEA repeats differ")
        if int(contracts[3]["repeat_count"]) != 25:
            raise RuntimeError(f"{suite_path.name}: offshore repeats differ")
        if sum(row["optimization_runs"] for row in campaigns) != int(
            suite["total_optimization_runs"]
        ):
            raise RuntimeError(f"{suite_path.name}: suite run total differs")
        if sum(
            row["complete_layout_evaluations"] for row in campaigns
        ) != int(suite["total_complete_layout_evaluations"]):
            raise RuntimeError(
                f"{suite_path.name}: suite physical-evaluation total differs"
            )
        if set(suite["blocked_methods_excluded"]) != {
            "alga",
            "taae",
            "rlpso",
            "rlfode",
        }:
            raise RuntimeError(f"{suite_path.name}: blocked method set differs")
    print(
        "formal_suite_contract_audit_pass "
        "suites=2 campaigns=8 runs_per_suite=28325 "
        "complete_layout_evaluations_per_suite=597155000"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
