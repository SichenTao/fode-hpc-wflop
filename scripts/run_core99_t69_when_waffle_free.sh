#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T69 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.enconman.2017.06.005
# Facts, conflict profiles, semantics, HPC and claim boundary:
# hpc/core99_cpp/include/core99/feng_t69.hpp
# Protocol: wait for the admitted T83 queue, build/test this immutable T69
# snapshot, run three one/all-core H6 comparisons, then the 15 paper-native
# and five supplementary conflict cases with 10000 physical evaluations and
# 1000 common long-term scenarios per case using all Waffle cores.
# Claim boundary: flexible academic reconstruction, not author replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t83-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T69 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T69 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t69 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T69=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t69 \
    --target core99_t69_hpc core99_t69_test -j "${workers}"
ctest --test-dir build-waffle-t69 \
    -R '^core99_t69_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t69_h6_formal.py \
    --binary build-waffle-t69/hpc/core99_cpp/core99_t69_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --scenarios 1000 \
    --h6-fes 1000 \
    --formal-fes 10000
