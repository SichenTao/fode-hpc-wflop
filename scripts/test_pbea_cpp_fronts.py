#!/usr/bin/env python3
"""Validate atomic non-dominated front artifacts for every T46 algorithm."""

import argparse
import json
import pathlib
import subprocess
import tempfile


ALGORITHMS = ("moead_p", "moead", "nsgaii", "mopso", "morime", "armoea")


parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
args = parser.parse_args()
validated = {}
with tempfile.TemporaryDirectory(prefix="pbea-front-test-") as directory:
    root = pathlib.Path(directory)
    for algorithm in ALGORITHMS:
        output = root / f"{algorithm}.json"
        subprocess.check_call([
            args.binary, "--algorithm", algorithm, "--scenario", "ws1",
            "--turbines", "15", "--population", "20", "--generations", "1",
            "--workers", "4", "--seed", "20250729",
            "--output-front", str(output),
        ], stdout=subprocess.DEVNULL)
        artifact = json.loads(output.read_text())
        if artifact["algorithm"] != algorithm:
            raise SystemExit(f"{algorithm}: artifact identity mismatch")
        if artifact["complete_layout_evaluations"] != 40:
            raise SystemExit(f"{algorithm}: physical-FES mismatch")
        solutions = artifact["solutions"]
        if not solutions:
            raise SystemExit(f"{algorithm}: empty nondominated set")
        for solution in solutions:
            layout = solution["layout"]
            if len(layout) != 15 or len(set(layout)) != 15:
                raise SystemExit(f"{algorithm}: invalid layout cardinality")
            if min(layout) < 1 or max(layout) > 400:
                raise SystemExit(f"{algorithm}: cell outside grid")
            replay = json.loads(subprocess.check_output([
                args.binary, "--scenario", "ws1", "--evaluate-layout",
                ",".join(map(str, layout)),
            ], text=True))
            actual = solution["objectives"]
            expected = [
                replay["inverse_power"], replay["land_area_grid_units"],
                replay["total_cost"],
            ]
            for left, right in zip(actual, expected):
                if abs(left-right) > 2e-12*max(1.0, abs(right)):
                    raise SystemExit(
                        f"{algorithm}: stored objective does not replay")
        validated[algorithm] = len(solutions)
print(json.dumps({"validated_nondominated_solutions": validated}))
