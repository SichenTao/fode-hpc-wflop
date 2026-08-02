#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T08 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.apenergy.2016.06.101
# Protocol: wait for T19; strict build and H5; one/all-core H6; then all 49
# paper roles and 211 optimization receipts with every Waffle core.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t19-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T08 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T08 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t08 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T08=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t08 \
    --target core99_t08_hpc core99_t08_test -j "${workers}"
ctest --test-dir build-waffle-t08 \
    -R '^core99_t08_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t08_h6_formal.py \
    --binary build-waffle-t08/hpc/core99_cpp/core99_t08_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all
