#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: T87 non-interfering Waffle H6 and paper-native formal queue
# Paper/DOI: Hu et al.; 10.1016/j.energy.2023.129745
# Source/reconstruction/claim: shared/contracts/core99_t87_hu_iga_pso_2024.json;
# scheduler only, declared figure-proxy reproduction, not author CFD replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${CORE99_SOURCE_COMMIT}" ]] || exit 2

# L0124 already waits for T16 and all earlier approved work. T87 begins only
# after L0124 exits, preserving the existing Waffle queue.
while tmux has-session -t core99-l0124-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-t87-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-t87-waffle \
    --target core99_t87_hpc core99_t87_test -j20
ctest --test-dir build-core99-t87-waffle \
    -R 'core99_t87_(cpp|h5)' --output-on-failure

# Each IGA-PSO run has sufficient internal population-level parallelism to use
# all twenty cores. Formal case/seed runs execute sequentially.
python3 scripts/run_core99_t87_admission.py \
    --binary build-core99-t87-waffle/hpc/core99_cpp/core99_t87_hpc \
    --data shared/data/core99_t87_figure_proxy.bin \
    --output-root "results/core99/formal/T87/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --workers 20 \
    --iga-population 300 \
    --iga-generations 1000 \
    --pso-population 100 \
    --pso-iterations 200 \
    --repeat-count 10
