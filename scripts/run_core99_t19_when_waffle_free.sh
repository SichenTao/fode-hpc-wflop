#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T19 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.energy.2021.120035
# Protocol: wait for T25; strict GPL optional-target build/H5; one/all-core H6;
# then all 112 deterministic paper-native roles with official sequential
# TRW-S processes filling every Waffle core.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t25-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T19 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T19 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t19 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T19=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t19 \
    --target core99_t19_hpc core99_t19_test -j "${workers}"
ctest --test-dir build-waffle-t19 \
    -R '^core99_t19_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t19_h6_formal.py \
    --binary build-waffle-t19/hpc/core99_cpp/core99_t19_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all \
    --formal-iterations 10000 \
    --formal-time-limit 3600
