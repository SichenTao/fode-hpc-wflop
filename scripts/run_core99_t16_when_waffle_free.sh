#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: T16 non-interfering Waffle H6 and paper-native formal queue
# Paper/DOI: Thomas et al.; 10.2514/6.2019-0538
# Source/reconstruction/claim: shared/contracts/core99_t16_thomas_2019.json;
# scheduler only, academic declared reproduction, not SNOPT/SOWFA replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}
cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${source_commit}" ]] || exit 2

# Preserve every previously approved Waffle campaign. T16 begins only after
# all earlier paper packages and their deferred full-resource work finish.
while tmux has-session -t core99-t12-h6 2>/dev/null \
   || tmux has-session -t core99-t14-formal 2>/dev/null \
   || tmux has-session -t core99-t17-deferred 2>/dev/null \
   || tmux has-session -t core99-t22-h6-w1 2>/dev/null \
   || tmux has-session -t core99-t62-deferred 2>/dev/null \
   || tmux has-session -t core99-t63-deferred 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-t16-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T16=ON
cmake --build build-core99-t16-waffle \
    --target core99_t16_hpc core99_t16_test -j20
ctest --test-dir build-core99-t16-waffle \
    -R 'core99_t16_(cpp|h5)' --output-on-failure

# The paper's independent multistarts are the full-resource axis: twenty
# concurrent single-worker SLSQP runs consume all twenty Waffle CPU cores
# without nested oversubscription or changing any optimizer state transition.
python3 scripts/run_core99_t16_admission.py \
    --binary build-core99-t16-waffle/hpc/core99_cpp/core99_t16_hpc \
    --data shared/data/core99_t16_public_data.bin \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --workers-per-run 1 \
    --concurrent-runs 20 \
    --maxeval-per-stage 220 \
    --start-count 200
