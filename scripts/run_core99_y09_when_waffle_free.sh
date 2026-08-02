#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: Y09 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.renene.2025.124386
# Protocol: wait for L0079, build and H5-test the immutable snapshot, run all
# twelve one/all-core H6 comparisons, then twelve paper-native all-core cases.
# Missing information, conflicts, completion, semantic identity, production
# backend, controlling contract and claim boundary:
# hpc/core99_cpp/include/core99/li_y09.hpp
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0079-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Y09 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "Y09 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-y09 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_Y09=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-y09 \
    --target core99_y09_hpc core99_y09_test -j "${workers}"
ctest --test-dir build-waffle-y09 \
    -R '^core99_y09_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_y09_h6_formal.py \
    --binary build-waffle-y09/hpc/core99_cpp/core99_y09_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --population 100 \
    --h6-generations 50 \
    --formal-maximum-generations 1000
