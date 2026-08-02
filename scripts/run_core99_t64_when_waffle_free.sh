#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T64 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: The Impact of Land Use Constraints in Multi-Objective
# Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
# Public source: no target source or native paper arrays were located.
# Missing/conflicts/reconstruction and semantic IDs:
# hpc/core99_cpp/include/core99/sorkhabi_t64.hpp
# HPC design: wait for T33, then exclusively use every Waffle CPU core for
# build, H5, complete-work one/all-core H6, and 1175 target formal runs.
# Controlling contract: shared/contracts/core99_t64_sorkhabi_2016.json
# Claim boundary: academic flexible declared reconstruction, not author
# source, native maps, native wind array, random states, or numerical replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t33-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T64 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

workers=$(getconf _NPROCESSORS_ONLN)
if [[ "${workers}" -lt 2 ]]; then
    echo "T64 requires a multicore Waffle allocation" >&2
    exit 3
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t64 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T64=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t64 \
    --target core99_t64_hpc core99_t64_test -j "${workers}"
ctest --test-dir build-waffle-t64 \
    -R '^core99_t64_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t64_h6_formal.py \
    --binary build-waffle-t64/hpc/core99_cpp/core99_t64_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" \
    --repeat-count 25
