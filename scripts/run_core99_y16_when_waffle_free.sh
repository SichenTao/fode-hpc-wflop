#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: Y16 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1109/TSTE.2026.3686029
# First-party supporting patent: CN121683298A/CN121683298B
# Protocol: wait for Y14; strict build and H5; 20-task one/all-core H6; then
# all 31 deterministic paper-native roles with 18 angles and ten patterns.
# Public asset, missing information, conflicts, corrections, reconstruction,
# semantic IDs, backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/huang_y16.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-y14-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Y16 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "Y16 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-y16 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_Y16=ON \
    -DCORE99_Y16_STRICT_WARNINGS=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-y16 \
    --target core99_y16_hpc core99_y16_test -j "${workers}"
ctest --test-dir build-waffle-y16 \
    -R '^core99_y16_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_y16_h6_formal.py \
    --binary build-waffle-y16/hpc/core99_cpp/core99_y16_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --h6-mip-time-limit-seconds 60 \
    --formal-mip-time-limit-seconds 10000 \
    --maximum-bda-iterations 20
