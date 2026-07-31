#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T78 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.apenergy.2020.114896
# Public source, missing information, conflicts, declared completion, semantic
# IDs, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/wu_t78.hpp
# HPC design: wait for the T68 queue, then build and validate the immutable T78
# snapshot. H6 uses one versus every Waffle core on the complete strict case;
# every formal repeat then uses every Waffle core without oversubscription.
# Claim boundary: academic flexible reconstruction, not author replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t68-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T78 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T78 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t78 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T78=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t78 \
    --target core99_t78_hpc core99_t78_test -j "${workers}"
ctest --test-dir build-waffle-t78 \
    -R '^core99_t78_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t78_h6_formal.py \
    --binary build-waffle-t78/hpc/core99_cpp/core99_t78_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}"
