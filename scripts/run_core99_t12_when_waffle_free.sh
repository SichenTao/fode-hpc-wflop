#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T12 immutable-snapshot Waffle all-core formal wrapper.
# Paper/DOI: Wilson et al.; 10.1016/j.renene.2018.03.052.
# Protocol: release build and H5, then all four target algorithms on all five
# paper competition scenarios at the 2000-call per-scenario allowance.
# Every role requests all available Waffle CPU cores. Fast roles execute first;
# the five long 3s-MDE roles execute last and every role is independently
# resumable. Facts, missing data, conflicts, reconstruction and claim boundary:
# hpc/core99_cpp/include/core99/windflo_t12.hpp.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T12 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "T12 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-t12 \
    -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t12 \
    --target core99_t12_hpc core99_t12_test -j "${workers}"
ctest --test-dir build-waffle-t12 \
    -R '^core99_t12_(cpp|h5|runner)$' --output-on-failure
python3 scripts/run_core99_t12_admission.py \
    --binary build-waffle-t12/hpc/core99_cpp/core99_t12_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers "${workers}" --physical-fes 2000
