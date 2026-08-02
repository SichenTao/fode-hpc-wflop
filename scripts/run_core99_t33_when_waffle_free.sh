#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T33 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Combined Layout and Cable Optimization of Offshore Wind Farms;
# 10.1016/j.ejor.2023.04.046
# Official data DOI: 10.11583/DTU.13134731
# Cited cable predecessor DOI: 10.1002/net.22100
# Public source, missing assets, paper/data conflicts, reconstruction
# completion, semantic IDs, production backend, and claim boundary:
# hpc/core99_cpp/include/core99/cazzaro_t33.hpp
# HPC design: wait for T24, then exclusively use all 20 Waffle cores for
# build, H5, one/all-core H6, and 500 target runs over all twenty paper cases
# Controlling contract: shared/contracts/core99_t33_cazzaro_combined_2023.json
# Claim boundary: declared academic reconstruction, not author numerical replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t24-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T33 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t33 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T33=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t33 \
    --target core99_t33_hpc core99_t33_test -j 20
ctest --test-dir build-waffle-t33 \
    -R '^core99_t33_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t33_h6_formal.py \
    --binary build-waffle-t33/hpc/core99_cpp/core99_t33_hpc \
    --data-root \
        build-waffle-t33/core99_t31_data/VNSforLargeOffshoreWindFarmLayoutOptimization_SyntheticInstances \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 25
