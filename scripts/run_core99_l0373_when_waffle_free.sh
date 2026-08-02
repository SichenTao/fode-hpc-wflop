#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0373 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.renene.2021.10.032
# Protocol: wait for L0368; strict build/H5; complete N16-W36 one/all-core
# H6; all six paper-native profiles on every available Waffle core.
# Public sources, missing information, conflicts, corrections, reconstruction,
# semantic IDs, backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/chen_l0373.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0368-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0373 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0373 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0373 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0373=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0373 \
    --target core99_l0373_hpc core99_l0373_test -j "${workers}"
ctest --test-dir build-waffle-l0373 \
    -R '^core99_l0373_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0373_h6_formal.py \
    --binary build-waffle-l0373/hpc/core99_cpp/core99_l0373_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}"
