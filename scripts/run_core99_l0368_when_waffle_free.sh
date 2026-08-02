#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0368 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.enconman.2021.114610
# Cited same-author public asset DOI: 10.17632/bvrdgykzwy.1
# Protocol: wait for Y16; strict build and H5; S5W4 one/all-core H6; all
# 20 native cases at population100/500 generations; sixteen S1 transfers.
# Public asset, missing information, conflicts, corrections, reconstruction,
# semantic IDs, backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/liu_l0368.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-y16-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0368 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0368 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0368 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0368=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0368 \
    --target core99_l0368_hpc core99_l0368_test -j "${workers}"
ctest --test-dir build-waffle-l0368 \
    -R '^core99_l0368_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0368_h6_formal.py \
    --binary build-waffle-l0368/hpc/core99_cpp/core99_l0368_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --h6-generations 100 \
    --formal-generations 500
