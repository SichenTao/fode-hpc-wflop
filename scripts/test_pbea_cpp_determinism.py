#!/usr/bin/env python3
import argparse
import json
import subprocess


def run(binary, workers):
    value = json.loads(subprocess.check_output([
        binary, "--scenario", "ws1", "--turbines", "15",
        "--population", "20", "--generations", "3",
        "--seed", "20250729", "--ipd", "3", "--workers", str(workers)
    ], text=True))
    for key in ("workers", "evaluator_seconds", "algorithm_seconds",
                "end_to_end_seconds"):
        value.pop(key)
    return value


parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
args = parser.parse_args()
serial = run(args.binary, 1)
parallel = run(args.binary, 4)
if serial != parallel:
    raise SystemExit(f"worker-count semantic mismatch:\n{serial}\n{parallel}")
print(json.dumps({"serial_equals_parallel": True, "complete_layout_evaluations":
                  serial["complete_layout_evaluations"]}))
