#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: Y14 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1109/TSTE.2026.3661110
# Protocol: wait for Y09; strict build and H5; six-role one/all-core H6; then
# six roles x ten seeds at the paper's 150000 evaluation-slot budget.
# Public asset, missing information, conflict, reconstruction, semantic IDs,
# backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/zhang_y14.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-y09-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Y14 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "Y14 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-y14 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_Y14=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build-waffle-y14 \
    --target core99_y14_hpc core99_y14_test -j "${workers}"
ctest --test-dir build-waffle-y14 \
    -R '^core99_y14_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_y14_h6_formal.py \
    --binary build-waffle-y14/hpc/core99_cpp/core99_y14_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --h6-evaluation-slots 5000 \
    --formal-evaluation-slots 150000 \
    --formal-repeats 10
