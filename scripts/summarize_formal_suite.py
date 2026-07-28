#!/usr/bin/env python3
"""Create separate, auditable descriptive summaries for all formal campaigns."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import statistics
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
SUITE_CONTRACT = ROOT / "formal/contracts/waffle_campaign_suite_v1.json"
CAMPAIGNS = {
    "common": {
        "contract": ROOT / "formal/contracts/eighteen_algorithm_cpp_hpc_waffle_v1.json",
        "results": "eighteen_algorithm_cpp_hpc_waffle_v1",
        "receipt": "campaign_receipt.json",
    },
    "bde": {
        "contract": ROOT / "formal/contracts/bde_source_replay_waffle_v1.json",
        "results": "bde_source_replay_waffle_v1",
        "receipt": "campaign_receipt.json",
    },
    "pbea": {
        "contract": ROOT / "formal/contracts/pbea_six_algorithm_waffle_v1.json",
        "results": "pbea_six_algorithm_waffle_v1",
        "receipt": "campaign_file_receipt.json",
    },
    "offshore": {
        "contract": ROOT / "formal/contracts/offshore_cpp_hpc_waffle_v1.json",
        "results": "offshore_cpp_hpc_waffle_v1",
        "receipt": "campaign_receipt.json",
    },
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def read_jsonl(paths: Iterable[Path]) -> list[dict[str, Any]]:
    records = []
    for path in sorted(paths):
        for line_number, line in enumerate(path.read_text().splitlines(), 1):
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as error:
                raise RuntimeError(
                    f"{path}:{line_number}: invalid JSON: {error}"
                ) from error
    return records


def numeric_stats(values: Iterable[float]) -> dict[str, float | int]:
    data = [float(value) for value in values]
    if not data or not all(math.isfinite(value) for value in data):
        raise RuntimeError("descriptive statistic received empty/nonfinite data")
    return {
        "n": len(data),
        "mean": statistics.fmean(data),
        "sample_std": statistics.stdev(data) if len(data) > 1 else 0.0,
        "median": statistics.median(data),
        "minimum": min(data),
        "maximum": max(data),
    }


def optional_numeric_stats(
    values: Iterable[float | int | None],
) -> dict[str, float | int] | None:
    data = [float(value) for value in values if value is not None]
    return numeric_stats(data) if data else None


def flatten_stats(
    target: dict[str, Any],
    prefix: str,
    summary: dict[str, float | int] | None,
) -> None:
    for suffix in ("mean", "sample_std", "median", "minimum", "maximum"):
        target[f"{prefix}_{suffix}"] = (
            None if summary is None else summary[suffix]
        )


def average_tied_ranks(
    values: dict[str, float],
    *,
    higher_is_better: bool,
) -> dict[str, float]:
    ordered = sorted(
        values.items(),
        key=lambda item: (
            -item[1] if higher_is_better else item[1],
            item[0],
        ),
    )
    result: dict[str, float] = {}
    index = 0
    while index < len(ordered):
        end = index + 1
        while end < len(ordered) and ordered[end][1] == ordered[index][1]:
            end += 1
        rank = (index + 1 + end) / 2.0
        for key, _ in ordered[index:end]:
            result[key] = rank
        index = end
    return result


def write_csv_atomic(
    path: Path,
    rows: list[dict[str, Any]],
    fieldnames: list[str],
) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(path)


def common_rows(
    records: list[dict[str, Any]],
    *,
    expected_repeats: int,
    expected_groups: int,
    campaign_id: str,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        grouped[(record["algorithm_id"], record["case_id"])].append(record)
    if len(grouped) != expected_groups:
        raise RuntimeError(
            f"{campaign_id}: groups={len(grouped)} expected={expected_groups}"
        )
    rows = []
    case_medians: dict[str, dict[str, float]] = defaultdict(dict)
    for (algorithm, case_id), group in sorted(grouped.items()):
        if len(group) != expected_repeats:
            raise RuntimeError(
                f"{campaign_id}/{algorithm}/{case_id}: "
                f"repeats={len(group)} expected={expected_repeats}"
            )
        objectives = numeric_stats(
            record["best_expected_power_kw"] for record in group
        )
        end_to_end = numeric_stats(
            record["timing_seconds"]["end_to_end"] for record in group
        )
        evaluator = numeric_stats(
            record["timing_seconds"]["evaluator"] for record in group
        )
        algorithm_time = numeric_stats(
            record["timing_seconds"]["algorithm"] for record in group
        )
        row: dict[str, Any] = {
            "campaign_id": campaign_id,
            "algorithm_id": algorithm,
            "case_id": case_id,
            "repeats": len(group),
            "physical_fes_per_run": group[0]["physical_fes"],
        }
        flatten_stats(row, "best_expected_power_kw", objectives)
        flatten_stats(row, "end_to_end_seconds", end_to_end)
        flatten_stats(row, "evaluator_seconds", evaluator)
        flatten_stats(row, "algorithm_seconds", algorithm_time)
        rows.append(row)
        case_medians[case_id][algorithm] = float(objectives["median"])

    rank_values: dict[str, list[float]] = defaultdict(list)
    wins: dict[str, int] = defaultdict(int)
    for medians in case_medians.values():
        ranks = average_tied_ranks(medians, higher_is_better=True)
        for algorithm, rank in ranks.items():
            rank_values[algorithm].append(rank)
            if rank == 1.0:
                wins[algorithm] += 1
    rank_rows = []
    for algorithm, values in sorted(rank_values.items()):
        rank_rows.append(
            {
                "campaign_id": campaign_id,
                "algorithm_id": algorithm,
                "cases": len(values),
                "mean_rank": statistics.fmean(values),
                "median_rank": statistics.median(values),
                "rank_one_cases": wins[algorithm],
            }
        )
    rank_rows.sort(key=lambda row: (row["mean_rank"], row["algorithm_id"]))
    return rows, rank_rows


def pbea_rows(
    result_dir: Path,
    *,
    expected_repeats: int,
    expected_groups: int,
    campaign_id: str,
) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str, int], list[dict[str, Any]]] = defaultdict(list)
    for path in sorted(result_dir.glob("*.summary.json")):
        record = read_json(path)
        grouped[
            (
                record["algorithm"],
                record["scenario"],
                int(record["turbines"]),
            )
        ].append(record)
    if len(grouped) != expected_groups:
        raise RuntimeError(
            f"{campaign_id}: groups={len(grouped)} expected={expected_groups}"
        )
    rows = []
    metrics = (
        "nondominated_count",
        "minimum_inverse_power",
        "minimum_land_area_grid_units",
        "minimum_total_cost",
        "end_to_end_seconds",
        "evaluator_seconds",
        "algorithm_seconds",
    )
    for (algorithm, scenario, turbines), group in sorted(grouped.items()):
        if len(group) != expected_repeats:
            raise RuntimeError(
                f"{campaign_id}/{algorithm}/{scenario}/tn{turbines}: "
                f"repeats={len(group)} expected={expected_repeats}"
            )
        row: dict[str, Any] = {
            "campaign_id": campaign_id,
            "algorithm_id": algorithm,
            "scenario": scenario,
            "turbines": turbines,
            "repeats": len(group),
            "complete_layout_evaluations_per_run": group[0][
                "complete_layout_evaluations"
            ],
        }
        for metric in metrics:
            flatten_stats(
                row,
                metric,
                numeric_stats(record[metric] for record in group),
            )
        rows.append(row)
    return rows


def offshore_rows(
    result_dir: Path,
    *,
    expected_repeats: int,
    expected_groups: int,
    campaign_id: str,
) -> list[dict[str, Any]]:
    ignored = {"environment.json", "campaign_receipt.json"}
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = defaultdict(list)
    for path in sorted(result_dir.glob("*.json")):
        if path.name in ignored:
            continue
        record = read_json(path)
        grouped[(record["algorithm_id"], record["case_id"])].append(record)
    if len(grouped) != expected_groups:
        raise RuntimeError(
            f"{campaign_id}: groups={len(grouped)} expected={expected_groups}"
        )
    rows = []
    for (algorithm, case_id), group in sorted(grouped.items()):
        if len(group) != expected_repeats:
            raise RuntimeError(
                f"{campaign_id}/{algorithm}/{case_id}: "
                f"repeats={len(group)} expected={expected_repeats}"
            )
        row: dict[str, Any] = {
            "campaign_id": campaign_id,
            "algorithm_id": algorithm,
            "case_id": case_id,
            "repeats": len(group),
            "physical_fes_per_run": group[0]["physical_fes"],
        }
        for metric in (
            "best_aep_kwh",
            "best_capacity_factor",
            "best_lcoe",
            "best_cable_cost",
            "nondominated_count",
        ):
            flatten_stats(
                row,
                metric,
                optional_numeric_stats(record.get(metric) for record in group),
            )
        for timing in ("end_to_end", "evaluator", "algorithm"):
            flatten_stats(
                row,
                f"{timing}_seconds",
                numeric_stats(
                    record["timing_seconds"][timing] for record in group
                ),
            )
        rows.append(row)
    return rows


def self_test() -> int:
    summary = numeric_stats([1.0, 2.0, 3.0])
    if summary["mean"] != 2.0 or summary["median"] != 2.0:
        raise RuntimeError("numeric_stats self-test failed")
    ranks = average_tied_ranks(
        {"a": 4.0, "b": 3.0, "c": 3.0, "d": 1.0},
        higher_is_better=True,
    )
    if ranks != {"a": 1.0, "b": 2.5, "c": 2.5, "d": 4.0}:
        raise RuntimeError("average_tied_ranks self-test failed")
    common_fixture = []
    for algorithm, offset in (("a", 10.0), ("b", 5.0)):
        for case_id in ("c1", "c2"):
            for repeat in range(2):
                common_fixture.append(
                    {
                        "algorithm_id": algorithm,
                        "case_id": case_id,
                        "physical_fes": 10,
                        "best_expected_power_kw": offset + repeat,
                        "timing_seconds": {
                            "end_to_end": 3.0 + repeat,
                            "evaluator": 2.0 + repeat,
                            "algorithm": 1.0,
                        },
                    }
                )
    common_summary, rank_summary = common_rows(
        common_fixture,
        expected_repeats=2,
        expected_groups=4,
        campaign_id="fixture",
    )
    if len(common_summary) != 4 or rank_summary[0]["algorithm_id"] != "a":
        raise RuntimeError("common_rows self-test failed")
    with tempfile.TemporaryDirectory(prefix="formal-summary-self-test-") as tmp:
        temporary = Path(tmp)
        pbea_fixture = temporary / "pbea"
        offshore_fixture = temporary / "offshore"
        pbea_fixture.mkdir()
        offshore_fixture.mkdir()
        for algorithm in ("a", "b"):
            for repeat in range(2):
                payload = {
                    "algorithm": algorithm,
                    "scenario": "ws1",
                    "turbines": 15,
                    "complete_layout_evaluations": 10,
                    "nondominated_count": 3 + repeat,
                    "minimum_inverse_power": 1.0,
                    "minimum_land_area_grid_units": 2.0,
                    "minimum_total_cost": 3.0,
                    "end_to_end_seconds": 4.0,
                    "evaluator_seconds": 3.0,
                    "algorithm_seconds": 1.0,
                }
                (pbea_fixture / f"{algorithm}-{repeat}.summary.json").write_text(
                    json.dumps(payload)
                )
        if len(pbea_rows(
            pbea_fixture,
            expected_repeats=2,
            expected_groups=2,
            campaign_id="fixture",
        )) != 2:
            raise RuntimeError("pbea_rows self-test failed")
        for algorithm in ("gga", "geoga"):
            for repeat in range(2):
                payload = {
                    "algorithm_id": algorithm,
                    "case_id": "site",
                    "physical_fes": 10,
                    "best_aep_kwh": 100.0,
                    "best_capacity_factor": 0.4,
                    "best_lcoe": 2.0 if algorithm == "gga" else None,
                    "best_cable_cost": 4.0,
                    "nondominated_count": 0,
                    "timing_seconds": {
                        "end_to_end": 4.0,
                        "evaluator": 3.0,
                        "algorithm": 1.0,
                    },
                }
                (offshore_fixture / f"{algorithm}-{repeat}.json").write_text(
                    json.dumps(payload)
                )
        if len(offshore_rows(
            offshore_fixture,
            expected_repeats=2,
            expected_groups=2,
            campaign_id="fixture",
        )) != 2:
            raise RuntimeError("offshore_rows self-test failed")
    print("formal_suite_summary_self_test_pass")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, default=ROOT / "results")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / "results/waffle_campaign_suite_v1/analysis",
    )
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        return self_test()

    suite = read_json(SUITE_CONTRACT)
    current_head = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    environments: dict[str, dict[str, Any]] = {}
    receipts: dict[str, dict[str, Any]] = {}
    contracts: dict[str, dict[str, Any]] = {}
    receipt_hashes: dict[str, str] = {}
    for key, paths in CAMPAIGNS.items():
        contract = read_json(paths["contract"])
        result_dir = arguments.results_root / paths["results"]
        environment_path = result_dir / "environment.json"
        receipt_path = result_dir / paths["receipt"]
        if not environment_path.is_file() or not receipt_path.is_file():
            raise RuntimeError(f"{key}: formal environment/receipt is missing")
        environment = read_json(environment_path)
        receipt = read_json(receipt_path)
        if environment["git_head"] != current_head:
            raise RuntimeError(f"{key}: result HEAD differs from analysis HEAD")
        if "waffle" not in environment["host"].lower():
            raise RuntimeError(f"{key}: formal host is not Waffle")
        if int(environment["workers"]) != int(environment["nproc"]):
            raise RuntimeError(f"{key}: worker policy did not use all nproc")
        if receipt.get("status") != "complete_file_matrix":
            raise RuntimeError(f"{key}: campaign receipt is not complete")
        if int(receipt["formal_runs"]) != int(contract["formal_run_count"]):
            raise RuntimeError(f"{key}: receipt/contract run counts differ")
        if int(receipt["complete_layout_evaluations"]) != int(
            contract["formal_complete_layout_evaluations"]
        ):
            raise RuntimeError(f"{key}: receipt/contract FES counts differ")
        environments[key] = environment
        receipts[key] = receipt
        contracts[key] = contract
        receipt_hashes[key] = sha256(receipt_path)

    common_records = read_jsonl(
        (arguments.results_root / CAMPAIGNS["common"]["results"]).glob(
            "seed_*.jsonl"
        )
    )
    common, common_ranks = common_rows(
        common_records,
        expected_repeats=len(contracts["common"]["seeds"]),
        expected_groups=(
            len(contracts["common"]["algorithms"])
            * int(contracts["common"]["cases"])
        ),
        campaign_id=contracts["common"]["campaign_id"],
    )
    bde_records = read_jsonl(
        (arguments.results_root / CAMPAIGNS["bde"]["results"]).glob(
            "bde__seed*.jsonl"
        )
    )
    bde, bde_ranks = common_rows(
        bde_records,
        expected_repeats=len(contracts["bde"]["seeds"]),
        expected_groups=int(contracts["bde"]["case_axes"]["case_count"]),
        campaign_id=contracts["bde"]["campaign_id"],
    )
    pbea = pbea_rows(
        arguments.results_root / CAMPAIGNS["pbea"]["results"],
        expected_repeats=int(contracts["pbea"]["repeat_count"]),
        expected_groups=(
            len(contracts["pbea"]["algorithms"])
            * len(contracts["pbea"]["wind_scenarios"])
            * len(contracts["pbea"]["turbine_counts"])
        ),
        campaign_id=contracts["pbea"]["campaign_id"],
    )
    offshore = offshore_rows(
        arguments.results_root / CAMPAIGNS["offshore"]["results"],
        expected_repeats=int(contracts["offshore"]["repeat_count"]),
        expected_groups=sum(
            len(contracts["offshore"]["cases"])
            if profile["cases"] == "all"
            else len(profile["cases"])
            for profile in contracts["offshore"]["profiles"]
        ),
        campaign_id=contracts["offshore"]["campaign_id"],
    )

    observed_runs = (
        len(common_records)
        + len(bde_records)
        + sum(row["repeats"] for row in pbea)
        + sum(row["repeats"] for row in offshore)
    )
    if observed_runs != int(suite["total_optimization_runs"]):
        raise RuntimeError(
            f"suite runs={observed_runs}, "
            f"expected={suite['total_optimization_runs']}"
        )

    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    output_rows = {
        "common_single_objective.csv": common,
        "common_algorithm_ranks.csv": common_ranks,
        "bde_source_replay_single_objective.csv": bde,
        "bde_source_replay_algorithm_ranks.csv": bde_ranks,
        "three_objective_descriptive.csv": pbea,
        "offshore_descriptive.csv": offshore,
    }
    output_paths = []
    for name, rows in output_rows.items():
        if not rows:
            raise RuntimeError(f"{name}: no summary rows")
        path = arguments.output_dir / name
        write_csv_atomic(path, rows, list(rows[0]))
        output_paths.append(path)

    suite_summary = {
        "schema_version": 1,
        "suite_id": suite["suite_id"],
        "git_head": current_head,
        "formal_host": "Waffle",
        "workers": {
            key: environment["workers"]
            for key, environment in environments.items()
        },
        "campaigns": {
            "common": {
                "runs": len(common_records),
                "descriptive_groups": len(common),
                "rank_rows": len(common_ranks),
            },
            "bde_source_replay": {
                "runs": len(bde_records),
                "descriptive_groups": len(bde),
                "rank_rows": len(bde_ranks),
            },
            "three_objective": {
                "runs": sum(row["repeats"] for row in pbea),
                "descriptive_groups": len(pbea),
                "quality_boundary": (
                    "No HV or IGD is computed without a separately frozen "
                    "reference front and normalization contract."
                ),
            },
            "offshore": {
                "runs": sum(row["repeats"] for row in offshore),
                "descriptive_groups": len(offshore),
                "quality_boundary": (
                    "Scalar and multiobjective profiles remain separated."
                ),
            },
        },
        "total_optimization_runs": observed_runs,
        "total_complete_layout_evaluations": suite[
            "total_complete_layout_evaluations"
        ],
        "claim_boundary": (
            "These are within-profile descriptive summaries. Cross-problem "
            "objective values and provenance levels are not pooled."
        ),
    }
    suite_summary_path = arguments.output_dir / "formal_suite_summary.json"
    temporary = suite_summary_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(suite_summary, indent=2) + "\n")
    temporary.replace(suite_summary_path)
    output_paths.append(suite_summary_path)

    output_hashes = {
        path.name: sha256(path)
        for path in sorted(output_paths)
    }
    analysis_receipt = {
        "schema_version": 1,
        "suite_id": suite["suite_id"],
        "git_head": current_head,
        "campaign_receipt_sha256": receipt_hashes,
        "outputs_sha256": output_hashes,
        "status": "complete_descriptive_analysis",
        "evidence_boundary": (
            "Descriptive statistics and common-problem median ranks only. "
            "Inferential tests and multiobjective indicators require their "
            "own frozen hypotheses, multiplicity, reference-front, and "
            "normalization contracts."
        ),
    }
    receipt_path = arguments.output_dir / "analysis_receipt.json"
    temporary = receipt_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(analysis_receipt, indent=2) + "\n")
    temporary.replace(receipt_path)
    print(
        "formal_suite_summary_pass "
        f"runs={observed_runs} outputs={len(output_paths)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
