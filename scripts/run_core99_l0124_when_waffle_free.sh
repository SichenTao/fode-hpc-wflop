#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: L0124 non-interfering Waffle H6 and paper-native formal queue
# Paper/DOI: Parada et al.; 10.1016/j.renene.2017.02.017
# Source/reconstruction/claim: shared/contracts/core99_l0124_parada_2017.json;
# scheduler only, academic declared reproduction, not author-source replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}
cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${source_commit}" ]] || exit 2

# T16 itself waits for every earlier approved Waffle campaign. L0124 begins
# only after T16 has exited, preserving the already approved queue.
while tmux has-session -t core99-t16-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-l0124-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-l0124-waffle \
    --target core99_l0124_hpc core99_l0124_test -j20
ctest --test-dir build-core99-l0124-waffle \
    -R 'core99_l0124_(cpp|h5)' --output-on-failure

# Thirty independent paper-native case/repeat tasks provide the full-resource
# axis. Twenty concurrent one-worker processes consume all Waffle CPU cores
# without nested oversubscription.
python3 scripts/run_core99_l0124_admission.py \
    --binary build-core99-l0124-waffle/hpc/core99_cpp/core99_l0124_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers-per-run 1 \
    --concurrent-runs 20 \
    --population 600 \
    --generations 500 \
    --repeat-count 5
