#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T67 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.renene.2016.10.038
# Public source: no target MATLAB source or native commercial-turbine arrays.
# Missing information, completion rules, semantic IDs, HPC design, controlling
# contract, and claim boundary:
# hpc/core99_cpp/include/core99/abdulrahman_t67.hpp
# HPC design: wait for T64, then exclusively use all Waffle CPU cores for
# build, H5, complete-work one/all-core H6, and 4050 target formal runs.
# Claim boundary: declared academic reconstruction, not author numerical replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t64-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T67 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 2 ]]; then
    echo "T67 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t67 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T67=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t67 \
    --target core99_t67_hpc core99_t67_test -j "${workers}"
ctest --test-dir build-waffle-t67 \
    -R '^core99_t67_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t67_h6_formal.py \
    --binary build-waffle-t67/hpc/core99_cpp/core99_t67_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}"
