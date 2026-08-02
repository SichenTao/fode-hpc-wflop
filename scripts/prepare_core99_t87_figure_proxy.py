#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T87 Fig. 2 turbine-curve, Fig. 5 wind-rose and Fig. 9
feasible-site proxy extractor
Paper/DOI: Wind Farm Layout Optimization in Complex Terrain Based on CFD and
IGA-PSO; 10.1016/j.energy.2023.129745
Paper-provided assets: vector/raster figures, WD156-3300 normalized power and
thrust-coefficient curves, 20D by 92D site, 0.5D grid inherited from DOI
10.1016/j.energy.2022.123970, AEH threshold 2000 h, displayed AEH range
500-2500 h, 16 direction/speed-bin wind rose, and 6.9% feasible-site statement.
Public source/data: the target paper explicitly states that the authors do
not have permission to share data; no author code archive was located.
Reconstruction: digitize the Fig. 2 circular/triangular markers; extract 522
red grid points from the hashed Fig. 9 raster
(522/7585=6.882%, rounding to the printed 6.9%). Rank candidate AEH from
unobscured neighboring color pixels and anchor the rank to the printed
2000-2500 h feasible interval. Digitize the four visible Fig. 5 speed stacks
and normalize their rounded radial total to probability one.
Claim boundary: versioned image-derived Qianjiang proxy, not author CFD,
terrain, mast time-series, or exact numerical field
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
from PIL import Image


FIGURE5_SHA256 = (
    "3a09bd6436e6885aaf607a0250ebaed2c82286bccabb1b3182fc1b0eb2c13576"
)
FIGURE2_SHA256 = (
    "8f449ccf624e3838480b43d66e44dd194d21dbf72dceff548327daf5141f4564"
)
FIGURE9_SHA256 = (
    "f0fb82fc53364ae5dc5a3464503297daacead45e8079c2e44445aeea964855aa"
)
X_ORIGIN_PX = 269.3
X_STEP_PX = 11.535
Y_ORIGIN_PX = 102.3
Y_STEP_PX = 11.495
GRID_NX = 41
GRID_NY = 185
COLORBAR_X_PX = 820
COLORBAR_TOP_PX = 102
COLORBAR_BOTTOM_PX = 2218

# Cumulative radial pixel boundaries of the red/orange/yellow/green stacks,
# in N, NNE, ..., NNW order. These are the central-ray vector/raster
# digitization with explicit small-bar corrections where compass lines obscure
# the innermost pixels. No blue or purple stack is visible in the wind rose.
WIND_STACK_BOUNDARIES = [
    [93, 190, 221, 226],
    [85, 165, 190, 190],
    [90, 172, 209, 214],
    [72, 164, 198, 198],
    [56, 152, 189, 195],
    [68, 191, 282, 294],
    [79, 267, 369, 369],
    [52, 143, 157, 157],
    [49, 73, 86, 86],
    [34, 40, 72, 72],
    [20, 40, 45, 45],
    [20, 28, 32, 32],
    [15, 27, 27, 27],
    [36, 36, 36, 36],
    [41, 62, 62, 62],
    [57, 131, 152, 157],
]
SPEED_BIN_MIDPOINTS_MPS = [3.5, 6.0, 10.0, 14.0]

# Fig. 2 exposes markers at 0.5 m/s. Values are digitized to the nearest
# plotted 0.01; the plateau values and cut-in/rated/cut-out boundaries also
# close against the accompanying text. The displayed Ct values above one at
# low speeds are retained in the fixture; the evaluator records and applies a
# physical 0.999 clamp before square-root wake equations.
TURBINE_CURVE = [
    (0.0, 0.00, 0.00),
    (2.5, 0.00, 1.32),
    (3.0, 0.01, 1.20),
    (3.5, 0.02, 1.06),
    (4.0, 0.04, 0.95),
    (4.5, 0.08, 0.86),
    (5.0, 0.12, 0.80),
    (5.5, 0.17, 0.80),
    (6.0, 0.22, 0.80),
    (6.5, 0.29, 0.80),
    (7.0, 0.37, 0.80),
    (7.5, 0.47, 0.80),
    (8.0, 0.56, 0.80),
    (8.5, 0.68, 0.76),
    (9.0, 0.78, 0.71),
    (9.5, 0.88, 0.55),
    (10.0, 0.95, 0.45),
    (10.5, 0.98, 0.38),
    (11.0, 1.00, 0.32),
    (11.5, 1.00, 0.27),
    (12.0, 1.00, 0.23),
    (12.5, 1.00, 0.20),
    (13.0, 1.00, 0.18),
    (13.5, 1.00, 0.16),
    (14.0, 1.00, 0.14),
    (14.5, 1.00, 0.13),
    (15.0, 1.00, 0.12),
    (15.5, 1.00, 0.11),
    (16.0, 1.00, 0.10),
    (16.5, 1.00, 0.09),
    (17.0, 1.00, 0.08),
    (17.5, 1.00, 0.075),
    (18.0, 1.00, 0.07),
    (18.5, 1.00, 0.06),
    (19.0, 1.00, 0.055),
    (19.5, 1.00, 0.05),
    (20.0, 1.00, 0.045),
    (20.0001, 0.00, 0.00),
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def red_mask(image: np.ndarray) -> np.ndarray:
    return (
        (image[:, :, 0] > 220)
        & (image[:, :, 1] < 100)
        & (image[:, :, 2] < 100)
    )


def extract_candidates(figure9: Path) -> list[tuple[float, float, float]]:
    image = np.asarray(Image.open(figure9).convert("RGB"), dtype=float)
    red = red_mask(image)
    bar = image[
        COLORBAR_TOP_PX:COLORBAR_BOTTOM_PX,
        COLORBAR_X_PX,
    ]
    raw_candidates: list[tuple[float, float, float]] = []
    for y_index in range(GRID_NY):
        for x_index in range(GRID_NX):
            x_pixel = X_ORIGIN_PX + X_STEP_PX * x_index
            y_pixel = Y_ORIGIN_PX + Y_STEP_PX * y_index
            x_low = max(0, int(x_pixel - 11))
            x_high = min(image.shape[1], int(x_pixel + 12))
            y_low = max(0, int(y_pixel - 11))
            y_high = min(image.shape[0], int(y_pixel + 12))
            yy, xx = np.ogrid[y_low:y_high, x_low:x_high]
            radius_squared = (
                (xx - x_pixel) ** 2 + (yy - y_pixel) ** 2
            )
            local_red = red[y_low:y_high, x_low:x_high]
            if int(local_red[radius_squared <= 5.5**2].sum()) < 10:
                continue
            local = image[y_low:y_high, x_low:x_high]
            sample = local[
                (radius_squared >= 6.0**2)
                & (radius_squared <= 10.0**2)
                & (~local_red)
            ]
            sample = sample[
                (sample.max(axis=1) - sample.min(axis=1)) > 25
            ]
            require(sample.size > 0, "candidate has no visible AEH neighborhood")
            colorbar_indices = []
            for pixel in sample:
                squared_distance = ((bar - pixel) ** 2).sum(axis=1)
                colorbar_indices.append(int(np.argmin(squared_distance)))
            local_aeh = [
                2500.0
                - 2000.0 * index / float(len(bar) - 1)
                for index in colorbar_indices
            ]
            # The red point obscures its own background. A robust upper
            # neighborhood quantile preserves the visible ridge ranking.
            rank_value = float(np.percentile(local_aeh, 95.0))
            x_d = -10.0 + 0.5 * x_index
            y_d = 46.0 - 0.5 * y_index
            raw_candidates.append((x_d, y_d, rank_value))
    require(
        len(raw_candidates) == 522,
        f"Fig. 9 extraction expected 522 candidates, got {len(raw_candidates)}",
    )
    ranks = [item[2] for item in raw_candidates]
    minimum = min(ranks)
    maximum = max(ranks)
    require(maximum > minimum, "AEH candidate ranking is degenerate")
    return [
        (
            x_d,
            y_d,
            2000.0 + 500.0 * (rank - minimum) / (maximum - minimum),
        )
        for x_d, y_d, rank in raw_candidates
    ]


def extract_wind_states() -> list[tuple[float, float, float]]:
    raw: list[tuple[float, float, float]] = []
    for direction, boundaries in enumerate(WIND_STACK_BOUNDARIES):
        previous = 0
        for speed, boundary in zip(
            SPEED_BIN_MIDPOINTS_MPS, boundaries, strict=True
        ):
            require(boundary >= previous, "wind-stack boundary is not monotone")
            radial_mass = float(boundary - previous)
            if radial_mass > 0.0:
                raw.append((22.5 * direction, speed, radial_mass))
            previous = boundary
    total = sum(item[2] for item in raw)
    require(total > 0.0, "digitized wind rose is empty")
    return [
        (direction, speed, mass / total)
        for direction, speed, mass in raw
    ]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--figure2", type=Path, required=True)
    parser.add_argument("--figure5", type=Path, required=True)
    parser.add_argument("--figure9", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(digest(args.figure2) == FIGURE2_SHA256, "Fig. 2 SHA-256 mismatch")
    require(digest(args.figure5) == FIGURE5_SHA256, "Fig. 5 SHA-256 mismatch")
    require(digest(args.figure9) == FIGURE9_SHA256, "Fig. 9 SHA-256 mismatch")
    candidates = extract_candidates(args.figure9)
    states = extract_wind_states()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        stream.write(b"T87PXY2\0")
        stream.write(
            struct.pack(
                "<III", len(candidates), len(states), len(TURBINE_CURVE)
            )
        )
        for x_d, y_d, aeh_h in candidates:
            stream.write(struct.pack("<fff", x_d, y_d, aeh_h))
        for direction, speed, probability in states:
            stream.write(struct.pack("<fff", direction, speed, probability))
        for speed, normalized_power, thrust_coefficient in TURBINE_CURVE:
            stream.write(
                struct.pack(
                    "<fff", speed, normalized_power, thrust_coefficient
                )
            )
    output_digest = digest(args.output)
    report = {
        "status": "pass",
        "figure2_sha256": FIGURE2_SHA256,
        "figure5_sha256": FIGURE5_SHA256,
        "figure9_sha256": FIGURE9_SHA256,
        "candidate_count": len(candidates),
        "full_grid_count": GRID_NX * GRID_NY,
        "candidate_fraction": len(candidates) / (GRID_NX * GRID_NY),
        "wind_state_count": len(states),
        "wind_probability_sum": sum(item[2] for item in states),
        "turbine_curve_point_count": len(TURBINE_CURVE),
        "minimum_candidate_aeh_h": min(item[2] for item in candidates),
        "maximum_candidate_aeh_h": max(item[2] for item in candidates),
        "output_sha256": output_digest,
        "output_size_bytes": args.output.stat().st_size,
    }
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
