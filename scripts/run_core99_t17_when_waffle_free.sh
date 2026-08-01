#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: T17 non-interfering Waffle admission queue
# Paper/DOI: Brogna et al.; 10.1016/j.apenergy.2019.114189
# Source/reconstruction/claim: shared/contracts/core99_t17_brogna_2020.json;
# scheduler only, academic declared reproduction, not author-site replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${CORE99_SOURCE_COMMIT}" ]] || exit 2

# Preserve all already-approved Waffle work. T17 starts only after those
# sessions and the queued T62 admission release the shared twenty CPU cores.
while tmux has-session -t core99-t12-h6 2>/dev/null \
   || tmux has-session -t core99-t14-formal 2>/dev/null \
   || tmux has-session -t core99-t22-h6-w1 2>/dev/null \
   || tmux has-session -t core99-t62-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-t17-waffle \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-core99-t17-waffle \
    --target core99_t17_hpc core99_t17_test -j20
ctest --test-dir build-core99-t17-waffle \
    -R 'core99_t17_(cpp|h5)' --output-on-failure
python3 scripts/run_core99_t17_admission.py \
    --binary build-core99-t17-waffle/hpc/core99_cpp/core99_t17_hpc \
    --proxy shared/data/core99_t17_pywake_proxy.bin \
    --output-root "results/core99/h6/T17/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --workers-per-run 10 \
    --concurrent-runs 2 \
    --stage1-fes 500 \
    --stage2-fes 2000
