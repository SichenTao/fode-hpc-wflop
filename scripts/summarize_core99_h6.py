#!/usr/bin/env python3
"""Build a public H6 admission receipt from raw Core-99 C++ observations."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import statistics
from datetime import UTC, datetime
from pathlib import Path


def load_group(root: Path, stem: str, workers: int, repeats: int) -> list[dict]:
    observations = []
    for repeat in range(1, repeats + 1):
        path = root / f"{stem}-w{workers}-r{repeat}.json"
        observations.append(json.loads(path.read_text(encoding="utf-8")))
    return observations


def summarize(observations: list[dict]) -> dict:
    hashes = sorted({item["scientific_hash"] for item in observations})
    objectives = sorted({item["best_objective"] for item in observations})
    physical_fes = sorted({item["physical_fes"] for item in observations})
    observed_workers = sorted({item["observed_workers"] for item in observations})
    if len(hashes) != 1 or len(objectives) != 1 or len(physical_fes) != 1:
        raise RuntimeError("repeated observations are not scientifically identical")
    return {
        "repeats": len(observations),
        "physical_fes": physical_fes[0],
        "observed_workers": observed_workers,
        "scientific_hash": hashes[0],
        "best_objective": objectives[0],
        "median_seconds": {
            "end_to_end": statistics.median(
                item["end_to_end_seconds"] for item in observations
            ),
            "evaluator": statistics.median(
                item["evaluator_seconds"] for item in observations
            ),
            "algorithm": statistics.median(
                item["algorithm_seconds"] for item in observations
            ),
        },
        "raw_observations": observations,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", type=Path, required=True)
    parser.add_argument("--stem", required=True)
    parser.add_argument("--serial-workers", type=int, default=1)
    parser.add_argument("--hpc-workers", type=int, default=20)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--host", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    serial = summarize(
        load_group(
            args.input_dir,
            args.stem,
            args.serial_workers,
            args.repeats,
        )
    )
    hpc = summarize(
        load_group(
            args.input_dir,
            args.stem,
            args.hpc_workers,
            args.repeats,
        )
    )
    if serial["scientific_hash"] != hpc["scientific_hash"]:
        raise RuntimeError("serial and HPC scientific hashes differ")
    if serial["best_objective"] != hpc["best_objective"]:
        raise RuntimeError("serial and HPC objectives differ")

    speedup = {
        key: serial["median_seconds"][key] / hpc["median_seconds"][key]
        for key in ("end_to_end", "evaluator", "algorithm")
    }
    receipt = {
        "schema_version": 1,
        "receipt_type": "core99_h6_bounded_admission",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "host": args.host,
        "platform": platform.platform(),
        "source_commit": args.source_commit,
        "stem": args.stem,
        "serial": serial,
        "hpc": hpc,
        "speedup_serial_to_hpc": speedup,
        "claim_boundary": (
            "bounded H6 admission only; not a paper-scale formal timing claim"
        ),
    }
    canonical = json.dumps(receipt, sort_keys=True).encode()
    receipt["receipt_sha256"] = hashlib.sha256(canonical).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "core99_h6_receipt_written "
        f"output={args.output} "
        f"end_to_end_speedup={speedup['end_to_end']:.6f} "
        f"evaluator_speedup={speedup['evaluator']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
