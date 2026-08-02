#!/usr/bin/env python3
"""Measure T-MOEA R4 errors without changing its frozen oracle source."""

from __future__ import annotations

import json

import validate_tmoea_nysted_r4 as oracle


def main() -> int:
    maximum_absolute_error = 0.0
    maximum_scaled_absolute_error = 0.0
    original_close = oracle.close

    def tracking_close(
        observed: float, expected: float, label: str
    ) -> None:
        nonlocal maximum_absolute_error
        nonlocal maximum_scaled_absolute_error
        absolute_error = abs(observed - expected)
        scaled_error = absolute_error / max(1.0, abs(expected))
        maximum_absolute_error = max(
            maximum_absolute_error, absolute_error
        )
        maximum_scaled_absolute_error = max(
            maximum_scaled_absolute_error, scaled_error
        )
        original_close(observed, expected, label)

    oracle.close = tracking_close
    return_code = oracle.main()
    if return_code != 0:
        return return_code
    print(json.dumps({
        "status": "pass",
        "oracle": "scripts/validate_tmoea_nysted_r4.py",
        "maximum_absolute_error": maximum_absolute_error,
        "maximum_scaled_absolute_error": maximum_scaled_absolute_error,
        "scaled_tolerance": 2.0e-12,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
