#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: L0499 non-interfering Waffle H6 and 126-case formal queue
# Paper/DOI: Wen, Song and Wang; 10.1016/j.enconman.2022.115347
# Source/reconstruction/claim:
# shared/contracts/core99_l0499_wen_uncertain_cvar_2022.json
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}
cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${source_commit}" ]] || exit 2

while tmux has-session -t core99-l0371-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-l0499-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-l0499-waffle \
    --target core99_l0499_hpc core99_l0499_test -j20
ctest --test-dir build-core99-l0499-waffle \
    -R 'core99_l0499_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_l0499_admission.py \
    --binary build-core99-l0499-waffle/hpc/core99_cpp/core99_l0499_hpc \
    --data shared/data/core99_l0499_proxy.bin \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --max-physical-fes 20032 \
    --repeat-count 20
