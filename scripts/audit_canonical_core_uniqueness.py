#!/usr/bin/env python3
"""Reject duplicated scientific algorithm cores in formal executables."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FODE_SOURCES = {
    "src/case_loader.cpp",
    "src/evaluator.cpp",
    "src/executor.cpp",
    "src/optimizer.cpp",
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def audit_fode() -> None:
    fode_cmake = read("hpc/fode_cpp/CMakeLists.txt")
    unified_cmake = read("hpc/wflop_cpp/CMakeLists.txt")
    root_cmake = read("CMakeLists.txt")

    match = re.search(
        r"add_library\(fode_core STATIC(?P<body>.*?)\)",
        fode_cmake,
        flags=re.DOTALL,
    )
    if not match:
        raise RuntimeError("fode_core static library is absent")
    observed = {
        line.strip()
        for line in match.group("body").splitlines()
        if line.strip().endswith(".cpp")
    }
    if observed != FODE_SOURCES:
        raise RuntimeError(
            f"fode_core source mismatch expected={sorted(FODE_SOURCES)} "
            f"observed={sorted(observed)}"
        )
    if "add_executable(fode_cpp_hpc src/main.cpp)" not in fode_cmake:
        raise RuntimeError("analysis frontend is not thin")
    if "target_link_libraries(fode_cpp_hpc PRIVATE fode_core)" not in fode_cmake:
        raise RuntimeError("analysis frontend does not link fode_core")
    for source in FODE_SOURCES:
        if f"../fode_cpp/{source}" in unified_cmake:
            raise RuntimeError(f"unified CLI recompiles canonical source {source}")
    for target in ("wflop_cpp_hpc", "rlfode_train_qtable"):
        if target not in unified_cmake:
            raise RuntimeError(f"missing target {target}")
    if "fode_core" not in unified_cmake:
        raise RuntimeError("unified CMake does not link fode_core")
    if "add_subdirectory(hpc/fode_cpp)" not in root_cmake:
        raise RuntimeError("root build does not establish fode_core authority first")
    if "add_subdirectory(hpc/fode_cpp)" in root_cmake.split(
        "add_subdirectory(hpc/wflop_cpp)"
    )[1]:
        raise RuntimeError("fode_core is configured after unified CLI")
    for cmake_text, name in (
        (fode_cmake, "fode_cpp"),
        (unified_cmake, "wflop_cpp"),
    ):
        if "-ffp-contract=off" not in cmake_text:
            raise RuntimeError(f"{name} lacks frozen floating-point contract")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--method", choices=("fode",), default="fode")
    parser.add_argument("--all", action="store_true")
    parser.parse_args()
    audit_fode()
    print(
        "canonical_core_uniqueness_audit_pass "
        "method=fode core=fode_core formal_cli=wflop_cpp_hpc"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
