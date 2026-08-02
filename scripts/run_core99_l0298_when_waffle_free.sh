#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0298 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1109/TSG.2020.3022378
# Protocol: wait for L0373; strict build/H5; fixed-work one/all-core H6;
# nine native profiles, 29 roles and 25 seeds at paper maximum budgets.
# Public assets, missing fields, conflicts, reconstruction, semantic IDs,
# backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/tao_l0298.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0373-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0298 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0298 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0298 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0298=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0298 \
    --target core99_l0298_hpc core99_l0298_test -j "${workers}"
ctest --test-dir build-waffle-l0298 \
    -R '^core99_l0298_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0298_h6_formal.py \
    --binary build-waffle-l0298/hpc/core99_cpp/core99_l0298_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all \
    --seeds 25 \
    --outer-population 120 \
    --outer-iterations 250 \
    --inner-population 100 \
    --inner-iterations 250
