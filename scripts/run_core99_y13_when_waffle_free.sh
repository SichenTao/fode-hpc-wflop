#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: Y13 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
# Protocol: wait for T26; strict release build and H5; complete 20x20 one/all-
# core H6; then one deterministic full run for each of the four paper cases.
# Facts, corrections and claim boundary:
# hpc/core99_cpp/include/core99/du_y13.hpp.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t26-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Y13 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "Y13 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-y13 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_Y13=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-y13 \
    --target core99_y13_hpc core99_y13_test -j "${workers}"
ctest --test-dir build-waffle-y13 \
    -R '^core99_y13_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_y13_admission.py \
    --binary build-waffle-y13/hpc/core99_cpp/core99_y13_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" --stage all
