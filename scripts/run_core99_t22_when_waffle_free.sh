#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Unit: T22 non-interfering Waffle all-core H6 and paper-native formal queue
# Paper/DOI: Thomas et al.; 10.5194/wes-8-865-2023
# Public source: https://github.com/jaredthomas68/thomas2022-8-opt-algs-wflop
# at revision 41d7290b8cc9bf3d90b25d844312f4790037806d
# Missing/reconstruction/claim: shared/contracts/core99_t22_iea37_cs4.json;
# scheduler only, academic declared reconstruction, not author DEBO replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail
: "${CORE99_SOURCE_COMMIT:?set CORE99_SOURCE_COMMIT to the deployed commit}"
observed_commit=$(git rev-parse HEAD)
[[ "${observed_commit}" == "${CORE99_SOURCE_COMMIT}" ]] || exit 2

# T12 owns two cores until its long 3s-MDE admission finishes. T22 then takes
# the complete 20-core Waffle node. The already queued T16 script explicitly
# waits for this tmux session, which prevents nested oversubscription.
while tmux has-session -t core99-t12-h6 2>/dev/null; do
    sleep 30
done

cmake -S . -B build-core99-t22-waffle \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON
cmake --build build-core99-t22-waffle \
    --target core99_t22_hpc core99_t22_test -j20
ctest --test-dir build-core99-t22-waffle \
    -R 'core99_t22_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_t22_h6_formal.py \
    --binary build-core99-t22-waffle/hpc/core99_cpp/core99_t22_hpc \
    --output-root "results/core99/formal/T22/${CORE99_SOURCE_COMMIT}" \
    --source-commit "${CORE99_SOURCE_COMMIT}" \
    --workers 20 \
    --timing-repeats 101 \
    --timing-observations 5 \
    --run-formal
