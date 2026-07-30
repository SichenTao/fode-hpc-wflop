#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: reproducible T17 open-proxy asset builder
Paper/DOI: A New Wake Model and Comparison of Eight Algorithms for Layout
Optimization of Wind Farms in Complex Terrain; 10.1016/j.apenergy.2019.114189
Public source: https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git revision
5b07481ec9b3633a74844651648f266ba82a8b32, MIT
Missing/reconstruction: private paper fields are replaced by a separately
named same-lineage proxy; the exact transform is implemented below
Controlling contract: shared/contracts/core99_t17_brogna_2020.json
Claim boundary: derived open proxy, not the Northwest-China site
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct


NX, NY, SECTORS = 23, 33, 12
HUB_FRACTION = (67.0 - 30.0) / (200.0 - 30.0)


def read_grd(path: Path) -> list[float]:
    tokens = path.read_text(encoding="utf-8").split()
    if tokens[0] != "DSAA" or [int(tokens[1]), int(tokens[2])] != [NX, NY]:
        raise RuntimeError(f"unexpected WAsP grid header: {path}")
    values = [float(value) for value in tokens[9:]]
    if len(values) != NX * NY:
        raise RuntimeError(f"unexpected WAsP grid length: {path}")
    valid = [
        index for index, value in enumerate(values)
        if math.isfinite(value) and abs(value) < 1.0e30
    ]
    if not valid:
        raise RuntimeError(f"no finite WAsP values: {path}")
    for index, value in enumerate(values):
        if math.isfinite(value) and abs(value) < 1.0e30:
            continue
        row, column = divmod(index, NX)
        nearest = min(
            valid,
            key=lambda other: (
                (other // NX - row) ** 2 + (other % NX - column) ** 2,
                other,
            ),
        )
        values[index] = values[nearest]
    return values


def interpolate(
    low: list[float], high: list[float], *, circular_degrees: bool = False
) -> list[float]:
    if not circular_degrees:
        return [
            (1.0 - HUB_FRACTION) * left + HUB_FRACTION * right
            for left, right in zip(low, high)
        ]
    result = []
    for left, right in zip(low, high):
        x = (
            (1.0 - HUB_FRACTION) * math.cos(math.radians(left))
            + HUB_FRACTION * math.cos(math.radians(right))
        )
        y = (
            (1.0 - HUB_FRACTION) * math.sin(math.radians(left))
            + HUB_FRACTION * math.sin(math.radians(right))
        )
        result.append(math.degrees(math.atan2(y, x)))
    return result


def property_at_hub(root: Path, sector: int, name: str) -> list[float]:
    prefix = f"Ridge area   Sector {sector}   Height "
    low = read_grd(root / f"{prefix}30m   {name}.grd")
    high = read_grd(root / f"{prefix}200m   {name}.grd")
    return interpolate(low, high, circular_degrees=name == "Orographic turn")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pywake-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    data_root = (
        args.pywake_root
        / "py_wake/examples/data/ParqueFicticio"
    )
    elevation = read_grd(
        data_root / "Ridge area   Sector All   Height 30m   Elevation.grd"
    )
    payload = bytearray()
    payload.extend(b"T17PXY1\0")
    payload.extend(struct.pack("<III", NX, NY, SECTORS))
    payload.extend(struct.pack("<ffff", 0.0, 6000.0, 0.0, 4000.0))
    payload.extend(struct.pack(f"<{len(elevation)}f", *elevation))
    for sector in range(1, SECTORS + 1):
        for name in (
            "Orographic speed",
            "Orographic turn",
            "Flow inclination",
            "Sector frequency",
        ):
            values = property_at_hub(data_root, sector, name)
            payload.extend(struct.pack(f"<{len(values)}f", *values))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(
        f"t17_proxy_written path={args.output} bytes={len(payload)} "
        "source=PyWake@5b07481ec9b3633a74844651648f266ba82a8b32"
    )


if __name__ == "__main__":
    main()
