#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T85 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
# Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
# Public source, cited predecessor, missing assets, reconstruction completion,
# semantic IDs, production backend, and claim boundary:
# hpc/core99_cpp/include/core99/song_t85.hpp
# HPC design: wait for T82, then exclusively use all 20 Waffle cores for
# build, H5, one/all-core H6, and 150 target runs over all six paper problems
# Controlling contract: shared/contracts/core99_t85_song_2023.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t82-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T85 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t85 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T85=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t85 \
    --target core99_t85_hpc core99_t85_test -j 20
ctest --test-dir build-waffle-t85 \
    -R '^core99_t85_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t85_h6_formal.py \
    --binary build-waffle-t85/hpc/core99_cpp/core99_t85_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 25
