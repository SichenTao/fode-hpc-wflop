#!/usr/bin/env python3
"""Validate one formal T46 result pair without interpreting performance."""

import argparse
import json
import math
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--front", required=True)
parser.add_argument("--summary", required=True)
parser.add_argument("--algorithm", required=True)
parser.add_argument("--scenario", choices=("ws1", "ws2"), required=True)
parser.add_argument("--turbines", type=int, required=True)
parser.add_argument("--seed", type=int, required=True)
args = parser.parse_args()

front = json.loads(Path(args.front).read_text())
summary = json.loads(Path(args.summary).read_text())
expected = {
    "algorithm": args.algorithm,
    "scenario": args.scenario,
    "turbines": args.turbines,
    "seed": args.seed,
    "complete_layout_evaluations": 10100,
}
for key, value in expected.items():
    if front.get(key) != value:
        raise SystemExit(f"front {key}: {front.get(key)!r} != {value!r}")
    if summary.get(key) != value:
        raise SystemExit(f"summary {key}: {summary.get(key)!r} != {value!r}")
solutions = front.get("solutions")
if not isinstance(solutions, list) or not solutions:
    raise SystemExit("formal front is empty")
if summary.get("nondominated_count") != len(solutions):
    raise SystemExit("summary/front nondominated count mismatch")
seen = set()
for solution in solutions:
    objectives = solution.get("objectives")
    layout = solution.get("layout")
    if not isinstance(objectives, list) or len(objectives) != 3:
        raise SystemExit("invalid objective triple")
    if not all(isinstance(value, (int, float)) and math.isfinite(value)
               for value in objectives):
        raise SystemExit("non-finite objective")
    if not isinstance(layout, list) or len(layout) != args.turbines:
        raise SystemExit("invalid layout cardinality")
    if len(set(layout)) != args.turbines:
        raise SystemExit("duplicate turbine cell")
    if min(layout) < 1 or max(layout) > 400:
        raise SystemExit("layout cell outside 20-by-20 grid")
    key = tuple(sorted(layout))
    if key in seen:
        raise SystemExit("duplicate nondominated layout")
    seen.add(key)
for key in ("evaluator_seconds", "algorithm_seconds", "end_to_end_seconds"):
    value = summary.get(key)
    if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
        raise SystemExit(f"invalid timing {key}")
print("pbea_formal_run_valid")
