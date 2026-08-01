#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0581 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Varela and Ning; 10.2514/6.2023-1543.
# Protocol: wait for L0805; release build and H5; 349-turbine one/all-core
# dense/sparse H6; 40 accuracy roles and 10 paired dense/sparse starts.
# Public source, missing assets, conflicts, reconstruction, semantic IDs,
# HPC design and claim boundary:
# hpc/core99_cpp/include/core99/varela_l0581.hpp.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0805-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0581 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0581 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0581 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0581=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0581 \
    --target core99_l0581_hpc core99_l0581_test -j "${workers}"
ctest --test-dir build-waffle-l0581 \
    -R '^core99_l0581_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0581_admission.py \
    --binary build-waffle-l0581/hpc/core99_cpp/core99_l0581_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" --stage all
