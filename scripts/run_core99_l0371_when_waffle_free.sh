#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: L0371 non-interfering Waffle H6 and 29-case formal queue
# Paper/DOI: Guo et al.; 10.1016/j.jweia.2021.104548
# Source/reconstruction/claim:
# shared/contracts/core99_l0371_guo_stability_deem_2021.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${CORE99_SOURCE_COMMIT}" ]] || exit 2

# Preserve the approved queue. T87 itself waits for every earlier session.
while tmux has-session -t core99-t87-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-l0371-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-l0371-waffle \
    --target core99_l0371_hpc core99_l0371_test -j20
ctest --test-dir build-core99-l0371-waffle \
    -R 'core99_l0371_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_l0371_admission.py \
    --binary build-core99-l0371-waffle/hpc/core99_cpp/core99_l0371_hpc \
    --data shared/data/core99_l0371_proxy.bin \
    --output-root "results/core99/formal/L0371/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --total-workers 20 \
    --max-physical-fes 150000 \
    --repeat-count 30 \
    --h6-physical-fes 5000
