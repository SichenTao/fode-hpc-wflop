#!/usr/bin/env python3
"""Test immediate H6 cross-worker science and learned-state fail-fast gates."""

from __future__ import annotations

import copy

from run_hpc_core_target_scaling import (
    require_immediate_cross_worker_science,
)


def expect_failure(
    row: dict[str, str],
    baseline: dict,
    observed: dict,
    fragment: str,
) -> None:
    try:
        require_immediate_cross_worker_science(row, baseline, observed)
    except RuntimeError as error:
        if fragment not in str(error):
            raise RuntimeError(
                f"unexpected fail-fast error: {error}"
            ) from error
        return
    raise RuntimeError(f"fail-fast mutation was admitted: {fragment}")


def observation(
    *,
    workers: int,
    science: str = "science-exact",
    state_hash: str = "state-exact",
) -> dict:
    return {
        "key": {"repetition": 1, "workers": workers},
        "scientific_output_sha256": science,
        "raw_result": {
            "learned_state_hash": state_hash,
            "numerical_state": {
                "available": True,
                "parameter_count": 850385,
                "l2_norm": 88.0,
                "linf_norm": 1.0,
                "weighted_checksum": 1258000.0,
            },
        },
    }


def main() -> int:
    # Repetition 1 begins at W=2 under balanced rotation. The first completed
    # worker is therefore a valid provisional baseline until W=1 arrives.
    taae = {"pair_id": "Y36__fixture", "corpus_id": "Y36"}
    baseline = observation(workers=2)
    later_w1 = observation(workers=1)
    later_w1["raw_result"]["numerical_state"][
        "weighted_checksum"
    ] += 2.0e-10
    require_immediate_cross_worker_science(taae, baseline, later_w1)

    scientific = copy.deepcopy(later_w1)
    scientific["scientific_output_sha256"] = "science-drift"
    expect_failure(
        taae,
        baseline,
        scientific,
        "fail-fast scientific output drift",
    )

    numerical = copy.deepcopy(later_w1)
    numerical["raw_result"]["numerical_state"][
        "weighted_checksum"
    ] += 1.0
    expect_failure(
        taae,
        baseline,
        numerical,
        "fail-fast TAAE numerical state weighted_checksum drift",
    )

    rlpso = {"pair_id": "T42__fixture", "corpus_id": "T42"}
    state = observation(workers=20, state_hash="state-drift")
    expect_failure(
        rlpso,
        observation(workers=2),
        state,
        "fail-fast learned-state hash drift",
    )
    print(
        "plan005_h6_fail_fast_pass balanced_rotation_non_w1_baseline=1 "
        "rejected_science=1 accepted_taae_tolerance=1 "
        "rejected_taae_numerical=1 rejected_learned_state=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
