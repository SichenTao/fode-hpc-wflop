#!/usr/bin/env python3
"""Audit BDE WS5/WS6 P3 identity, provenance, hashes, and isolation."""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_UNCHANGED = {
    "scripts/validate_bde_source_replay.py": (
        "ff110f3b13d3dfb1b2a6c04bb49c535ac2571ce80f6e0817039c1a43dbcd771b"
    ),
    "shared/contracts/bde_source_replay_execution_contract.json": (
        "8f8d58b95bcfb41c5908b372d6af9ff13e6717f0ae32bab32d8921b91f4bfdd9"
    ),
    "scripts/prepare_bde_source_problem.py": (
        "f3e189816fdb48263b15d976580e3c2aff916cc6ddad060be94f3f9ebdb6a57b"
    ),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    contract = json.loads(
        (
            ROOT / "shared/contracts/bde_ws56_declared_proxy_contract.json"
        ).read_text(encoding="utf-8")
    )
    cases_path = (
        ROOT / "shared/contracts/bde_ws56_declared_proxy_cases.json"
    )
    cases = json.loads(cases_path.read_text(encoding="utf-8"))
    if sha256(cases_path) != contract["hash_contract"][
        "case_manifest_sha256"
    ]:
        raise RuntimeError("BDE WS5/WS6 case manifest hash differs")
    with tempfile.TemporaryDirectory(prefix="bde-ws56-contract-") as temp:
        regenerated = Path(temp) / "cases.json"
        subprocess.run(
            [
                "python3",
                str(ROOT / "scripts/prepare_bde_ws56_declared_proxy.py"),
                "--output",
                str(regenerated),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
        if regenerated.read_bytes() != cases_path.read_bytes():
            raise RuntimeError("BDE WS5/WS6 case generation is not stable")

    identifiers = [case["case_id"] for case in cases["cases"]]
    expected = [
        f"BDEWS{scenario}P3{terrain}tn{turbines}"
        for scenario in (5, 6)
        for terrain in ("STD", "DAE")
        for turbines in (30, 35, 40)
    ]
    if identifiers != expected:
        raise RuntimeError("BDE WS5/WS6 case identity or order differs")
    for case in cases["cases"]:
        if case["case_id"].startswith(("BDE-S-WS", "BDE-D-WS")):
            raise RuntimeError("WS1-WS4 source replay leaked into P3 cases")
        scenario = 5 if case["case_id"].startswith("BDEWS5") else 6
        if len(case["wind_speeds_mps"]) != 8:
            raise RuntimeError("BDE P3 speed cardinality differs")
        if len(case["wind_directions_rad"]) != (12 if scenario == 5 else 16):
            raise RuntimeError("BDE P3 direction cardinality differs")
        probability = sum(
            value
            for row in case["joint_probabilities"]
            for value in row
        )
        if abs(probability - 1.0) > 1.0e-12:
            raise RuntimeError("BDE P3 probability differs from one")
        if "DAE" in case["case_id"]:
            if (
                case["cell_width"] != 250.0
                or len(case["unavailable_cells_1based"]) != 499
            ):
                raise RuntimeError("BDE P3 Daegwallyeong composite differs")
        elif (
            case["cell_width"] != 231.0
            or case["unavailable_cells_1based"]
        ):
            raise RuntimeError("BDE P3 standard farm differs")

    for relative, expected_hash in EXPECTED_UNCHANGED.items():
        if sha256(ROOT / relative) != expected_hash:
            raise RuntimeError(f"preserved BDE replay file changed: {relative}")
    if contract["method_freeze"]["paper_schedule_imax"] != 400:
        raise RuntimeError("paper Imax was not frozen")
    if contract["method_freeze"]["full_10000_fes_generation_count"] != 398:
        raise RuntimeError("exact-FES generation count differs")
    if contract["problem_evidence_tier"] != "P3_DECLARED_PROXY":
        raise RuntimeError("BDE composite evidence tier differs")
    if contract["problem_evidence_subtype"] != "composite_proxy":
        raise RuntimeError("BDE composite evidence subtype differs")
    if contract["method_evidence_tier"] != "M2_CITATION_PREDECESSOR":
        raise RuntimeError("BDE method evidence tier differs")
    if not contract["sensitivity_obligation"][
        "required_before_external_scientific_claim"
    ]:
        raise RuntimeError("BDE sensitivity obligation is absent")

    required_fact_tokens = [
        "Paper title:",
        "DOI:",
        "Paper provides:",
        "Public author code URL:",
        "Public author code revision or archive hash:",
        "Public code/assets provide:",
        "Known missing information:",
        "Reconstruction performed here:",
        "Method evidence tier:",
        "Problem evidence tier:",
        "Method semantic ID:",
        "Problem semantic ID:",
        "Controlling contracts:",
        "Claim boundary:",
        "Last evidence audit date:",
    ]
    for relative in (
        "hpc/bde_ws56_cpp/include/bde_ws56/evolution.hpp",
        "hpc/bde_ws56_cpp/src/evolution.cpp",
        "hpc/bde_ws56_cpp/src/main.cpp",
        "hpc/bde_ws56_cpp/tests/evolution_test.cpp",
    ):
        text = (ROOT / relative).read_text(encoding="utf-8")
        missing = [token for token in required_fact_tokens if token not in text]
        if missing:
            raise RuntimeError(f"{relative}: incomplete fact block {missing}")
    print(
        "bde_ws56_contract_audit_pass "
        "cases=12 mask=499/285 preserved_ws1_ws4_files=3 "
        "shared_bde_slice_checked_by_transition_audit"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
