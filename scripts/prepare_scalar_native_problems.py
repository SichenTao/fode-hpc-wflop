#!/usr/bin/env python3
"""Build the paper-native scalar/discrete WFLOP case contracts.

This generator deliberately separates case families that earlier project
snapshots grouped under the FODE 50-case transfer benchmark.  The generated
JSON files are self-contained and are the executable contracts.  The local
MATLAB ``.mat`` arrays are evidence inputs only and are never copied verbatim.

The numerical wind arrays are taken from the user-owned HGPSO/WFLOP source
archive when a paper points to the same named scenario family.  Each generated
contract records the source hash and a P0/P1/P3 claim boundary.  P3 contracts
must never be reported as the unavailable original author data.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from scipy.io import loadmat


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WIND_ROOT = Path(
    "/home/sichentao/Desktop/projects/04-ENE-wind-farm-layout-optimization/"
    "WFLOP-HGPSO-TsungHua/data/benchmarks/windscenarios"
)


@dataclass(frozen=True)
class Scenario:
    name: str
    directions_rad: tuple[float, ...]
    speeds_mps: tuple[float, ...]
    probabilities: tuple[tuple[float, ...], ...]
    source: str
    source_sha256: str


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_hash(value: object) -> str:
    encoded = json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=True
    ).encode()
    return hashlib.sha256(encoded).hexdigest()


def scenario_from_mat(root: Path, filename: str, name: str) -> Scenario:
    path = root / filename
    if not path.is_file():
        raise FileNotFoundError(
            f"required source wind array is unavailable: {path}"
        )
    raw = loadmat(path)
    theta = tuple(float(value) for value in raw["theta"].reshape(-1))
    velocity = tuple(float(value) for value in raw["velocity"].reshape(-1))
    probability_array = raw["f_theta_v"]
    expected_shape = (len(theta), len(velocity))
    if probability_array.shape != expected_shape:
        raise RuntimeError(
            f"{path}: expected probability shape {expected_shape}, "
            f"found {probability_array.shape}"
        )
    probabilities = tuple(
        tuple(float(value) for value in probability_array[row, :])
        for row in range(len(theta))
    )
    return Scenario(
        name=name,
        directions_rad=theta,
        speeds_mps=velocity,
        probabilities=probabilities,
        source=str(path),
        source_sha256=sha256(path),
    )


def declared_scenario(
    name: str,
    directions_deg: Iterable[float],
    speeds_mps: Iterable[float],
    probabilities: Iterable[Iterable[float]],
    source: str,
) -> Scenario:
    directions = tuple(math.radians(float(value)) for value in directions_deg)
    speeds = tuple(float(value) for value in speeds_mps)
    probability_rows = tuple(
        tuple(float(value) for value in row) for row in probabilities
    )
    return Scenario(
        name=name,
        directions_rad=directions,
        speeds_mps=speeds,
        probabilities=probability_rows,
        source=source,
        source_sha256="paper_declared_no_separate_binary_asset",
    )


def unavailable_cells(pattern: int) -> list[int]:
    def inclusive(start: int, stop: int, step: int = 1) -> list[int]:
        return list(range(start, stop + 1, step))

    patterns = {
        0: [],
        1: inclusive(121, 144),
        2: inclusive(61, 84),
        3: inclusive(11, 143, 12) + inclusive(12, 144, 12),
        4: inclusive(6, 143, 12) + inclusive(7, 145, 12),
        5: (
            inclusive(41, 104, 12)
            + inclusive(42, 104, 12)
            + inclusive(43, 104, 12)
            + inclusive(44, 104, 12)
        ),
        6: (
            inclusive(1, 27, 12)
            + inclusive(2, 27, 12)
            + inclusive(11, 35, 12)
            + inclusive(12, 36, 12)
            + inclusive(109, 144, 12)
            + inclusive(119, 144, 12)
            + inclusive(110, 144, 12)
            + inclusive(120, 144, 12)
        ),
        7: inclusive(133, 144),
        8: inclusive(61, 72),
        9: inclusive(12, 144, 12),
        10: inclusive(6, 144, 12),
        11: inclusive(42, 104, 12) + inclusive(43, 104, 12),
        12: [1, 2, 11, 12, 13, 24, 121, 132, 133, 134, 143, 144],
    }
    return sorted(set(patterns[pattern]))


def make_case(
    *,
    case_id: str,
    semantics_id: str,
    scenario: Scenario,
    rows: int,
    cols: int,
    turbine_count: int,
    cell_width: float,
    na_pattern: int = 0,
    unavailable: list[int] | None = None,
    hub_height: float = 80.0,
) -> dict:
    case = {
        "case_id": case_id,
        "semantics_id": semantics_id,
        "rows": rows,
        "cols": cols,
        "turbine_count": turbine_count,
        "cell_width": cell_width,
        "rotor_diameter": 77.0,
        "hub_height": hub_height,
        "surface_roughness": 0.00025,
        "wake_deficit_coefficient": 2.0 / 3.0,
        "power_curve_cubic_coefficient": 0.3,
        "power_curve_rated_kw": 629.1,
        "power_curve_cutin_mps": 2.0,
        "power_curve_rated_mps": 12.8,
        "power_curve_cutout_mps": 18.0,
        "wind_directions_rad": list(scenario.directions_rad),
        "wind_speeds_mps": list(scenario.speeds_mps),
        "joint_probabilities": [list(row) for row in scenario.probabilities],
        "na_pattern": na_pattern,
        "unavailable_cells_1based": (
            unavailable_cells(na_pattern) if unavailable is None else unavailable
        ),
        "source_windscenario": scenario.source,
        "source_windscenario_sha256": scenario.source_sha256,
    }
    case["case_hash"] = canonical_hash(case)
    return case


def write_contract(
    filename: str,
    *,
    problem_id: str,
    semantics_id: str,
    doi: str,
    fidelity: str,
    claim_boundary: str,
    physical_budget: int,
    cases: list[dict],
) -> None:
    contract = {
        "schema_version": 1,
        "problem_id": problem_id,
        "semantics_id": semantics_id,
        "paper_doi": doi,
        "fidelity_class": fidelity,
        "claim_boundary": claim_boundary,
        "physical_fes_budget": physical_budget,
        "case_count": len(cases),
        "cases": cases,
    }
    contract["contract_hash"] = canonical_hash(contract)
    target = ROOT / "shared/contracts" / filename
    target.write_text(json.dumps(contract, indent=2) + "\n", encoding="utf-8")


def landuse_cases(
    semantics_id: str,
    scenarios: Iterable[Scenario],
    turbine_counts: Iterable[int],
    *,
    cell_width: float,
    hub_height: float,
) -> list[dict]:
    return [
        make_case(
            case_id=f"{scenario.name}_N{turbines}_NA{pattern}",
            semantics_id=semantics_id,
            scenario=scenario,
            rows=12,
            cols=12,
            turbine_count=turbines,
            cell_width=cell_width,
            na_pattern=pattern,
            hub_height=hub_height,
        )
        for scenario in scenarios
        for turbines in turbine_counts
        for pattern in range(13)
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wind-root", type=Path, default=DEFAULT_WIND_ROOT)
    args = parser.parse_args()

    ws1 = declared_scenario(
        "WS1", [0], [13], [[1.0]],
        "AGPSO/HGPSO paper-declared WS1",
    )
    ws2 = declared_scenario(
        "WS2", [0, 90, 180, 270], [13], [[0.25]] * 4,
        "AGPSO/HGPSO paper-declared WS2",
    )
    ws3 = declared_scenario(
        "WS3", [0, 60, 120, 180, 240, 300], [13],
        [[0.2], [0.3], [0.2], [0.1], [0.1], [0.1]],
        "AGPSO/HGPSO paper-declared WS3",
    )
    ws4 = declared_scenario(
        "WS4", list(range(0, 360, 30)), [7, 10, 13],
        [[0.008333, 0.016667, 0.058333] for _ in range(12)],
        "AGPSO/HGPSO paper-declared WS4",
    )
    write_contract(
        "agpso_aiga_hgpso_native_156_cases.json",
        problem_id="agpso_aiga_hgpso_landuse_156",
        semantics_id="agpso_aiga_hgpso_landuse_156_v1",
        doi="10.1016/j.enconman.2022.116174;10.1007/s42235-024-00498-3;"
        "10.26599/tst.2026.9010059",
        fidelity="P1_paper_and_source_equivalent",
        claim_boundary=(
            "Exact paper-declared 4 wind scenarios x 3 turbine counts x "
            "13 land-use patterns under the shared discrete Jensen model."
        ),
        physical_budget=24000,
        cases=landuse_cases(
            "agpso_aiga_hgpso_landuse_156_v1",
            [ws1, ws2, ws3, ws4],
            [15, 20, 25],
            cell_width=231.0,
            hub_height=80.0,
        ),
    )

    d1 = ws1
    d2 = ws2
    d3 = ws3
    write_contract(
        "clshade_native_117_cases.json",
        problem_id="clshade_landuse_117",
        semantics_id="clshade_landuse_117_v1",
        doi="10.1016/j.asoc.2023.110306",
        fidelity="P1_paper_declared",
        claim_boundary=(
            "Paper-declared D1-D3 x N15/20/25 x L0-L12; cell width and "
            "GE1.5sl hub/power constants follow the paper model."
        ),
        physical_budget=20000,
        cases=landuse_cases(
            "clshade_landuse_117_v1",
            [d1, d2, d3],
            [15, 20, 25],
            cell_width=154.0,
            hub_height=88.0,
        ),
    )
    write_contract(
        "ise_native_117_cases.json",
        problem_id="ise_landuse_117",
        semantics_id="ise_landuse_117_v1",
        doi="10.1016/j.engappai.2023.106198",
        fidelity="P1_paper_declared",
        claim_boundary=(
            "Paper-declared P1-P3 x N15/20/25 x L0-L12 with 154 m cells; "
            "the shared discrete evaluator is parameterized per case."
        ),
        physical_budget=20000,
        cases=landuse_cases(
            "ise_landuse_117_v1",
            [d1, d2, d3],
            [15, 20, 25],
            cell_width=154.0,
            hub_height=80.0,
        ),
    )

    wc1 = ws2
    wc2 = ws3
    wc3 = declared_scenario(
        "WC3", list(range(0, 360, 30)), [7, 10, 13],
        [[0.008333, 0.016667, 0.058333] for _ in range(12)],
        "A-LSHADE paper-declared WC3",
    )
    alshade_scenarios = [
        Scenario(
            "WC1", ws2.directions_rad, ws2.speeds_mps, ws2.probabilities,
            "A-LSHADE paper-declared WC1", ws2.source_sha256,
        ),
        Scenario(
            "WC2", ws3.directions_rad, ws3.speeds_mps, ws3.probabilities,
            "A-LSHADE paper-declared WC2", ws3.source_sha256,
        ),
        wc3,
    ]
    write_contract(
        "alshade_native_117_cases.json",
        problem_id="alshade_complex_wake_117",
        semantics_id="alshade_complex_wake_117_v1",
        doi="10.1109/pic62406.2024.10892732",
        fidelity="P1_paper_declared",
        claim_boundary=(
            "Paper-declared WC1-WC3 x N15/20/25 x L0-L12; 120 x 200 "
            "paper work is frozen as 24,000 complete layout evaluations."
        ),
        physical_budget=24000,
        cases=landuse_cases(
            "alshade_complex_wake_117_v1",
            alshade_scenarios,
            [15, 20, 25],
            cell_width=231.0,
            hub_height=80.0,
        ),
    )

    cgpso_scenarios = [
        scenario_from_mat(args.wind_root, name, f"WS{index}")
        for index, name in enumerate(
            [
                "3speed_12direction.mat",
                "3speed_12direction_uniform.mat",
                "4speed_12direction.mat",
                "6speed_12direction.mat",
            ],
            start=1,
        )
    ]
    write_contract(
        "cgpso_native_16_cases.json",
        problem_id="cgpso_complex_large_16",
        semantics_id="cgpso_complex_large_16_v1",
        doi="10.1109/jas.2023.123387",
        fidelity="P1_paper_and_local_source_arrays",
        claim_boundary=(
            "Paper-native 21x21 grid, four complex wind arrays, and "
            "N40/60/80/100; no land-use masks."
        ),
        physical_budget=24000,
        cases=[
            make_case(
                case_id=f"{scenario.name}_N{turbines}",
                semantics_id="cgpso_complex_large_16_v1",
                scenario=scenario,
                rows=21,
                cols=21,
                turbine_count=turbines,
                cell_width=231.0,
            )
            for scenario in cgpso_scenarios
            for turbines in [40, 60, 80, 100]
        ],
    )

    write_contract(
        "ciga_native_declared_4_cases.json",
        problem_id="ciga_native_declared_4",
        semantics_id="ciga_native_declared_4_p3_v1",
        doi="10.1145/3766671.3766786",
        fidelity="P3_declared_reconstruction",
        claim_boundary=(
            "The paper fixes 12x12, N15, and four wind conditions but does "
            "not expose machine-readable land masks. L0 is used explicitly; "
            "this is not the unavailable original constrained data."
        ),
        physical_budget=24000,
        cases=[
            make_case(
                case_id=f"{scenario.name}_N15_NA0",
                semantics_id="ciga_native_declared_4_p3_v1",
                scenario=scenario,
                rows=12,
                cols=12,
                turbine_count=15,
                cell_width=231.0,
            )
            for scenario in [ws1, ws2, ws3, ws4]
        ],
    )

    lsde_scenarios = [
        scenario_from_mat(args.wind_root, f"13speed_{directions}direction.mat",
                          f"WS{index}")
        for index, directions in enumerate([4, 5, 6, 7], start=1)
    ]
    write_contract(
        "lsde_native_declared_12_cases.json",
        problem_id="lsde_large_declared_12",
        semantics_id="lsde_large_declared_12_p3_v1",
        doi="10.1049/cit2.70150",
        fidelity="P3_same_lineage_array_reconstruction",
        claim_boundary=(
            "Paper-declared 15x15, N30/50/100 and 4-7-direction family. "
            "Same-lineage hashed wind arrays fill the unpublished originals; "
            "results must be labeled reconstruction."
        ),
        physical_budget=20000,
        cases=[
            make_case(
                case_id=f"{scenario.name}_N{turbines}",
                semantics_id="lsde_large_declared_12_p3_v1",
                scenario=scenario,
                rows=15,
                cols=15,
                turbine_count=turbines,
                cell_width=231.0,
            )
            for scenario in lsde_scenarios
            for turbines in [30, 50, 100]
        ],
    )

    wfadde_scenarios = [
        scenario_from_mat(args.wind_root, f"13speed_{directions}direction.mat",
                          f"WS{directions}")
        for directions in range(1, 9)
    ]
    write_contract(
        "wfadde_native_declared_24_cases.json",
        problem_id="wfadde_native_declared_24",
        semantics_id="wfadde_native_declared_24_p3_v1",
        doi="10.2139/ssrn.6135326",
        fidelity="P3_preprint_and_same_lineage_array_reconstruction",
        claim_boundary=(
            "Preprint-declared eight wind conditions x N30/50/80. Hashed "
            "same-lineage 1-8-direction arrays fill missing author files."
        ),
        physical_budget=24000,
        cases=[
            make_case(
                case_id=f"{scenario.name}_N{turbines}",
                semantics_id="wfadde_native_declared_24_p3_v1",
                scenario=scenario,
                rows=12,
                cols=12,
                turbine_count=turbines,
                cell_width=231.0,
            )
            for scenario in wfadde_scenarios
            for turbines in [30, 50, 80]
        ],
    )

    msshade_scenarios = [
        scenario_from_mat(args.wind_root, f"13speed_{directions}direction.mat",
                          f"WS{index}")
        for index, directions in enumerate([2, 3, 4, 5], start=1)
    ]
    write_contract(
        "msshade_native_declared_16_cases.json",
        problem_id="msshade_native_declared_16",
        semantics_id="msshade_native_declared_16_p3_v1",
        doi="10.3390/electronics13163196",
        fidelity="P3_paper_and_same_lineage_array_reconstruction",
        claim_boundary=(
            "Paper-declared random Weibull 2/3/4/5-direction WS1-WS4 x "
            "N20/30/40/50. Hashed same-lineage arrays reconstruct the "
            "unpublished random samples."
        ),
        physical_budget=24000,
        cases=[
            make_case(
                case_id=f"{scenario.name}_N{turbines}",
                semantics_id="msshade_native_declared_16_p3_v1",
                scenario=scenario,
                rows=12,
                cols=12,
                turbine_count=turbines,
                cell_width=231.0,
            )
            for scenario in msshade_scenarios
            for turbines in [20, 30, 40, 50]
        ],
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
