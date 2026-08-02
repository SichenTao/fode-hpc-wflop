#!/usr/bin/env python3
"""Validate evidence tiers and immutable semantic identities for executable profiles."""

from __future__ import annotations

import csv
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACTS = ROOT / "shared" / "contracts"


def load(name: str) -> dict:
    return json.loads((CONTRACTS / name).read_text(encoding="utf-8"))


def main() -> int:
    schema = load("evidence_authority_schema.json")
    policy = load("reconstruction_admission_policy.json")
    registry = load("executable_profile_evidence.json")
    method_tiers = set(schema["method_evidence_tiers"])
    problem_tiers = set(schema["problem_evidence_tiers"])
    required = set(schema["required_profile_fields"])
    modes = set(schema["required_execution_modes"])
    template = registry["execution_mode_template"]
    if set(template) != modes:
        raise RuntimeError("execution mode template does not cover all modes")
    profiles = registry["profiles"]
    ids: set[str] = set()
    pairs: set[tuple[str, str]] = set()
    for profile in profiles:
        missing = required.difference(profile)
        if missing:
            raise RuntimeError(
                f"{profile.get('profile_id', '<unknown>')}: missing {sorted(missing)}"
            )
        if profile["profile_id"] in ids:
            raise RuntimeError(f"duplicate profile_id {profile['profile_id']}")
        ids.add(profile["profile_id"])
        pair = (profile["algorithm_id"], profile["problem_id"])
        if pair in pairs:
            raise RuntimeError(f"duplicate algorithm/problem pair {pair}")
        pairs.add(pair)
        if profile["method_evidence_tier"] not in method_tiers:
            raise RuntimeError(f"{profile['profile_id']}: invalid method tier")
        if profile["problem_evidence_tier"] not in problem_tiers:
            raise RuntimeError(f"{profile['profile_id']}: invalid problem tier")
        mode_declaration = profile["execution_modes"]
        if mode_declaration == "@execution_mode_template":
            pass
        elif isinstance(mode_declaration, dict):
            if set(mode_declaration) != modes:
                raise RuntimeError(
                    f"{profile['profile_id']}: mode declaration is incomplete"
                )
            for mode, declaration in mode_declaration.items():
                if (
                    not isinstance(declaration, dict)
                    or not isinstance(declaration.get("supported"), bool)
                    or not str(declaration.get("status", "")).strip()
                ):
                    raise RuntimeError(
                        f"{profile['profile_id']}: invalid {mode} mode"
                    )
        else:
            raise RuntimeError(
                f"{profile['profile_id']}: unknown mode declaration"
            )
        if not profile["claim_boundary"].strip():
            raise RuntimeError(f"{profile['profile_id']}: empty claim boundary")

    profile_by_id = {profile["profile_id"]: profile for profile in profiles}
    gga_semantics = load("gga_problem_semantics.json")["canonical_semantics_id"]
    gga_execution = load("gga_execution_contract.json")["problem_semantics_id"]
    if gga_semantics != "geojson_radians_ct_rss_repaired_v1":
        raise RuntimeError("GGA canonical problem semantic differs")
    if gga_execution != gga_semantics:
        raise RuntimeError("GGA execution and problem semantic contracts differ")
    for profile_id in (
        "gga__gga2026_layout_cable",
        "geoga__admitted_gga_problem_asset_proxy",
        "tmoea__nysted_gga_asset_reconstruction",
    ):
        if profile_by_id[profile_id]["problem_semantics_id"] != gga_semantics:
            raise RuntimeError(f"{profile_id}: GGA canonical semantic differs")

    with (CONTRACTS / "algorithm_provenance.tsv").open(
        encoding="utf-8", newline=""
    ) as handle:
        provenance = list(csv.DictReader(handle, delimiter="\t"))
    semantics = {
        (row["algorithm_id"], row["effective_semantics_id"]) for row in provenance
    }
    for profile in profiles:
        identity = (profile["algorithm_id"], profile["method_semantics_id"])
        if identity not in semantics:
            raise RuntimeError(
                f"{profile['profile_id']}: semantic identity differs from baseline"
            )

    campaign_pairs: set[tuple[str, str]] = set()
    common = json.loads(
        (ROOT / "formal/contracts/eighteen_algorithm_cpp_hpc_spark2_v3.json")
        .read_text(encoding="utf-8")
    )
    campaign_pairs.update((algorithm, common["problem_id"]) for algorithm in common["algorithms"])
    bde = json.loads(
        (ROOT / "formal/contracts/bde_source_replay_spark2_v1.json")
        .read_text(encoding="utf-8")
    )
    campaign_pairs.add((bde["algorithm_id"], bde["problem_id"]))
    offshore = json.loads(
        (ROOT / "formal/contracts/offshore_cpp_hpc_spark2_v1.json")
        .read_text(encoding="utf-8")
    )
    campaign_pairs.update(
        (profile["algorithm_id"], profile["problem_id"])
        for profile in offshore["profiles"]
    )
    pbea = json.loads(
        (ROOT / "formal/contracts/pbea_six_algorithm_spark2_v1.json")
        .read_text(encoding="utf-8")
    )
    campaign_pairs.update(
        (algorithm, pbea["problem_id"]) for algorithm in pbea["algorithms"]
    )
    rpso = json.loads(
        (ROOT / "shared/contracts/rpso_source_problem_execution_contract.json")
        .read_text(encoding="utf-8")
    )
    campaign_pairs.add(
        (rpso["admitted_executable_probe"]["algorithm"], rpso["problem_id"])
    )
    missing_pairs = campaign_pairs.difference(pairs)
    if missing_pairs:
        raise RuntimeError(f"formal executable profiles lack evidence: {sorted(missing_pairs)}")
    if policy["canonical_profile_registry"] != (
        "shared/contracts/executable_profile_evidence.json"
    ):
        raise RuntimeError("admission policy points to another profile registry")
    print(
        "evidence_authority_audit_pass "
        f"profiles={len(profiles)} formal_pairs={len(campaign_pairs)} "
        f"method_tiers={len(method_tiers)} problem_tiers={len(problem_tiers)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
