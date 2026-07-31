#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T68 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1109/TSTE.2016.2614266
# Public source, missing information, completion, semantic IDs, HPC design,
# controlling contract and claim boundary: hpc/core99_cpp/include/core99/hou_t68.hpp
# HPC design: wait for T81, then build and test the immutable T68 snapshot;
# H6 uses one versus all cores on full 4800-variable Scenario III and formal
# throughput pairs two 10-worker runs to fill all 20 Waffle cores.
# Claim boundary: academic flexible reconstruction, not author replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t81-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T68 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]] || ((workers % 2 != 0)); then
    echo "T68 requires an even multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t68 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T68=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t68 \
    --target core99_t68_hpc core99_t68_test -j "${workers}"
ctest --test-dir build-waffle-t68 \
    -R '^core99_t68_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t68_h6_formal.py \
    --binary build-waffle-t68/hpc/core99_cpp/core99_t68_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}"
