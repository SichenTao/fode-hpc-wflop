#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T84 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1002/we.2692
# Protocol: wait for T10; strict build and H5; one/all-core H6 for complete
# SLSQP+WEC and ALPSO+WEC; then all 16 final paper roles over 200 public
# common starts (3,200 resumable receipts) using all Waffle CPU cores.
# Facts, conflicts, reconstruction, semantic IDs and claim boundary:
# hpc/core99_cpp/include/core99/thomas_t84.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t10-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T84 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T84 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t84 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T84=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t84 \
    --target core99_t84_hpc core99_t84_test -j "${workers}"
ctest --test-dir build-waffle-t84 \
    -R '^core99_t84_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t84_admission.py \
    --binary build-waffle-t84/hpc/core99_cpp/core99_t84_hpc \
    --data shared/data/core99_t84_public_data.bin \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all
