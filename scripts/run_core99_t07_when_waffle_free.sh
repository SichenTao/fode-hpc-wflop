#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

while tmux has-session -t core99-t05-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t07" \
    -DBUILD_TESTING=ON -DCORE99_ENABLE_T07=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t07" \
    --target core99_t07_hpc core99_t07_test -j 20
ctest --test-dir "${project_root}/build-waffle-t07" \
    -R '^core99_t07_(cpp|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t07_admission.py" \
    --binary \
        "${project_root}/build-waffle-t07/hpc/core99_cpp/core99_t07_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 5
