#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T25 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.5194/wes-9-321-2024
# Protocol: wait for the previously queued T18 campaign; strict build/H5;
# fixed-work one/all-core H6; deterministic Horns matrix and 30 stochastic
# IEA roles by 25 platform seeds using top-level all-core scheduling.
# Fact boundary: hpc/core99_cpp/include/core99/rodrigues_t25.hpp
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t18-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T25 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T25 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t25 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T25=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t25 \
    --target core99_t25_hpc core99_t25_test -j "${workers}"
ctest --test-dir build-waffle-t25 \
    -R '^core99_t25_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_t25_h6_formal.py \
    --binary build-waffle-t25/hpc/core99_cpp/core99_t25_hpc \
    --output "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --stage all \
    --seeds 25 \
    --formal-maximum-evaluations 5000 \
    --include-fd
