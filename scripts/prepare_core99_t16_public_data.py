#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T16 compact public-data preparation
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source: https://github.com/byuflowlab/thomas2021-wec at commit
8ff27d66079591f25619abeedbfc970d70e2b520. This later same-author-lineage
repository contains the 12-direction Nantucket wind rose, NREL 5-MW CP/CT
table, concentric 38-turbine layout, and 200-start generators used by the
same model family. It has no explicit repository license and is therefore
used as factual semantic evidence, not copied as executable source.
Paper-provided assets: 38 turbines; NREL 5-MW; 12 Nantucket directions;
2-km physical circular farm; five-diameter concentric baseline.
Missing/conflicts: the 2019 paper did not publish an original source archive,
exact pseudo-random states, or an explicit data license. The later repository
uses a 1936.8 m hub-centre radius so the 126.4 m rotor remains inside the
2-km physical circle; Eq. (21) prints a 2-km centre constraint. The public
NREL5MWCPCT_dict.txt header labels columns two and three as CP and CT, but its
own readandwritedict.py and the source pickle prove that the actual order is
wind speed, CT, CP. This preparation follows those higher-authority assets.
Reconstruction: extract only numeric facts needed by the independent C++
implementation, centre the public baseline at (0,0), and encode a versioned
little-endian binary with deterministic SHA-256 provenance.
Problem semantic ID: t16_nantucket38_author_lineage_reconstructed_v1
Method semantic ID: t16_wec_slsqp_autodiff_reconstruction_v1
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Claim boundary: compact factual input fixture for academic reproduction; not
redistribution of the later repository's executable code or an author-2019
source release
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


EXPECTED_REVISION = "8ff27d66079591f25619abeedbfc970d70e2b520"
MAGIC = b"T16DATA1"


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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--allow-revision-mismatch", action="store_true")
    args = parser.parse_args()

    source = Path(args.source_root).resolve()
    revision = (
        source / ".git"
    )
    if not revision.exists():
        raise SystemExit(f"not a git checkout: {source}")
    import subprocess

    observed_revision = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if (
        observed_revision != EXPECTED_REVISION
        and not args.allow_revision_mismatch
    ):
        raise SystemExit(
            f"expected source revision {EXPECTED_REVISION}, "
            f"observed {observed_revision}"
        )

    inputs = source / "archive" / "project-code" / "input_files"
    wind = numeric_rows(inputs / "nantucket_wind_rose_for_LES.txt", 3)
    cpct = numeric_rows(inputs / "NREL5MWCPCT_dict.txt", 3)
    layout = numeric_rows(inputs / "RoundFarm38Turb5DSpacing.txt", 2)
    if len(wind) != 12:
        raise SystemExit(f"expected 12 wind states, observed {len(wind)}")
    if len(layout) != 38:
        raise SystemExit(f"expected 38 turbines, observed {len(layout)}")
    if len(cpct) < 100:
        raise SystemExit(f"unexpectedly short CP/CT table: {len(cpct)}")
    probability_sum = sum(row[2] for row in wind)
    # The public same-lineage array deliberately retains 3.8% probability
    # outside the twelve 8 m/s production bins.  Preserve the printed array
    # instead of silently renormalizing it.
    if abs(probability_sum - 0.962) > 1.0e-12:
        raise SystemExit(
            f"expected the public 0.962 probability mass, got {probability_sum}"
        )

    centre_x = sum(row[0] for row in layout) / len(layout)
    centre_y = sum(row[1] for row in layout) / len(layout)
    centred = [(x - centre_x, y - centre_y) for x, y in layout]

    payload = bytearray(MAGIC)
    payload.extend(struct.pack("<I", len(cpct)))
    for row in wind:
        payload.extend(struct.pack("<3d", *row))
    for speed, ct, cp in cpct:
        # The text header is reversed. The repository's conversion script
        # assigns column 2 from data['CT'] and column 3 from data['CP'].
        payload.extend(struct.pack("<3d", speed, cp, ct))
    for row in centred:
        payload.extend(struct.pack("<2d", *row))

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    print(
        f"revision={observed_revision} cpct_rows={len(cpct)} "
        f"wind_probability_mass={probability_sum:.15g} "
        f"bytes={len(payload)} sha256={digest}"
    )


if __name__ == "__main__":
    main()
