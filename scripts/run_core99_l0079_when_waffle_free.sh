#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0079 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.oceaneng.2017.04.049
# Protocol: wait for the admitted T69 queue, build and H5-test this immutable
# snapshot, run six one/all-core H6 comparisons, then all six paper-native
# GA/PSO x constraint-mode cases with source parameters on all Waffle cores.
# Facts, completions and claim boundary:
# hpc/core99_cpp/include/core99/pillai_l0079.hpp
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t69-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0079 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0079 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0079 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_L0079=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0079 \
    --target core99_l0079_hpc core99_l0079_test -j "${workers}"
ctest --test-dir build-waffle-l0079 \
    -R '^core99_l0079_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_l0079_h6_formal.py \
    --binary build-waffle-l0079/hpc/core99_cpp/core99_l0079_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --population 100 \
    --h6-generations 20 \
    --formal-maximum-generations 1000
