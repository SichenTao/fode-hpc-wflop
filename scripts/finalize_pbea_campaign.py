#!/usr/bin/env python3
"""Create a deterministic completeness and SHA-256 receipt for T46."""

import argparse
import hashlib
import json
from pathlib import Path


ALGORITHMS = ("moead_p", "moead", "nsgaii", "mopso", "morime", "armoea")
SCENARIOS = ("ws1", "ws2")
TURBINES = range(15, 31)
SEEDS = range(202507290001, 202507290026)

parser = argparse.ArgumentParser()
parser.add_argument("--results", required=True)
parser.add_argument("--receipt", required=True)
args = parser.parse_args()
root = Path(args.results)
files = []
missing = []
for algorithm in ALGORITHMS:
    for scenario in SCENARIOS:
        for turbines in TURBINES:
            for seed in SEEDS:
                stem = f"{algorithm}__{scenario}__tn{turbines}__seed{seed}"
                for suffix in (".front.json", ".summary.json"):
                    path = root / f"{stem}{suffix}"
                    if not path.is_file():
                        missing.append(path.name)
                    else:
                        files.append({
                            "path": path.relative_to(root).as_posix(),
                            "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        })
if missing:
    raise SystemExit(
        f"campaign incomplete: missing {len(missing)} files; first={missing[:5]}")
files.sort(key=lambda row: row["path"])
receipt = {
    "schema_version": 1,
    "campaign_id": "pbea_six_algorithm_waffle_v1",
    "formal_runs": 4800,
    "result_files": len(files),
    "complete_layout_evaluations": 48_480_000,
    "files": files,
    "status": "complete_file_matrix",
    "evidence_boundary": (
        "This receipt proves file completeness and byte identity. Statistical "
        "claims require the separately generated metric and inference receipt."
    ),
}
output = Path(args.receipt)
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_suffix(output.suffix + ".tmp")
temporary.write_text(json.dumps(receipt, indent=2) + "\n")
temporary.replace(output)
print(json.dumps({
    "formal_runs": receipt["formal_runs"],
    "result_files": receipt["result_files"],
    "complete_layout_evaluations": receipt["complete_layout_evaluations"],
}))
