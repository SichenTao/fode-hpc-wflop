#!/usr/bin/env python3
"""Check source-visible GeoGA transition parity and its frozen fixture."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require_tokens(text: str, tokens: list[str], label: str) -> None:
    missing = [token for token in tokens if token not in text]
    if missing:
        raise RuntimeError(f"{label} lacks parity tokens: {missing}")


def main() -> int:
    audit = json.loads(
        (
            ROOT
            / "shared/contracts/geoga_anholt_operator_parity_audit.json"
        ).read_text(encoding="utf-8")
    )
    old_path = ROOT / "hpc/gga_cpp/src/main.cpp"
    new_path = ROOT / "hpc/geoga_cpp/src/evolution.cpp"
    old_text = old_path.read_text(encoding="utf-8")
    new_text = new_path.read_text(encoding="utf-8")
    old_geoga = old_text[
        old_text.index("RunResult optimize_geoga("):
        old_text.index("RunResult optimize_tmoea(")
    ]
    old_geoga_hash = hashlib.sha256(old_geoga.encode("utf-8")).hexdigest()
    if old_geoga_hash != audit["admitted_geoga_function_slice_sha256"]:
        raise RuntimeError(
            "admitted GeoGA function changed after parity freeze"
        )
    if any(item["status"] != "exact" for item in audit["parity_items"]):
        raise RuntimeError("GeoGA parity ledger contains a non-exact transition")

    common_tokens = [
        "seed ^ 0x47454f4741ULL",
        "score_sum",
        "threshold <= cumulative",
        "if (first == second)",
        "second = (second + 1) % kPopulationSize"
            if "kPopulationSize" in new_text
            else "second = (second + 1) % population_size",
        "std::stable_sort",
        "return left.layout < right.layout",
    ]
    require_tokens(new_text, common_tokens, "extension")
    require_tokens(
        new_text,
        [
            "0, 3000",
            "generation, 3001",
            "generation, 3002",
            "3003",
            "3004",
            "3006",
            "generation, 3007",
            "std::unique(repaired.begin(), repaired.end())",
            "draw++",
            "problem.candidates.size()",
            "std::max_element",
        ],
        "extension",
    )
    require_tokens(
        old_geoga,
        [
            "random_layout(problem, rng, 3000, individual)",
            "3001 + static_cast<std::uint64_t>(which)",
            "3003",
            "3004",
            "3006",
            "3007",
            "std::stable_sort",
            "std::max_element",
        ],
        "admitted implementation",
    )

    initialization_end = new_text.index("WorkReceipt work;")
    initialization_prefix = new_text[:initialization_end]
    if "stable_sort(population" in initialization_prefix:
        raise RuntimeError(
            "extension sorts the initial population before roulette"
        )
    if audit["admitted_method_semantic_id"] != "geoga_declared_reconstruction_v1":
        raise RuntimeError("parity audit method identity differs")
    print(
        "geoga_operator_parity_audit_pass "
        f"transitions={len(audit['parity_items'])} "
        "method=geoga_declared_reconstruction_v1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
