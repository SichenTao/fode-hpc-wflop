#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0499 source-backed deterministic proxy builder
Paper/DOI: Wen, Song and Wang, Wind farm layout optimization with
uncertain wind condition; 10.1016/j.enconman.2022.115347.
Paper-provided facts: Case A Dirichlet vector [50,100,200,100,50];
Case B has 41 NDAWN stations, 20 hourly years and twelve 30-degree
direction sectors; V=8 m/s; target Fig. 7 turbine curves.
Public code/data: no paper-linked source or machine-readable NDAWN archive
was located. The direct customized-GA predecessor DOI is
10.1109/CCTA41146.2020.9206378; legal full-text retrieval was attempted
but unavailable at the audit date.
Missing/reconstruction: the 41 station time series and exact Fig. 7 knots
are absent. This builder creates a deterministic, station/year-varying
12-sector proxy centered on the distributions visible in target Fig. 10
and stores declared digitized power/Ct knots. It is not author data.
Problem semantic IDs: l0499_case_a_dm_cvar_grid_v1;
l0499_case_b_ndawn41_proxy_dm_cvar_grid_v1.
Method semantic ID: l0499_fixed_count_binary_ga_completed_v1.
Claim boundary: source-backed academic reconstruction, not author data,
source code, random state or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import hashlib
import math
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "shared/data/core99_l0499_proxy.bin"

STATIONS = 41
YEARS = 20
SECTORS = 12
CASE_A_ALPHA = (50.0, 100.0, 200.0, 100.0, 50.0)
POWER_CT_KNOTS = (
    (0.0, 0.0, 0.0),
    (3.0, 0.0, 0.86),
    (4.0, 35.0, 0.86),
    (5.0, 105.0, 0.84),
    (6.0, 225.0, 0.82),
    (7.0, 390.0, 0.80),
    (8.0, 604.0, 0.77),
    (9.0, 910.0, 0.70),
    (10.0, 1210.0, 0.55),
    (11.0, 1500.0, 0.40),
    (25.0, 1500.0, 0.03),
    (25.01, 0.0, 0.0),
)


def normalize(values: list[float]) -> list[float]:
    clipped = [max(value, 0.002) for value in values]
    total = sum(clipped)
    return [value / total for value in clipped]


def station_year_distribution(station: int, year: int) -> list[float]:
    # Fig. 10 visibly has dominant N/SSE/S sectors and smaller E/W sectors.
    base = [
        0.180, 0.095, 0.055, 0.070, 0.125, 0.145,
        0.165, 0.055, 0.035, 0.045, 0.075, 0.055,
    ]
    phase_station = 0.37 * (station + 1)
    phase_year = 0.61 * (year + 1)
    future_shift = 0.018 if year >= 10 else 0.0
    values: list[float] = []
    for sector, value in enumerate(base):
        seasonal = 0.014 * math.sin(
            phase_station + phase_year + 0.71 * sector
        )
        station_shape = 0.010 * math.cos(
            0.23 * (station + 1) * (sector + 1)
        )
        drift = future_shift * math.sin(0.9 * sector + 0.15 * station)
        values.append(value + seasonal + station_shape + drift)
    return normalize(values)


def main() -> None:
    payload = bytearray(b"L0499P1")
    payload.extend(struct.pack("<IIII", STATIONS, YEARS, SECTORS, len(POWER_CT_KNOTS)))
    payload.extend(struct.pack("<5d", *CASE_A_ALPHA))
    for speed, power, ct in POWER_CT_KNOTS:
        payload.extend(struct.pack("<3f", speed, power, ct))
    for station in range(STATIONS):
        for year in range(YEARS):
            payload.extend(
                struct.pack(
                    "<12f", *station_year_distribution(station, year)
                )
            )
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(payload)
    print(OUTPUT)
    print(hashlib.sha256(payload).hexdigest())


if __name__ == "__main__":
    main()
