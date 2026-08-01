#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0649 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
# Protocol: wait for Y13; release build and H5; N=500 one/all-core H6; then
# one complete WR7 nine-turbine FLOWERS analytical-gradient optimization.
# Public source, reconstruction, semantic IDs and claim boundary:
# hpc/core99_cpp/include/core99/locascio_l0649.hpp.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-y13-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0649 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0649 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0649 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0649=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0649 \
    --target core99_l0649_hpc core99_l0649_test -j "${workers}"
ctest --test-dir build-waffle-l0649 \
    -R '^core99_l0649_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0649_admission.py \
    --binary build-waffle-l0649/hpc/core99_cpp/core99_l0649_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" --stage all
