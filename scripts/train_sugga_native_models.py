#!/usr/bin/env python3
"""Train case-specific SUGGA response surfaces from physical Monte Carlo.

The paired C++ executable performs all complete-layout evaluations and emits
fixed-order cell-response tables.  This driver fits the paper's RBF SVR and
freezes models in the native C++ loader format.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import numpy as np
from sklearn.svm import SVR


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fit_model(response: Path, output: Path, gamma: float) -> dict[str, object]:
    with response.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    features = np.asarray(
        [[float(row["x"]), float(row["y"])] for row in rows],
        dtype=np.float64,
    )
    targets = np.asarray(
        [float(row["mean_power_kw"]) for row in rows], dtype=np.float64
    )
    model = SVR(kernel="rbf", gamma=gamma, C=1.0, epsilon=0.1)
    model.fit(features, targets)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as handle:
        handle.write(
            f"{model.support_vectors_.shape[0]}\t{gamma:.17g}\t"
            f"{float(model.intercept_[0]):.17g}\n"
        )
        coefficients = model.dual_coef_[0]
        for vector, coefficient in zip(
            model.support_vectors_, coefficients, strict=True
        ):
            handle.write(
                f"{float(vector[0]):.17g}\t{float(vector[1]):.17g}\t"
                f"{float(coefficient):.17g}\n"
            )
    return {
        "case_id": response.stem.removesuffix(".mc"),
        "support_vector_count": int(model.support_vectors_.shape[0]),
        "gamma": gamma,
        "C": 1.0,
        "epsilon": 0.1,
        "response_sha256": sha256(response),
        "model_sha256": sha256(output),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--cases", type=Path, nargs="+", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--samples", type=int, default=10000)
    parser.add_argument("--workers", type=int, default=0)
    parser.add_argument("--seed", type=int, default=2019060820260730)
    parser.add_argument("--gamma", type=float, default=0.3)
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    total_fes = 0
    contract_records: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="sugga-native-mc-") as temporary:
        response_root = Path(temporary)
        for contract_index, contract in enumerate(args.cases):
            contract_payload = json.loads(contract.read_text(encoding="utf-8"))
            contract_response = response_root / f"contract-{contract_index}"
            completed = subprocess.run(
                [
                    str(args.generator),
                    "--cases",
                    str(contract),
                    "--output-dir",
                    str(contract_response),
                    "--samples",
                    str(args.samples),
                    "--seed",
                    str(args.seed),
                    "--workers",
                    str(args.workers),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            generator_receipt = json.loads(completed.stdout)
            expected_cases = int(contract_payload["case_count"])
            problem_id = str(contract_payload["problem_id"])
            if int(generator_receipt["case_count"]) != expected_cases:
                raise RuntimeError(f"{contract}: generator case count mismatch")
            total_fes += int(generator_receipt["training_physical_fes"])
            for case in contract_payload["cases"]:
                case_id = case["case_id"]
                response = contract_response / f"{case_id}.mc.tsv"
                output = (
                    args.output_dir / problem_id / f"{case_id}.svr.tsv"
                )
                record = fit_model(response, output, args.gamma)
                record["problem_id"] = problem_id
                record["problem_semantics_id"] = case["semantics_id"]
                records.append(record)
            contract_records.append(
                {
                    "path": str(contract),
                    "problem_id": problem_id,
                    "sha256": sha256(contract),
                    "case_count": expected_cases,
                    "training_physical_fes": int(
                        generator_receipt["training_physical_fes"]
                    ),
                }
            )

    records.sort(key=lambda row: str(row["case_id"]))
    receipt = {
        "schema_version": 1,
        "training_id": "sugga_native_train_from_scratch_v1",
        "paper_doi": "10.1016/j.enconman.2019.06.082",
        "samples_per_case": args.samples,
        "sampler_seed": args.seed,
        "svr": {
            "kernel": "rbf",
            "gamma": args.gamma,
            "C": 1.0,
            "epsilon": 0.1,
        },
        "contracts": contract_records,
        "case_count": len(records),
        "training_physical_fes": total_fes,
        "models": records,
        "claim_boundary": (
            "Paper-guided deterministic retraining; not unavailable author "
            "response-surface identity."
        ),
    }
    receipt_path = args.output_dir / "training_receipt.json"
    receipt_path.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "sugga_native_training_pass "
        f"cases={len(records)} training_physical_fes={total_fes} "
        f"receipt_sha256={sha256(receipt_path)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
