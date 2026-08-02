#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T82 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
# Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
# Public source, missing assets, conflicts, reconstruction completion,
# semantic IDs, production backend, and claim boundary:
# hpc/core99_cpp/include/core99/cao_t82.hpp
# HPC design: wait for L0259, then exclusively use all 20 Waffle cores for
# build, H5, one/all-core H6, and 75 target runs over all three paper problems
# Controlling contract: shared/contracts/core99_t82_cao_2022.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0259-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T82 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t82 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T82=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t82 \
    --target core99_t82_hpc core99_t82_test -j 20
ctest --test-dir build-waffle-t82 \
    -R '^core99_t82_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t82_h6_formal.py \
    --binary build-waffle-t82/hpc/core99_cpp/core99_t82_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 25
