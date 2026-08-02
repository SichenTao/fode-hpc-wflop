#!/usr/bin/env python3
"""WFLOP IMPLEMENTATION FACT DECLARATION

Implementation unit: admission checks for the ALGA attention reconstruction
Paper DOI: 10.1016/j.swevo.2025.102018
Public author code/data URL: unavailable, as recorded in docs/source-dossiers/T45.json
Related public source: https://github.com/zbh0528/WFLO-GGA at
6ce41326e6c1d3685a01e038baf6d1d07aa46126
Method evidence tier: M3_DECLARED_COMPLETION
Controlling contract: shared/contracts/alga_attention_declared_reconstruction_contract.json
Claim boundary: deterministic engineering reconstruction only; no author ALGA
or real-world 3D Guishan reproduction claim.
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path


ALGORITHM = "alga_attention_declared_reconstruction_v1"
SEMANTIC_FIELDS = [
    "algorithm_id",
    "method_id",
    "algorithm_provenance",
    "effective_semantics_id",
    "problem_id",
    "problem_semantics_id",
    "case_id",
    "seed",
    "physical_fes",
    "inference_physical_fes",
    "generations",
    "initial_population",
    "final_population",
    "best_expected_power_kw",
    "best_layout_1based",
    "learned_state_hash",
]


def run(
    binary: Path,
    cases: Path,
    problem: str,
    case_id: str,
    workers: int,
    output: Path,
    physical_fes: int = 480,
) -> dict:
    subprocess.run(
        [
            str(binary),
            "--algorithm",
            ALGORITHM,
            "--problem",
            problem,
            "--cases",
            str(cases),
            "--case",
            case_id,
            "--physical-fes",
            str(physical_fes),
            "--seed",
            "20260729",
            "--workers",
            str(workers),
            "--compute-backend",
            "cpu",
            "--output",
            str(output),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return json.loads(output.read_text(encoding="utf-8"))


def assert_feasible(result: dict, turbine_count: int, grid: int) -> None:
    layout = result["best_layout_1based"]
    if len(layout) != turbine_count:
        raise RuntimeError("ALGA reconstruction returned wrong layout length")
    if layout != sorted(layout) or len(layout) != len(set(layout)):
        raise RuntimeError("ALGA reconstruction returned duplicate/unsorted cells")
    if not all(1 <= cell <= grid for cell in layout):
        raise RuntimeError("ALGA reconstruction returned an out-of-grid cell")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--common-cases", type=Path, required=True)
    parser.add_argument("--transfer-cases", type=Path, required=True)
    parser.add_argument("--native-3d-cases", type=Path, required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="alga-m3-admission-") as temp:
        root = Path(temp)
        one = run(
            arguments.binary,
            arguments.native_3d_cases,
            "alga_guishan_3d_declared_proxy_v1",
            "ALGA_Guishan3D_SEASON1_tn40",
            1,
            root / "one.json",
        )
        twenty = run(
            arguments.binary,
            arguments.native_3d_cases,
            "alga_guishan_3d_declared_proxy_v1",
            "ALGA_Guishan3D_SEASON1_tn40",
            20,
            root / "twenty.json",
        )
        d80 = run(
            arguments.binary,
            arguments.common_cases,
            "fode_e0_common",
            "WS2tn80",
            20,
            root / "d80.json",
        )
        paper_generation_mapping = run(
            arguments.binary,
            arguments.native_3d_cases,
            "alga_guishan_3d_declared_proxy_v1",
            "ALGA_Guishan3D_SEASON1_tn40",
            20,
            root / "paper_generation_mapping.json",
            physical_fes=2430,
        )

    differing = [
        field for field in SEMANTIC_FIELDS if one[field] != twenty[field]
    ]
    if differing:
        raise RuntimeError(
            f"ALGA worker-count scientific state differs: {differing}"
        )
    if one["method_id"] != "ALGA_ATTENTION_DECLARED_RECONSTRUCTION_V1":
        raise RuntimeError("ALGA reconstruction identity is missing")
    if one["problem_id"] != "alga_guishan_3d_declared_proxy_v1":
        raise RuntimeError("primary ALGA 3D problem identity is missing")
    if one["physical_fes"] != 480 or one["inference_physical_fes"] != 480:
        raise RuntimeError("ALGA reconstruction did not stop at exact FES")
    if one["initial_population"] != 30 or one["final_population"] != 30:
        raise RuntimeError("ALGA paper population size is not preserved")
    if one["generations"] <= 0:
        raise RuntimeError("ALGA attention state was never updated")
    if not one["learned_state_hash"].startswith("fnv1a64:"):
        raise RuntimeError("ALGA learned-state hash is missing")
    assert_feasible(one, 40, 144)
    if d80["physical_fes"] != 480 or d80["generations"] <= 0:
        raise RuntimeError("80-turbine platform smoke did not search exactly")
    assert_feasible(d80, 80, 144)
    if (
        paper_generation_mapping["physical_fes"] != 2430
        or paper_generation_mapping["generations"] != 100
    ):
        raise RuntimeError(
            "paper generation mapping drifted: "
            "30 initialization plus 100 times 24 evaluated non-elites "
            "must equal 2430 complete-layout evaluations"
        )

    original = subprocess.run(
        [
            str(arguments.binary),
            "--algorithm",
            "alga",
            "--cases",
            str(arguments.common_cases),
            "--case",
            "WS1tn10",
            "--physical-fes",
            "60",
            "--workers",
            "1",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if original.returncode == 0 or "intentionally blocked" not in original.stderr:
        raise RuntimeError("original ALGA identifier was not kept blocked")

    gpu = subprocess.run(
        [
            str(arguments.binary),
            "--algorithm",
            ALGORITHM,
            "--compute-backend",
            "gpu",
            "--cases",
            str(arguments.common_cases),
            "--case",
            "WS1tn10",
            "--physical-fes",
            "60",
            "--workers",
            "1",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if (
        gpu.returncode == 0
        or "no hidden fallback" not in gpu.stderr
    ):
        raise RuntimeError("GPU compatibility interface did not fail closed")

    wrong_manifest = subprocess.run(
        [
            str(arguments.binary),
            "--algorithm",
            ALGORITHM,
            "--problem",
            "alga_guishan_3d_declared_proxy_v1",
            "--cases",
            str(arguments.common_cases),
            "--case",
            "WS2tn80",
            "--physical-fes",
            "480",
            "--workers",
            "1",
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if (
        wrong_manifest.returncode == 0
        or "manifest semantics do not match" not in wrong_manifest.stderr
    ):
        raise RuntimeError(
            "ALGA 3D problem accepted an arbitrary case manifest"
        )

    print(
        "alga_attention_reconstruction_pass "
        f"primary_hash={one['learned_state_hash']} "
        "workers=1,20 d80_fes=480 original=blocked gpu=fail_closed "
        "native_3d_manifest=frozen paper_100_generations_fes=2430"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
