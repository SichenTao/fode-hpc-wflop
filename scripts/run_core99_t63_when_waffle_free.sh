#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: T63 non-interfering Waffle five-case H6/formal queue
# Paper/DOI: Kuo et al.; 10.1016/j.apenergy.2016.06.085
# Source/reconstruction/claim: shared/contracts/core99_t63_kuo_2016.json;
# scheduler only, declared proxy reproduction, not author CFD/Gurobi replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}
cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${source_commit}" ]] || exit 2

# T63 consumes all CPU cores inside every HiGHS solve, so it starts only after
# every older admission/formal session and the T17 queue have exited.
while tmux has-session -t core99-t12-h6 2>/dev/null \
   || tmux has-session -t core99-t14-formal 2>/dev/null \
   || tmux has-session -t core99-t22-h6-w1 2>/dev/null \
   || tmux has-session -t core99-t62-deferred 2>/dev/null \
   || tmux has-session -t core99-t17-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-t63-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T63=ON
cmake --build build-core99-t63-waffle \
    --target core99_t63_hpc core99_t63_test -j20
ctest --test-dir build-core99-t63-waffle \
    -R 'core99_t63_(cpp|h5)' --output-on-failure
python3 scripts/run_core99_t63_admission.py \
    --binary build-core99-t63-waffle/hpc/core99_cpp/core99_t63_hpc \
    --proxy shared/data/core99_t63_figure_proxy.bin \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers 20 \
    --maximum-iterations 12 \
    --mip-time-limit-seconds 30
