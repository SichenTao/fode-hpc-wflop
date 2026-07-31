#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T83 immutable-snapshot Waffle all-core queue wrapper
# Paper DOI: 10.1016/j.apenergy.2022.118830
# Facts, completion, semantics, HPC, contract and claim boundary:
# hpc/core99_cpp/include/core99/cazzaro_t83.hpp
# HPC design: wait for T78, build/test the immutable T83 snapshot, run one/all
# Waffle-core H6, then all eight paper seeds with equal 30-minute shape and
# rectangle VNS using all cores and no oversubscription.
# Claim boundary: academic flexible reconstruction, not author replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t78-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T83 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T83 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t83 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T83=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t83 \
    --target core99_t83_hpc core99_t83_test -j "${workers}"
ctest --test-dir build-waffle-t83 \
    -R '^core99_t83_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t83_h6_formal.py \
    --binary build-waffle-t83/hpc/core99_cpp/core99_t83_hpc \
    --data-root build-waffle-t83/core99_t31_data/VNSforLargeOffshoreWindFarmLayoutOptimization_SyntheticInstances \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --formal-micro-seconds 1800
