#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T48 cost and printed-anchor validator
Paper/DOI: An Analytical Framework for Offshore Wind Farm Layout Optimization;
10.1260/030952407780811401
Public source: none located
Missing/reconstruction/claim boundary:
hpc/core99_cpp/include/core99/lackner_t48.hpp
The cost oracle is independent; energy validation uses multiple printed paper
anchors because the underlying wind and power arrays are absent.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


def call(binary: str, variables: list[float]) -> dict:
    completed = subprocess.run(
        [
            binary,
            "--mode",
            "evaluate",
            "--variables",
            ",".join(str(value) for value in variables),
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def capital_cost(variables: list[float]) -> float:
    x1, y1, x2, y2 = variables
    cable = min(math.hypot(x1, y1), math.hypot(x2, y2))
    cable += math.hypot(x1 - x2, y1 - y2)
    return 2 * 700_000 + 2 * 600_000 + 100 * (x1 + x2) + 620 * cable


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    initial_variables = [9109.0, -2013.0, 3273.0, 1614.0]
    example_variables = [1800.0, 100.0, 1600.0, -100.0]
    final_variables = [1794.0, -320.0, 1613.0, -115.0]
    initial = call(args.binary, initial_variables)
    example = call(args.binary, example_variables)
    final = call(args.binary, final_variables)

    assert abs(initial["capital_cost_dollars"] - capital_cost(initial_variables)) < 1.0e-8
    assert abs(example["capital_cost_dollars"] - capital_cost(example_variables)) < 1.0e-8
    assert abs(final["capital_cost_dollars"] - capital_cost(final_variables)) < 1.0e-8
    assert abs(initial["lcoe_dollars_per_kwh"] - 0.105) < 0.01
    assert abs(example["capacity_factor"] - 0.405) < 0.015
    assert abs(example["wake_loss_fraction"] - 0.04) < 0.015
    assert abs(final["lcoe_dollars_per_kwh"] - 0.051) < 0.01
    assert abs(final["capital_cost_dollars"] - 4.5e6) < 0.75e6
    assert abs(final["capacity_factor"] - 0.415) < 0.02

    print(
        json.dumps(
            {
                "status": "pass",
                "initial_lcoe": initial["lcoe_dollars_per_kwh"],
                "example_capacity_factor": example["capacity_factor"],
                "example_wake_loss_fraction": example["wake_loss_fraction"],
                "paper_final_lcoe": final["lcoe_dollars_per_kwh"],
                "paper_final_capital": final["capital_cost_dollars"],
                "paper_final_capacity_factor": final["capacity_factor"],
            },
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
