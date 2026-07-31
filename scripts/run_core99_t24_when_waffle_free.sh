#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T24 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
# Intermittency; 10.1016/j.apenergy.2024.123383
# Public source, missing assets, paper-internal data conflict, reconstruction
# completion, semantic IDs, production backend, and claim boundary:
# hpc/core99_cpp/include/core99/kim_t24.hpp
# HPC design: wait for T85, then exclusively use all 20 Waffle cores for
# build, H5, one/all-core H6, and 150 runs over all six paper problems
# Controlling contract: shared/contracts/core99_t24_kim_2024.json
# Claim boundary: declared reconstruction, not author numerical replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t85-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T24 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t24 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T24=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t24 \
    --target core99_t24_hpc core99_t24_test -j 20
ctest --test-dir build-waffle-t24 \
    -R '^core99_t24_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t24_h6_formal.py \
    --binary build-waffle-t24/hpc/core99_cpp/core99_t24_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 25
