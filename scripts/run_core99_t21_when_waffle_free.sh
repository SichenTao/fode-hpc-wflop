#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T21 non-interfering Waffle all-core H6/formal queue
# Paper/DOI: Topology Optimization of Wind Farm Layouts;
# 10.1016/j.renene.2022.06.019
# Public source: https://github.com/byuflowlab/iea37-wflo-casestudies
# revision af88908d22795030ac2dfbe37bc38e912aee8ed6
# Missing/conflicts/completion:
# shared/contracts/core99_t21_pollini_2022.json
# Method/problem semantic IDs: t21_ramp_mma_declared_reconstruction_v1;
# t21_pollini_two_circle_density_wflop_v1
# Controlling contract: shared/contracts/core99_t21_pollini_2022.json
# Queue semantics: T30 is the final predecessor in the existing serialized
# Waffle campaign, so T21 waits for that session and then owns all 20 CPU cores
# Claim boundary: scheduling wrapper for the academic declared reconstruction;
# it does not convert the implementation into author-code replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root is required}
source_commit=${2:?source commit is required}
output_root=${3:?output root is required}

while tmux has-session -t core99-t30-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t21" \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T21=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t21" \
    --target core99_t21_hpc core99_t21_test -j 20
ctest --test-dir "${project_root}/build-waffle-t21" \
    -R 'core99_t21_(cpp|h5)' --output-on-failure

python3 "${project_root}/scripts/run_core99_t21_h6_formal.py" \
    --binary \
    "${project_root}/build-waffle-t21/hpc/core99_cpp/core99_t21_hpc" \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers 20 \
    --timing-observations 5 \
    --run-formal
