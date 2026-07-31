#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T73 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.cie.2018.04.051
# Protocol: wait for L0298; strict build/H5; fixed-work one/all-core H6;
# primary four-cluster profile, twelve roles and 25 full-budget seeds.
# Public assets, missing fields, conflicts, reconstruction and claim boundary:
# hpc/core99_cpp/include/core99/song_t73.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0298-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T73 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T73 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t73 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T73=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t73 \
    --target core99_t73_hpc core99_t73_test -j "${workers}"
ctest --test-dir build-waffle-t73 \
    -R '^core99_t73_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t73_h6_formal.py \
    --binary build-waffle-t73/hpc/core99_cpp/core99_t73_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all \
    --seeds 25 \
    --ga-population 100 \
    --ga-generations 50 \
    --pattern-iterations 200 \
    --maintenance-replications 1000
