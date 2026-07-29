#!/usr/bin/env python3
"""Exercise faithful Plan-004 model families on every available backend."""

from __future__ import annotations

import argparse
import json
import subprocess

import torch


def run(binary: str, method: str, backend: str) -> str:
    result = subprocess.run(
        [binary, "--method", method, "--backend", backend],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    arguments = parser.parse_args()
    backends = ["cpu"]
    if torch.cuda.is_available():
        backends.extend(["cuda", "hybrid"])
    executions = []
    for backend in backends:
        for method in ("taae", "alga", "rlpso"):
            output = run(arguments.binary, method, backend)
            marker = f"{method}_"
            if marker not in output or "optimization_bridge=yes" not in output:
                raise RuntimeError(
                    f"{method}/{backend}: target bridge marker absent"
                )
            executions.append(
                {"method": method, "backend": backend, "output": output}
            )
    print(
        json.dumps(
            {
                "status": "pass",
                "contract": "plan004_faithful_learning_backend_matrix_v1",
                "backend_count": len(backends),
                "execution_count": len(executions),
                "cuda_exercised": "cuda" in backends,
                "hybrid_pinned_async_exercised": "hybrid" in backends,
                "target_optimization_bridges": 3,
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
