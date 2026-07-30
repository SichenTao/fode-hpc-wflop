#!/usr/bin/env python3
"""Bounded fixture tests for Plan-005 formal command and resume contracts."""

from __future__ import annotations

import copy
import json
import os
import tempfile
from pathlib import Path

from plan005_formal_common import (
    ROOT,
    build_manifest,
    formal_command,
    result_key,
    sha256,
    validate_manifest,
)
from run_plan005_formal_campaigns import (
    run_one,
    validate_existing,
)


def fake_h6_manifest() -> dict:
    document = build_manifest(prepared=True)
    cpu = min(os.sched_getaffinity(0))
    document["status"] = "frozen_ready"
    for campaign in document["campaigns"]:
        campaign["backend"]["selected_workers"] = 1
        campaign["backend"]["selected_affinity_cpus"] = [cpu]
        campaign["backend"]["selection_status"] = "accepted_h6"
    validate_manifest(document, prepared=False)
    return document


def write_stub(path: Path) -> None:
    path.write_text(
        """#!/usr/bin/env python3
import json
import sys
arguments = sys.argv[1:]
index = arguments.index("--physical-fes")
physical_fes = int(arguments[index + 1])
seed_index = arguments.index("--seed")
seed = int(arguments[seed_index + 1])
print(json.dumps({"physical_fes": physical_fes, "seed": seed,
                  "best_expected_power_kw": 1.0}))
""",
        encoding="utf-8",
    )
    path.chmod(0o755)


def main() -> int:
    manifest = fake_h6_manifest()
    route_count = 0
    for campaign in manifest["campaigns"]:
        case = campaign["cases"][0]
        front = ROOT / "build/plan005-formal-fixture/front.json"
        command = formal_command(
            campaign,
            case,
            seed=campaign["optimization_seeds"][0],
            front_path=front,
        )
        if command[0] != campaign["backend"]["binary_logical_path"]:
            raise RuntimeError(
                f"{campaign['corpus_id']}: binary route drift"
            )
        if str(campaign["backend"]["selected_workers"]) not in command:
            raise RuntimeError(
                f"{campaign['corpus_id']}: H6 worker route absent"
            )
        if str(campaign["optimization_seeds"][0]) not in command:
            raise RuntimeError(
                f"{campaign['corpus_id']}: optimization seed route absent"
            )
        route_count += 1
    if route_count != 23:
        raise RuntimeError(f"formal command route count drift: {route_count}")

    with tempfile.TemporaryDirectory(
        prefix="plan005-formal-runner-",
        dir=ROOT / "build",
    ) as temporary:
        directory = Path(temporary)
        binary = directory / "formal_stub.py"
        output = directory / "result.json"
        write_stub(binary)
        campaign = copy.deepcopy(
            next(
                item
                for item in manifest["campaigns"]
                if item["corpus_id"] == "S01"
            )
        )
        campaign["backend"]["binary_logical_path"] = str(
            binary.relative_to(ROOT)
        )
        campaign["backend"]["binary_sha256"] = sha256(binary)
        case = campaign["cases"][0]
        seed = campaign["optimization_seeds"][0]
        key = result_key(manifest, campaign, case, seed)
        receipt = run_one(
            manifest,
            campaign,
            case,
            seed,
            key,
            output,
        )
        if (
            receipt["status"] != "validated_complete"
            or not validate_existing(output, key)
        ):
            raise RuntimeError("complete formal result was not reusable")
        tampered = copy.deepcopy(key)
        tampered["optimization_seed"] += 1
        if validate_existing(output, tampered):
            raise RuntimeError("tampered result key was reusable")
        partial = directory / "partial.json"
        partial.write_text('{"status":"running"}\n', encoding="utf-8")
        if validate_existing(partial, key):
            raise RuntimeError("partial formal result was reusable")

    print(
        "plan005_formal_campaign_runner_fixture_pass "
        "command_routes=23 atomic_complete=1 reusable_complete=1 "
        "rejected_key_tamper=1 rejected_partial=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
