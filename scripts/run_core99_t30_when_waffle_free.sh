#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

# T30 follows T28 so every paper owns all Waffle CPU cores during H6 and formal
# execution. Waiting is external to the T30 evidence and cannot contaminate it.
while tmux has-session -t core99-t28-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t30" \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T30=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t30" \
    --target core99_t30_hpc core99_t30_test -j 20
ctest --test-dir "${project_root}/build-waffle-t30" \
    -R '^core99_t30_(cpp_highs|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t30_admission.py" \
    --binary \
        "${project_root}/build-waffle-t30/hpc/core99_cpp/core99_t30_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 3 \
    --phase all
