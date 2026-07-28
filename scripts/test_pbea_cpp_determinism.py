#!/usr/bin/env python3
import argparse
import json
import subprocess


ALGORITHMS = ("moead_p", "moead", "nsgaii", "mopso", "morime", "armoea")


def run(binary, algorithm, workers):
    value = json.loads(subprocess.check_output([
        binary, "--algorithm", algorithm,
        "--scenario", "ws1", "--turbines", "15",
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
results = {}
for algorithm in ALGORITHMS:
    serial = run(args.binary, algorithm, 1)
    parallel = run(args.binary, algorithm, 4)
    if serial != parallel:
        raise SystemExit(
            f"{algorithm} worker-count semantic mismatch:\n"
            f"{serial}\n{parallel}")
    results[algorithm] = serial["complete_layout_evaluations"]
print(json.dumps({"serial_equals_parallel": True, "algorithms": results}))
