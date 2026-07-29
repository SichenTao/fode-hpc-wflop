#!/usr/bin/env python3
"""Generate the isolated BDE WS5/WS6 P3 composite case manifest.

The exact WS5/WS6 arrays are absent from the paper and official archive.
The declared wind construction below is deterministic and normalized. The
Daegwallyeong topology is a manual row-major transcription of the numbered
red/white grid in paper Fig.5, while its metric scale is the paper's 250 m.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = (
    ROOT / "shared/contracts/bde_ws56_declared_proxy_cases.json"
)

PAPER_SHA256 = (
    "5029841587d4199ab846ddff971a178d52af7ccec0e758a22f5be2eff6626e85"
)
ARCHIVE_SHA256 = (
    "f4a317d4d727a9d452f76376373e2c8ad5546e35ff19530eda0ba328682dd140"
)
MASK_SOURCE_SHA256 = (
    "91fc345b6910a2df011630ac7cd2d24886879ec9edf977e4e4423af95ab3d7b7"
)

SPEEDS_MPS = [4.0, 6.0, 8.0, 10.0, 12.0, 14.0, 15.0, 16.0]
SPEED_WEIGHTS = [0.05, 0.10, 0.18, 0.24, 0.20, 0.13, 0.07, 0.03]
WS5_DIRECTION_WEIGHTS = [
    0.04,
    0.07,
    0.15,
    0.09,
    0.04,
    0.03,
    0.02,
    0.03,
    0.12,
    0.14,
    0.17,
    0.10,
]
WS6_DIRECTION_WEIGHTS = [
    0.06,
    0.08,
    0.07,
    0.09,
    0.11,
    0.08,
    0.04,
    0.03,
    0.02,
    0.02,
    0.03,
    0.04,
    0.08,
    0.10,
    0.09,
    0.06,
]

# Manual paper-Fig.5 digitization procedure: read the visibly numbered 28x28
# grid in row-major order; record every white (unavailable) cell; compress only
# consecutive white labels into inclusive ranges. A second pass checked all
# row boundaries. The resulting 499-cell list independently matches the hash-
# frozen author-source NA_type=13 topology. The no-license archive was used as
# a cross-check only; no archive file is copied or required at runtime.
FIG5_DIGITIZED_UNAVAILABLE_RANGES = [
    (1, 10), (16, 39), (46, 68), (74, 97), (103, 126), (132, 154),
    (161, 182), (191, 209), (220, 236), (249, 264), (270, 271),
    (280, 284), (288, 292), (297, 300), (308, 312), (317, 320),
    (325, 329), (336, 340), (346, 349), (353, 358), (364, 368),
    (374, 386), (392, 397), (402, 415), (420, 425), (430, 439),
    (448, 453), (458, 467), (476, 481), (487, 495), (504, 510),
    (516, 523), (532, 537), (545, 551), (560, 565), (573, 580),
    (588, 593), (601, 608), (616, 624), (629, 636), (643, 652),
    (657, 663), (670, 680), (686, 690), (698, 708), (714, 719),
    (726, 737), (742, 748), (754, 766), (769, 777), (782, 784),
]


def unavailable_cells() -> list[int]:
    return [
        cell
        for first, last in FIG5_DIGITIZED_UNAVAILABLE_RANGES
        for cell in range(first, last + 1)
    ]


def joint_probabilities(direction_weights: list[float]) -> list[list[float]]:
    rows = [
        [direction * speed for speed in SPEED_WEIGHTS]
        for direction in direction_weights
    ]
    total = math.fsum(value for row in rows for value in row)
    if abs(total - 1.0) > 1.0e-15:
        raise RuntimeError(f"joint distribution is not normalized: {total}")
    return rows


def case(
    scenario: int,
    terrain: str,
    turbines: int,
    direction_weights: list[float],
) -> dict:
    directions = len(direction_weights)
    is_standard = terrain == "STD"
    payload = {
        "case_id": f"BDEWS{scenario}P3{terrain}tn{turbines}",
        "scenario_problem_semantic_id": (
            f"bde2025_ws{scenario}_paper250_declared_proxy_v1"
        ),
        "rows": 21 if is_standard else 28,
        "cols": 21 if is_standard else 28,
        "turbine_count": turbines,
        "cell_width": 231.0 if is_standard else 250.0,
        "wind_directions_rad": [
            2.0 * math.pi * index / directions
            for index in range(directions)
        ],
        "wind_speeds_mps": SPEEDS_MPS,
        "joint_probabilities": joint_probabilities(direction_weights),
        "unavailable_cells_1based": [] if is_standard else unavailable_cells(),
    }
    canonical = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode()
    payload["case_sha256"] = hashlib.sha256(canonical).hexdigest()
    return payload


def document() -> dict:
    cases = []
    for scenario, direction_weights in (
        (5, WS5_DIRECTION_WEIGHTS),
        (6, WS6_DIRECTION_WEIGHTS),
    ):
        for terrain in ("STD", "DAE"):
            for turbines in (30, 35, 40):
                cases.append(
                    case(scenario, terrain, turbines, direction_weights)
                )
    return {
        "schema_version": 1,
        "collection_id": "bde2025_ws56_declared_proxy_isolated_v1",
        "problem_semantic_ids": [
            "bde2025_ws5_paper250_declared_proxy_v1",
            "bde2025_ws6_paper250_declared_proxy_v1",
        ],
        "evidence_tier": "P3_DECLARED_PROXY",
        "evidence_subtype": "composite_proxy",
        "paper_sha256": PAPER_SHA256,
        "official_archive_sha256": ARCHIVE_SHA256,
        "official_mask_source_sha256": MASK_SOURCE_SHA256,
        "authority_composition": {
            "paper": (
                "WS5 8x12 uneven, WS6 8x16 uneven, standard 21x21 at "
                "231 m, Daegwallyeong 28x28 at 250 m, N=30/35/40"
            ),
            "paper_figure_digitization": (
                "manual two-pass row-major white-cell transcription from "
                "numbered Fig.5; 499 unavailable and 285 available cells"
            ),
            "official_source_cross_check": (
                "hash-frozen NA_type=13 topology independently matches the "
                "Fig.5 transcription; source spacing remains 231 m"
            ),
            "declared_p3": (
                "speed values, speed marginals, angular grids, direction "
                "marginals, and independent joint-product construction"
            ),
        },
        "wind_construction": {
            "speed_values_mps": SPEEDS_MPS,
            "speed_marginal": SPEED_WEIGHTS,
            "ws5_direction_rule": "12 equally spaced angles from 0 degrees",
            "ws5_direction_marginal": WS5_DIRECTION_WEIGHTS,
            "ws6_direction_rule": "16 equally spaced angles from 0 degrees",
            "ws6_direction_marginal": WS6_DIRECTION_WEIGHTS,
            "joint_rule": "direction_marginal times speed_marginal",
            "normalization_rule": "literal decimal marginals each sum to one",
            "figure_use": (
                "Fig.4 supports cardinalities, unevenness, and a roughly "
                "4--16 m/s band only; no plotted value is treated as exact"
            ),
        },
        "alternatives_rejected": [
            {
                "alternative": "reuse common FODE WS5/WS6",
                "reason": (
                    "those cases are 13 speeds x 5/6 directions at 231 m "
                    "and have a different semantic identity"
                ),
            },
            {
                "alternative": "digitize Fig.4 as exact arrays",
                "reason": "the plot lacks tabulated joint probabilities",
            },
            {
                "alternative": "call the official 231 m mask replay 250 m",
                "reason": (
                    "Fig.5 topology is manually digitized and source-checked; "
                    "the authority mix still creates a composite proxy"
                ),
            },
        ],
        "sensitivity_obligation": {
            "required_before_external_scientific_claim": True,
            "factors": [
                "Fig.5 manual cell-state transcription uncertainty",
                "speed support and marginal",
                "direction rotation and marginal",
                "joint dependence versus product construction",
                "231 m versus 250 m Daegwallyeong spacing",
            ],
            "ranking_policy": (
                "results are development evidence only and must never be "
                "pooled or ranked with WS1--WS4 source replay"
            ),
        },
        "cases": cases,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    arguments = parser.parse_args()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(
        json.dumps(document(), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
