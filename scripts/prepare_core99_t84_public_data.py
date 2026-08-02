#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 compact author-data preparation
Paper/DOI: Wake Expansion Continuation: Multi-Modality Reduction in the Wind
Farm Layout Optimization Problem; 10.1002/we.2692
Public source: https://github.com/byuflowlab/thomas2021-wec at commit
8ff27d66079591f25619abeedbfc970d70e2b520. The repository supplies Vestas
V80 power/CT observations, the three paper wind resources, and all 200
starting layouts for the square, round, and Princess Amalia case families.
No LICENSE or COPYING file is present at the pinned revision. This script
therefore extracts factual numeric observations only; it does not copy the
author's Python/Fortran implementation into the production package.
Paper-provided assets: four cases (16/20, 38/12, 38/36, and 60/72), 200
common starts per case, V80 turbine, 2D final spacing, and the paper wind
resources and boundaries.
Missing/conflicts: proprietary SNOPT, Tapenade, pyOptSparse ALPSO runtime,
original environments and random generator states are unavailable. The
PlantEnergy and Jensen3D model lineages are pinned separately in the
controlling contract. The public directional_windrose.txt stores 8 m/s
for case 1 while Section 5.1 says 10 m/s; the paper is primary, so this
fixture preserves the public probabilities and records 10 m/s. The public
layout generators enforce 1D starting separation, matching Section 6 but
not the 2D final optimization constraint. The Amalia source forms its 14
facet boundary from the planned layout; this script independently computes
and stores that convex hull.
Reconstruction: versioned little-endian numeric fixture containing the two
V80 curves, four wind resources, explicit boundary geometry and the exact
200 public starting coordinate arrays for each case.
Problem semantic ID: t84_wec_four_case_author_data_v1
Method semantic IDs: t84_slsqp_control_v1, t84_slsqp_wec_v1,
t84_alpso_control_v1, and t84_alpso_wec_v1
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: compact factual input fixture for an independent academic
reproduction; not redistribution of the unlicensed executable source or an
author environment replay
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess


EXPECTED_REVISION = "8ff27d66079591f25619abeedbfc970d70e2b520"
MAGIC = b"T84DATA1"
ROTOR_DIAMETER_M = 80.0


def numeric_rows(path: Path, columns: int) -> list[tuple[float, ...]]:
    rows: list[tuple[float, ...]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        values = tuple(float(value) for value in line.replace(",", " ").split())
        if len(values) != columns:
            raise ValueError(f"{path}: expected {columns} columns, got {values}")
        rows.append(values)
    return rows


def convex_hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    ordered = sorted(set(points))
    if len(ordered) <= 1:
        return ordered

    def cross(
        origin: tuple[float, float],
        left: tuple[float, float],
        right: tuple[float, float],
    ) -> float:
        return ((left[0] - origin[0]) * (right[1] - origin[1])
                - (left[1] - origin[1]) * (right[0] - origin[0]))

    lower: list[tuple[float, float]] = []
    for point in ordered:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], point) <= 0.0:
            lower.pop()
        lower.append(point)
    upper: list[tuple[float, float]] = []
    for point in reversed(ordered):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], point) <= 0.0:
            upper.pop()
        upper.append(point)
    return lower[:-1] + upper[:-1]


def layout_family(
    inputs: Path,
    directory: str,
    turbines: int,
) -> list[list[tuple[float, float]]]:
    layouts: list[list[tuple[float, float]]] = []
    for index in range(200):
        path = (
            inputs / "layouts" / directory
            / f"nTurbs{turbines}_spacing5_layout_{index}.txt"
        )
        rows = numeric_rows(path, 2)
        if len(rows) != turbines:
            raise ValueError(f"{path}: expected {turbines} turbines")
        if directory == "round_38turbs":
            # The final author driver adds D/2 to the normalized arrays.
            layouts.append([
                (x * ROTOR_DIAMETER_M + 40.0, y * ROTOR_DIAMETER_M + 40.0)
                for x, y in rows
            ])
        else:
            layouts.append([
                (x * ROTOR_DIAMETER_M, y * ROTOR_DIAMETER_M)
                for x, y in rows
            ])
    return layouts


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--allow-revision-mismatch", action="store_true")
    args = parser.parse_args()

    source = Path(args.source_root).resolve()
    observed_revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if (observed_revision != EXPECTED_REVISION
            and not args.allow_revision_mismatch):
        raise SystemExit(
            f"expected source revision {EXPECTED_REVISION}, "
            f"observed {observed_revision}"
        )

    inputs = source / "project-code" / "input_files"
    power = numeric_rows(
        inputs / "niayifar_vestas_v80_power_curve_observed.txt", 2
    )
    thrust = numeric_rows(
        inputs / "mfg_ct_vestas_v80_niayifar2016.txt", 2
    )
    directional = numeric_rows(inputs / "directional_windrose.txt", 3)
    # Source order is direction, speed, frequency. Paper Section 5.1 has
    # authority over the conflicting source speed column for case 1.
    case1_wind = [(direction, 10.0, frequency)
                  for direction, _speed, frequency in directional]
    case2_wind = numeric_rows(inputs / "nantucket_wind_rose_for_LES.txt", 3)
    case3_wind = numeric_rows(inputs / "nantucket_windrose_ave_speeds.txt", 3)
    case4_wind = numeric_rows(
        inputs / "windrose_amalia_directionally_averaged_speeds.txt", 3
    )
    if [len(case1_wind), len(case2_wind), len(case3_wind), len(case4_wind)] \
            != [20, 12, 36, 72]:
        raise SystemExit("unexpected T84 wind-state count")

    square = layout_family(inputs, "grid_16turbs", 16)
    round_layouts = layout_family(inputs, "round_38turbs", 38)
    amalia = layout_family(inputs, "amalia_60turbs", 60)
    amalia_hull = convex_hull(amalia[0])
    if len(amalia_hull) != 14:
        raise SystemExit(
            f"expected the paper's 14-facet Amalia hull, got {len(amalia_hull)}"
        )

    round_radius = 0.5 * (
        ROTOR_DIAMETER_M * 4000.0 / 126.4 - ROTOR_DIAMETER_M
    )
    round_centre = round_radius + 0.5 * ROTOR_DIAMETER_M
    cases = [
        (1, case1_wind, square, 0, (0.0, 1280.0, 0.0, 1280.0)),
        (2, case2_wind, round_layouts, 1,
         (round_centre, round_centre, round_radius)),
        (3, case3_wind, round_layouts, 1,
         (round_centre, round_centre, round_radius)),
        (4, case4_wind, amalia, 2, tuple(amalia_hull)),
    ]

    payload = bytearray(MAGIC)
    payload.extend(struct.pack("<II", len(power), len(thrust)))
    for row in power:
        payload.extend(struct.pack("<2d", *row))
    for row in thrust:
        payload.extend(struct.pack("<2d", *row))
    payload.extend(struct.pack("<I", len(cases)))
    for case_id, wind, layouts, boundary_kind, boundary in cases:
        payload.extend(struct.pack(
            "<IIIII", case_id, len(layouts[0]), len(wind),
            boundary_kind, len(layouts)
        ))
        for row in wind:
            payload.extend(struct.pack("<3d", *row))
        if boundary_kind == 0:
            payload.extend(struct.pack("<4d", *boundary))
        elif boundary_kind == 1:
            payload.extend(struct.pack("<3d", *boundary))
        else:
            payload.extend(struct.pack("<I", len(boundary)))
            for point in boundary:
                payload.extend(struct.pack("<2d", *point))
        for layout in layouts:
            for point in layout:
                payload.extend(struct.pack("<2d", *point))

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    masses = [sum(row[2] for row in wind) for _, wind, *_ in cases]
    print(
        f"revision={observed_revision} bytes={len(payload)} "
        f"amalia_facets={len(amalia_hull)} wind_probability_masses={masses} "
        f"sha256={digest}"
    )


if __name__ == "__main__":
    main()
