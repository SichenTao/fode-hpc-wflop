#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0259 independent equation, LIBSVM and replay validator
Paper/DOI: Wind farm layout optimization based on support vector regression
guided genetic algorithm with consideration of participation among
landowners; 10.1016/j.enconman.2019.06.082.
Public source, conflicts, missing facts, reconstruction completion,
semantic IDs, production backend and claim boundary:
hpc/core99_cpp/include/core99/ju_l0259.hpp.
Independent paths: direct Python wake equations and sklearn.svm.SVR are
compared against the pure-C++ precomputed evaluator and official LIBSVM.
Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess

import numpy as np
from sklearn.svm import SVR


ROOT = Path(__file__).resolve().parents[1]
ROTOR_RADIUS = 38.5
ROUGHNESS = 0.00025


def invoke(binary: Path, *arguments: str) -> dict:
    completed = subprocess.run(
        [str(binary), *arguments],
        check=True,
        capture_output=True,
        text=True,
        timeout=600,
    )
    return json.loads(completed.stdout)


def turbine_power(speed: float) -> float:
    if speed < 2.0 or speed >= 18.0:
        return 0.0
    if speed < 12.8:
        return 0.3 * speed**3
    return 629.1


def winds(profile: int) -> list[tuple[float, float]]:
    if profile == 1:
        return [(0.0, 1.0)]
    if profile == 2:
        return [
            (0.0, 0.25),
            (math.pi / 2, 0.25),
            (math.pi, 0.25),
            (3 * math.pi / 2, 0.25),
        ]
    return [
        (0.0, 0.20),
        (math.pi / 3, 0.30),
        (2 * math.pi / 3, 0.20),
        (math.pi, 0.10),
        (4 * math.pi / 3, 0.10),
        (5 * math.pi / 3, 0.10),
    ]


def overlap(distance: float, wake_radius: float) -> float:
    if distance >= wake_radius + ROTOR_RADIUS:
        return 0.0
    if distance <= abs(wake_radius - ROTOR_RADIUS):
        radius = min(wake_radius, ROTOR_RADIUS)
        return math.pi * radius**2
    first = math.acos(max(-1.0, min(
        1.0,
        (distance**2 + wake_radius**2 - ROTOR_RADIUS**2)
        / (2 * distance * wake_radius),
    )))
    second = math.acos(max(-1.0, min(
        1.0,
        (distance**2 + ROTOR_RADIUS**2 - wake_radius**2)
        / (2 * distance * ROTOR_RADIUS),
    )))
    radicand = max(
        0.0,
        (-distance + wake_radius + ROTOR_RADIUS)
        * (distance + wake_radius - ROTOR_RADIUS)
        * (distance - wake_radius + ROTOR_RADIUS)
        * (distance + wake_radius + ROTOR_RADIUS),
    )
    return (
        wake_radius**2 * first
        + ROTOR_RADIUS**2 * second
        - 0.5 * math.sqrt(radicand)
    )


def independent_evaluation(
    layout: list[int],
    profile: int,
    cell_width: float = 154.0,
    hub_height: float = 88.0,
) -> dict:
    coordinates = [
        (
            (node % 12 + 0.5) * cell_width,
            (node // 12 + 0.5) * cell_width,
        )
        for node in layout
    ]
    expected = [0.0] * len(layout)
    entrainment = 0.5 / math.log(hub_height / ROUGHNESS)
    for angle, probability in winds(profile):
        cosine, sine = math.cos(angle), math.sin(angle)
        rotated = [
            (cosine * x - sine * y, sine * x + cosine * y)
            for x, y in coordinates
        ]
        for downstream in range(len(layout)):
            deficit_squared = 0.0
            for upstream in range(len(layout)):
                dy = rotated[upstream][1] - rotated[downstream][1]
                if dy <= 0:
                    continue
                dx = abs(rotated[downstream][0] - rotated[upstream][0])
                wake_radius = ROTOR_RADIUS + entrainment * dy
                area = overlap(dx, wake_radius)
                deficit = (
                    (2 / 3)
                    * ROTOR_RADIUS**2
                    / wake_radius**2
                    * area
                    / (math.pi * ROTOR_RADIUS**2)
                )
                deficit_squared += deficit**2
            speed = 13.0 * (1.0 - math.sqrt(deficit_squared))
            expected[downstream] += probability * turbine_power(speed)
    total = sum(expected)
    return {
        "expected_power_kw": total,
        "efficiency_percent":
            100.0 * total / (len(layout) * turbine_power(13.0)),
        "turbine_power_kw": expected,
    }


def relative_error(left: float, right: float) -> float:
    return abs(left - right) / max(1.0, abs(left), abs(right))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument(
        "--receipt",
        type=Path,
        default=ROOT
        / "evidence/development/"
        "L0259_h5_independent_equation_validation_20260731.json",
    )
    args = parser.parse_args()

    cases = invoke(args.binary, "--mode", "list-cases")["paper_case_ids"]
    expected_cases = [
        f"l0259_d{wind}_l{landscape}_n{turbines}"
        for wind in range(1, 4)
        for landscape in range(13)
        for turbines in (15, 20, 25)
    ]
    if cases != expected_cases:
        raise RuntimeError("L0259 117-case identity/order mismatch")

    inspections = {}
    for case_id in (
        "l0259_d1_l0_n15",
        "l0259_d2_l1_n20",
        "l0259_d3_l6_n25",
        "l0259_d3_l12_n25",
    ):
        item = invoke(
            args.binary,
            "--mode", "inspect",
            "--case", case_id,
            "--variant", "paper_probability",
        )
        landscape = item["landscape"]
        expected_available = (
            144 if landscape == 0 else (120 if landscape <= 6 else 132)
        )
        if (
            item["candidate_count"] != 144
            or item["available_count"] != expected_available
            or item["cell_width_m"] != 154
            or item["hub_height_m"] != 88
            or item["paper_population"] != 120
            or item["paper_generations"] != 200
            or item["paper_monte_carlo_layouts"] != 10000
            or item["paper_repeats"] != 100
        ):
            raise RuntimeError(f"L0259 settings mismatch for {case_id}")
        inspections[case_id] = item

    layout = [0, 2, 4, 6, 8, 10, 25, 27, 29, 31, 33, 35, 48, 50, 52,
              54, 56, 58, 73, 75, 77, 79, 81, 83, 96]
    cpp = invoke(
        args.binary,
        "--mode", "evaluate",
        "--case", "l0259_d3_l0_n25",
        "--variant", "paper_probability",
        "--indices", ",".join(map(str, layout)),
    )
    independent = independent_evaluation(layout, 3)
    errors = {
        "expected_power_kw": relative_error(
            cpp["expected_power_kw"], independent["expected_power_kw"]
        ),
        "efficiency_percent": relative_error(
            cpp["efficiency_percent"], independent["efficiency_percent"]
        ),
        "maximum_turbine_power": max(
            relative_error(left, right)
            for left, right in zip(
                cpp["turbine_power_kw"],
                independent["turbine_power_kw"],
                strict=True,
            )
        ),
    }
    if max(errors.values()) > 2.0e-12:
        raise RuntimeError(f"L0259 independent equation mismatch {errors}")

    surrogate = invoke(
        args.binary,
        "--mode", "surrogate",
        "--case", "l0259_d3_l5_n25",
        "--variant", "paper_probability",
        "--workers", "4",
        "--monte-carlo-layouts", "500",
        "--seed", "259500",
    )
    x = np.array(
        [(node % 12, node // 12) for node in range(144)],
        dtype=np.float64,
    )
    model = SVR(kernel="rbf", C=2000.0, gamma=0.3, epsilon=0.1)
    model.fit(x, np.asarray(surrogate["training_targets_kw"]))
    sklearn_prediction = model.predict(x)
    svr_max_abs_error = float(np.max(np.abs(
        sklearn_prediction - np.asarray(surrogate["predictions_kw"])
    )))
    if svr_max_abs_error > 2.0e-6:
        raise RuntimeError(
            f"L0259 official LIBSVM/sklearn mismatch {svr_max_abs_error}"
        )
    if surrogate["observed_workers"] != 4:
        raise RuntimeError("L0259 surrogate did not use requested workers")

    common = [
        "--mode", "optimize",
        "--case", "l0259_d3_l5_n25",
        "--variant", "paper_probability",
        "--seed", "259700",
        "--monte-carlo-layouts", "500",
        "--population", "40",
        "--generations", "8",
    ]
    one = invoke(args.binary, *common, "--workers", "1")["runs"][0]
    four = invoke(args.binary, *common, "--workers", "4")["runs"][0]
    if one["scientific_hash"] != four["scientific_hash"]:
        raise RuntimeError("L0259 one/all-core scientific replay mismatch")
    if one["physical_fes"] != 820 or four["physical_fes"] != 820:
        raise RuntimeError("L0259 physical FES accounting mismatch")
    if four["observed_workers"] != 4:
        raise RuntimeError("L0259 all-core work not observed")
    if (
        not four["best_evaluation"]["feasible"]
        or four["best_evaluation"]["efficiency_percent"]
        < four["initial_best"]["efficiency_percent"] - 1.0e-12
    ):
        raise RuntimeError("L0259 retained-best semantics failed")

    source = invoke(
        args.binary,
        *common,
        "--variant", "source_normal_threshold",
        "--workers", "4",
    )["runs"][0]
    if (
        source["method_semantic_id"]
        != "l0259_sugga_source_normal_threshold_v1"
    ):
        raise RuntimeError("L0259 source conflict identity absent")

    receipt = {
        "schema_version": 1,
        "corpus_id": "L0259",
        "doi": "10.1016/j.enconman.2019.06.082",
        "status": "H5_pass",
        "paper_case_count": len(cases),
        "paper_settings": inspections,
        "independent_equation_check": {
            "case_id": "l0259_d3_l0_n25",
            "cpp": cpp,
            "independent": independent,
            "relative_errors": errors,
        },
        "svr_check": {
            "training_layouts": 500,
            "cpp_backend": "official LIBSVM C++",
            "independent_backend": "sklearn.svm.SVR",
            "maximum_absolute_prediction_error_kw": svr_max_abs_error,
        },
        "bounded_replay": {
            "one_worker": one,
            "four_workers": four,
            "source_conflict_variant": source,
            "scientific_hash_identical": True,
            "speedup": {
                "monte_carlo_truth": one["monte_carlo_truth_seconds"]
                / max(four["monte_carlo_truth_seconds"], 1.0e-15),
                "population_truth": one["population_truth_seconds"]
                / max(four["population_truth_seconds"], 1.0e-15),
                "algorithm": one["algorithm_seconds"]
                / max(four["algorithm_seconds"], 1.0e-15),
                "end_to_end": one["end_to_end_seconds"]
                / max(four["end_to_end_seconds"], 1.0e-15),
            },
        },
        "claim_boundary": (
            "Academic paper/source flexible reproduction; published table "
            "values are scale references, not exact-number acceptance targets."
        ),
    }
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
