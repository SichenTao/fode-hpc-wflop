#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T05 independent paper-equation/H5 oracle
Paper DOI: 10.1016/j.renene.2013.10.023.
Public source: none found.
Missing: CPLEX models and numeric Figure-5 array.
Reconstruction: independently rebuild QIP and power equations from result
layouts; use published Tables 6-8 only as scale/trend references.
Claim boundary: equation-level academic reconstruction, not numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import tempfile


CASES = (
    "t05_case_a_k26",
    "t05_case_a_k30",
    "t05_case_b_k19",
    "t05_case_b_k39",
    "t05_case_c_k15",
    "t05_case_c_k39",
)
K = dict(zip(CASES, (26, 30, 19, 39, 15, 39), strict=True))
PUBLISHED = dict(
    zip(CASES, (12980, 14800, 9549, 18336, 13494, 32453), strict=True)
)


def case_c_wind() -> list[tuple[float, float, float]]:
    profile17 = [1] * 27 + [1.55, 1.7, 2.7, 3.2, 2.7, 1.7, 1.55, 1.2, 1]
    profile17[26] = 1.2
    profile12 = [1] * 27 + [1.45, 1.75, 1.7, 2.35, 1.7, 1.75, 1.45, 1.2, 1]
    profile12[26] = 1.2
    raw = []
    for direction in range(36):
        raw.extend(
            (
                (10.0 * direction, 8.0, 0.005),
                (10.0 * direction, 12.0, 0.008 * profile12[direction]),
                (10.0 * direction, 17.0, 0.011 * profile17[direction]),
            )
        )
    total = sum(item[2] for item in raw)
    return [(d, s, p / total) for d, s, p in raw]


def wind(case_id: str) -> list[tuple[float, float, float]]:
    if "_case_a_" in case_id:
        return [(0.0, 12.0, 1.0)]
    if "_case_b_" in case_id:
        return [(10.0 * d, 12.0, 1.0 / 36.0) for d in range(36)]
    return case_c_wind()


def deficit2(source: int, target: int, degrees: float) -> float:
    if source == target:
        return 0.0
    radians = math.radians(degrees)
    flow_x, flow_y = -math.sin(radians), -math.cos(radians)
    sx, sy = (source % 10) * 200.0, (source // 10) * 200.0
    tx, ty = (target % 10) * 200.0, (target // 10) * 200.0
    dx, dy = tx - sx, ty - sy
    downstream = dx * flow_x + dy * flow_y
    if downstream <= 1e-12:
        return 0.0
    crosswind = abs(dx * flow_y - dy * flow_x)
    if crosswind > 20.0 + 0.1 * downstream:
        return 0.0
    induction = 0.5 * (1.0 - math.sqrt(1.0 - 0.88))
    deficit = 2.0 * induction / (1.0 + 0.1 * downstream / 20.0) ** 2
    return deficit * deficit


def independent_oracle(
    case_id: str, layout: list[int]
) -> tuple[float, float]:
    states = wind(case_id)
    objective = 0.0
    for ai, source in enumerate(layout):
        for target in layout[ai + 1 :]:
            objective += sum(
                probability
                * (
                    deficit2(source, target, direction)
                    + deficit2(target, source, direction)
                )
                for direction, _, probability in states
            )
    power = 0.0
    for direction, speed, probability in states:
        state_power = 0.0
        for target in layout:
            total_deficit2 = sum(
                deficit2(source, target, direction) for source in layout
            )
            velocity = speed * max(0.0, 1.0 - math.sqrt(total_deficit2))
            state_power += 0.3 * velocity**3
        power += probability * state_power
    return objective, power


def run(
    binary: str,
    case_id: str,
    workers: int,
    directory: Path,
) -> dict:
    output = directory / f"{case_id}-w{workers}.json"
    subprocess.run(
        (
            binary,
            "--case",
            case_id,
            "--workers",
            str(workers),
            "--seed",
            "505050",
            "--multistarts",
            "128",
            "--node-limit",
            "4096",
            "--output",
            str(output),
        ),
        check=True,
        timeout=300,
    )
    return json.loads(output.read_text())["runs"][0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--output")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="t05-h5-") as temporary:
        directory = Path(temporary)
        rows = {}
        failures = []
        for case_id in CASES:
            row = run(args.binary, case_id, 4, directory)
            rows[case_id] = row
            layout = row["best_layout"]
            if len(layout) != K[case_id] or len(set(layout)) != K[case_id]:
                failures.append(f"{case_id}: cardinality")
            oracle_qip, oracle_power = independent_oracle(case_id, layout)
            if not math.isclose(
                oracle_qip, row["qip_objective"], rel_tol=2e-12, abs_tol=2e-14
            ):
                failures.append(f"{case_id}: independent QIP oracle")
            if not math.isclose(
                oracle_power,
                row["expected_power_kw"],
                rel_tol=2e-12,
                abs_tol=2e-9,
            ):
                failures.append(f"{case_id}: independent power oracle")
            if not math.isclose(
                row["qip_objective"],
                row["milp_linearized_objective"],
                rel_tol=2e-12,
                abs_tol=2e-14,
            ):
                failures.append(f"{case_id}: QIP/MILP equivalence")
            # These are not exact-value gates. A narrow scale band catches
            # direction/power/unit errors while allowing solver differences.
            relative = abs(row["expected_power_kw"] / PUBLISHED[case_id] - 1)
            if relative > 0.02:
                failures.append(f"{case_id}: published scale differs >2%")
        serial = run(args.binary, "t05_case_b_k19", 1, directory)
        parallel = rows["t05_case_b_k19"]
        if serial["scientific_hash"] != parallel["scientific_hash"]:
            failures.append("one/four-worker scientific hash")
        if serial["explored_nodes"] != parallel["explored_nodes"]:
            failures.append("one/four-worker explored node set")
        payload = {
            "schema_version": 1,
            "corpus_id": "T05",
            "doi": "10.1016/j.renene.2013.10.023",
            "status": "H5_pass" if not failures else "H5_fail",
            "paper_case_count": len(CASES),
            "independent_oracle": "Python paper equations, separate source",
            "bounded_rows": rows,
            "one_four_replay": {
                "same_scientific_hash":
                    serial["scientific_hash"]
                    == parallel["scientific_hash"],
                "same_explored_nodes":
                    serial["explored_nodes"] == parallel["explored_nodes"],
                "end_to_end_speedup":
                    serial["end_to_end_seconds"]
                    / parallel["end_to_end_seconds"],
            },
            "failures": failures,
            "claim_boundary":
                "equation-level academic reconstruction; published powers "
                "are scale checks, not exact-value gates",
        }
        encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        payload["receipt_sha256_without_self"] = hashlib.sha256(
            encoded.encode()
        ).hexdigest()
        encoded = json.dumps(payload, indent=2, sort_keys=True) + "\n"
        if args.output:
            Path(args.output).parent.mkdir(parents=True, exist_ok=True)
            Path(args.output).write_text(encoded)
        print(encoded, end="")
        if failures:
            raise RuntimeError("; ".join(failures))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
