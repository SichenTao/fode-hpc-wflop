#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: deterministic L0371 wind, stability and V80 proxy builder
Paper/DOI: Guo et al., Influence of atmospheric stability on wind farm
layout optimization based on an improved Gaussian wake model;
10.1016/j.jweia.2021.104548.
Paper-provided facts: seven ideal and seven Horns Rev stability classes,
three ideal wind cases, Horns Rev 1 geometry and the V80 turbine identity.
Public supplemental source: Feng and Shen, DOI 10.3390/en8043075, Table 1,
provides the 12 sector-wise Weibull parameters and frequencies derived from
the same 1999-2002 Horns Rev mast record. Local PDF SHA-256:
d8f82df7d70cfbe02c3ff8ae36c9cc20ef0995b945486c46912a72a6c3d160d2.
Missing/conflicts: the target paper supplies no numerical grid resolution,
case-(c) array, Horns Rev V80 curve array, stability array, code, seeds,
iteration count or repeat count. Its Fig. 4 contains 200 m-separated
turbines although Eq. (13) is typeset as strict distance >5D.
Reconstruction: use a 200 m ideal grid confirmed by Figs. 4-6; digitize
case-(c) relative 8/12/17 m/s stacks from Fig. 3 with cardinal gridline
occlusion filled by neighboring-bin interpolation; digitize the V80 power
and Ct curves from Feng and Shen Fig. 1(b); digitize and direction-normalize
the four stability bars from target Fig. 11. The C++ implementation records
the generated fixture SHA-256 and never calls these author-provided arrays.
Claim boundary: versioned declared academic proxy, not author data or exact
numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct


# Direction centres are 0,10,...,350 degrees clockwise from North. Values are
# normalized pixel-length estimates for the three visible speed stacks in
# target Fig. 3(iii). Cardinal-axis bins use neighboring-bin interpolation
# because the plotted gridline occludes the raster color.
CASE_C_STACKS = [
    (0.00123, 0.00918, 0.01404),
    (0.00027, 0.00779, 0.01323),
    (0.00212, 0.00882, 0.01051),
    (0.00310, 0.00898, 0.01220),
    (0.00348, 0.00877, 0.01127),
    (0.00348, 0.00893, 0.01323),
    (0.00327, 0.00882, 0.01405),
    (0.00191, 0.00866, 0.01410),
    (0.00011, 0.00392, 0.01258),
    (0.00101, 0.00835, 0.01334),
    (0.00191, 0.00877, 0.01410),
    (0.00327, 0.00887, 0.01416),
    (0.00387, 0.00893, 0.01405),
    (0.00365, 0.00898, 0.01416),
    (0.00310, 0.00887, 0.01399),
    (0.00180, 0.00877, 0.01394),
    (0.00044, 0.00779, 0.01372),
    (0.00012, 0.00646, 0.01166),
    (0.00199, 0.00780, 0.01361),
    (0.00397, 0.00866, 0.01350),
    (0.00408, 0.00915, 0.01356),
    (0.00354, 0.00877, 0.01388),
    (0.00343, 0.00915, 0.01372),
    (0.00305, 0.00871, 0.01454),
    (0.00245, 0.00887, 0.01410),
    (0.00082, 0.00860, 0.01416),
    (0.00005, 0.00299, 0.01247),
    (0.00049, 0.01016, 0.01538),
    (0.00093, 0.01432, 0.01829),
    (0.00229, 0.01764, 0.02221),
    (0.00278, 0.01693, 0.03485),
    (0.00343, 0.02189, 0.04220),
    (0.00354, 0.01671, 0.03446),
    (0.00387, 0.01759, 0.02238),
    (0.00397, 0.01356, 0.01818),
    (0.00218, 0.01056, 0.01584),
]

# V80 2 MW curve digitized from Feng and Shen Fig. 1(b). The target paper
# cites Jensen et al. (2004) for these characteristics but does not tabulate
# them. Values are linear-interpolation knots, not manufacturer source data.
V80_CURVE = [
    (0.0, 0.0, 0.0),
    (3.0, 0.0, 0.82),
    (4.0, 0.07, 0.86),
    (5.0, 0.16, 0.86),
    (6.0, 0.28, 0.86),
    (7.0, 0.45, 0.86),
    (8.0, 0.67, 0.85),
    (9.0, 0.94, 0.82),
    (10.0, 1.23, 0.74),
    (11.0, 1.54, 0.66),
    (12.0, 1.78, 0.56),
    (13.0, 1.90, 0.45),
    (14.0, 1.97, 0.36),
    (15.0, 2.00, 0.29),
    (16.0, 2.00, 0.24),
    (18.0, 2.00, 0.17),
    (20.0, 2.00, 0.13),
    (22.0, 2.00, 0.10),
    (25.0, 2.00, 0.07),
    (25.01, 0.0, 0.0),
    (30.0, 0.0, 0.0),
]

# L (m), z0 (m) in target Tables 2 and 4.
IDEAL_STABILITY = [
    (-10.0, 0.3),
    (-25.0, 0.3),
    (-100.0, 0.3),
    (1.0e30, 0.3),
    (200.0, 0.3),
    (100.0, 0.3),
    (20.0, 0.3),
]
HORNS_STABILITY = [
    (-74.0, 0.044),
    (-143.0, 0.046),
    (-334.0, 0.047),
    (2771.0, 0.045),
    (321.0, 0.034),
    (115.0, 0.027),
    (27.0, 0.006),
]

# Target Fig. 11 stacked bars (vu,u,nu,n,ns,s,vs). Each direction is
# normalized independently, matching the paper's stated missing-value
# normalization before use.
STABILITY_BARS = [
    [0.15, 0.18, 0.22, 0.08, 0.03, 0.07, 0.08],  # N
    [0.11, 0.12, 0.08, 0.07, 0.06, 0.17, 0.21],  # E
    [0.07, 0.09, 0.10, 0.18, 0.09, 0.20, 0.14],  # S
    [0.11, 0.19, 0.26, 0.10, 0.04, 0.11, 0.08],  # W
]

# Feng and Shen Table 1: scale A, shape c, sector frequency f(%).
HORNS_WEIBULL = [
    (8.89, 2.09, 4.82),
    (9.27, 2.13, 4.06),
    (8.23, 2.29, 3.59),
    (9.78, 2.30, 5.27),
    (11.64, 2.67, 9.12),
    (11.03, 2.45, 6.97),
    (11.50, 2.51, 9.17),
    (11.92, 2.40, 11.84),
    (11.49, 2.35, 12.41),
    (11.08, 2.27, 11.34),
    (11.34, 2.24, 11.70),
    (10.76, 2.19, 9.69),
]


def normalized(values: list[float]) -> list[float]:
    total = sum(values)
    if total <= 0.0:
        raise RuntimeError("cannot normalize empty L0371 probability array")
    return [value / total for value in values]


def build_payload() -> bytes:
    case_c = normalized([value for row in CASE_C_STACKS for value in row])
    stability = [
        value
        for row in STABILITY_BARS
        for value in normalized(row)
    ]
    wind = [
        (scale, shape, frequency / 100.0)
        for scale, shape, frequency in HORNS_WEIBULL
    ]
    payload = bytearray(b"L0371P1")
    payload.extend(
        struct.pack(
            "<IIIII",
            len(case_c),
            len(V80_CURVE),
            len(IDEAL_STABILITY),
            len(HORNS_STABILITY),
            len(stability),
        )
    )
    for value in case_c:
        payload.extend(struct.pack("<f", value))
    for speed, power, thrust in V80_CURVE:
        payload.extend(struct.pack("<fff", speed, power, thrust))
    for length, roughness in IDEAL_STABILITY:
        payload.extend(struct.pack("<dd", length, roughness))
    for length, roughness in HORNS_STABILITY:
        payload.extend(struct.pack("<dd", length, roughness))
    for value in stability:
        payload.extend(struct.pack("<f", value))
    for scale, shape, frequency in wind:
        payload.extend(struct.pack("<fff", scale, shape, frequency))
    return bytes(payload)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(build_payload())


if __name__ == "__main__":
    main()
