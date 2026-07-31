#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

# T31 follows T77 and owns all Waffle CPU workers; campaigns never overlap.
while tmux has-session -t core99-t77-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t31" \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T31=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t31" \
    --target core99_t31_hpc core99_t31_test -j 20
ctest --test-dir "${project_root}/build-waffle-t31" \
    -R '^core99_t31_(cpp|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t31_admission.py" \
    --binary \
        "${project_root}/build-waffle-t31/hpc/core99_cpp/core99_t31_hpc" \
    --data-root \
        "${project_root}/build-waffle-t31/core99_t31_data/VNSforLargeOffshoreWindFarmLayoutOptimization_SyntheticInstances" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 5 \
    --phase all
