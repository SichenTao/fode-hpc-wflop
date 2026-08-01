#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0805 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Shao et al.; 10.1016/J.ENERGY.2025.138820.
# Protocol: wait for L0649; release build and H5; Case-III one/all-core H6;
# then 30 runs each for Cases I-III and the single Case-IV run.
# Public source, reconstruction, semantic IDs and claim boundary:
# hpc/core99_cpp/include/core99/shao_l0805.hpp.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0649-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0805 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0805 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0805 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0805=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0805 \
    --target core99_l0805_hpc core99_l0805_test -j "${workers}"
ctest --test-dir build-waffle-l0805 \
    -R '^core99_l0805_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0805_admission.py \
    --binary build-waffle-l0805/hpc/core99_cpp/core99_l0805_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" --stage all
