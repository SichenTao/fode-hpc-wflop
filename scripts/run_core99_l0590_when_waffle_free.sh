#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: L0590 non-interfering Waffle training, H6 and formal queue
# Paper/DOI: Sun and Yang; 10.1016/j.apenergy.2023.121554
# Source/reconstruction/claim:
# shared/contracts/core99_l0590_sun_ann_height_2023.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"

while tmux has-session -t core99-l0499-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-l0590-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-l0590-waffle \
    --target core99_l0590_hpc core99_l0590_test -j20
ctest --test-dir build-core99-l0590-waffle \
    -R 'core99_l0590_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_l0590_admission.py \
    --binary build-core99-l0590-waffle/hpc/core99_cpp/core99_l0590_hpc \
    --output-root "results/core99/formal/L0590/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --total-workers 20 \
    --repeat-count 25 \
    --sample-count 32768 \
    --maximum-epochs 1000
