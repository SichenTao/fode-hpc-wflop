#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0245 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Padron et al.; 10.5194/wes-4-211-2019.
# Protocol: wait for L0581; release build and H5; 630-state one/all-core H6;
# then four MC references, 160 fixed-layout/method/set roles and the paper's
# 3 starts x 4 methods x 10 sets = 120 optimization roles.
# Public assets, missing data, conflicts, reconstruction, semantic IDs,
# HPC design and claim boundary:
# hpc/core99_cpp/include/core99/padron_l0245.hpp.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-l0581-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0245 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 4 ]]; then
    echo "L0245 requires a multicore Waffle allocation" >&2
    exit 3
fi
mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0245 \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_L0245=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0245 \
    --target core99_l0245_hpc core99_l0245_test -j "${workers}"
ctest --test-dir build-waffle-l0245 \
    -R '^core99_l0245_(cpp|h5)$' --output-on-failure
python3 scripts/run_core99_l0245_admission.py \
    --binary build-waffle-l0245/hpc/core99_cpp/core99_l0245_hpc \
    --data shared/data/core99_l0245_public_data.txt \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --maximum-evaluations 1000 --stage all
