#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: L0623 non-interfering Waffle H6 and formal queue
# Paper/DOI: Wang et al.; 10.1016/j.oceaneng.2023.116644
# Source/reconstruction/claim:
# shared/contracts/core99_l0623_wang_cfd_kriging_2024.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"

while tmux has-session -t core99-l0341-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-l0623-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-l0623-waffle \
    --target core99_l0623_hpc core99_l0623_test -j20
ctest --test-dir build-core99-l0623-waffle \
    -R 'core99_l0623_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_l0623_admission.py \
    --binary build-core99-l0623-waffle/hpc/core99_cpp/core99_l0623_hpc \
    --output-root "results/core99/formal/L0623/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --total-workers 20 \
    --repeat-count 25
