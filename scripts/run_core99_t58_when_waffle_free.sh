#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T58 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Rethore et al.; 10.1002/we.1667
# Protocol: wait for T84; strict release build and H5; one/all-core H6 for
# complete Stags and Middelgrunden SGA+SLP; then 101 resumable formal
# receipts covering five native roles and the declared 25-seed stochastic
# robustness extension with all Waffle CPU cores occupied.
# Facts, completion boundaries and semantic IDs:
# hpc/core99_cpp/include/core99/rethore_t58.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t84-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T58 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T58 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t58 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T58=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t58 \
    --target core99_t58_hpc core99_t58_test -j "${workers}"
ctest --test-dir build-waffle-t58 \
    -R '^core99_t58_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t58_admission.py \
    --binary build-waffle-t58/hpc/core99_cpp/core99_t58_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all
