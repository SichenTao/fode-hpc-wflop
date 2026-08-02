#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T72 non-interfering Waffle all-core H6/formal queue
# Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
# Novel Constraint Handling Approach Based on Constraint Programming;
# 10.1016/j.renene.2018.03.053
# Public source: no author source or native maps were located; related PyWake
# ISO formula source and its pinned revision are recorded in the T72 contract
# Missing/conflicts/completion:
# shared/contracts/core99_t72_sorkhabi_2018.json
# Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
# t72_energy_noise_voronoi9_declared_reconstruction_v1
# Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
# Queue semantics: T21 is the final predecessor in the serialized Waffle
# campaign, so T72 waits for that session and then owns all 20 CPU cores
# Claim boundary: scheduling wrapper for the academic declared reconstruction;
# it does not convert the implementation into author-code or IBM-CP replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root is required}
source_commit=${2:?source commit is required}
output_root=${3:?output root is required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T72 immutable source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi

while tmux has-session -t core99-t21-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t72" \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T72=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t72" \
    --target core99_t72_hpc core99_t72_test -j 20
ctest --test-dir "${project_root}/build-waffle-t72" \
    -R 'core99_t72_(cpp|h5)' --output-on-failure

python3 "${project_root}/scripts/run_core99_t72_h6_formal.py" \
    --binary \
    "${project_root}/build-waffle-t72/hpc/core99_cpp/core99_t72_hpc" \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers 20 \
    --timing-observations 5 \
    --physical-fes 80000 \
    --run-formal
