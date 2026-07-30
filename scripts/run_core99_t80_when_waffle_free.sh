#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

while tmux has-session -t core99-t74-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t80" \
    -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t80" \
    --target core99_t80_hpc core99_t80_test -j 20
ctest --test-dir "${project_root}/build-waffle-t80" \
    -R '^core99_t80_(cpp|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t80_admission.py" \
    --binary "${project_root}/build-waffle-t80/hpc/core99_cpp/core99_t80_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 10
