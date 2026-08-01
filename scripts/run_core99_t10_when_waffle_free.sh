#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T10 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.rser.2016.07.021
# Protocol: wait for T08; strict build and H5; one/all-core H6; then all 196
# paper-native roles and 1,960 optimization receipts with every Waffle core.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t08-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T10 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T10 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t10 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T10=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t10 \
    --target core99_t10_hpc core99_t10_test -j "${workers}"
ctest --test-dir build-waffle-t10 \
    -R '^core99_t10_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t10_h6_formal.py \
    --binary build-waffle-t10/hpc/core99_cpp/core99_t10_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all
