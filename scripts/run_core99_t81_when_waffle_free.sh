#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T81 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.apenergy.2021.117947
# Public source: FLORIS v2.4 is a cited dependency; no target source or native
# environmental/wave arrays were located.
# Missing information, completion rules, semantic IDs, HPC design, controlling
# contract, and claim boundary: hpc/core99_cpp/include/core99/ti_t81.hpp
# HPC design: wait for T76, then use all Waffle CPU cores for build, H5,
# one/15-start H6, and 50 paper-case runs; formal runs execute as two half-core
# processes so all 20 physical cores are available without oversubscription.
# Claim boundary: declared academic reconstruction, not author numerical replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t76-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T81 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]] || ((workers % 2 != 0)); then
    echo "T81 requires an even multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t81 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T81=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t81 \
    --target core99_t81_hpc core99_t81_test -j "${workers}"
ctest --test-dir build-waffle-t81 \
    -R '^core99_t81_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t81_h6_formal.py \
    --binary build-waffle-t81/hpc/core99_cpp/core99_t81_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}"
