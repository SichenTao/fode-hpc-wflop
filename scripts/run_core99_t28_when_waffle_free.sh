#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

# T28 follows T27 so each paper owns Waffle's full CPU allocation and optional
# CUDA device. This avoids contaminating H6 timing or silently oversubscribing.
while tmux has-session -t core99-t27-deferred 2>/dev/null; do
    sleep 20
done

torch_prefix=$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)')
cmake -S "${project_root}" -B "${project_root}/build-waffle-t28" \
    -DBUILD_TESTING=ON \
    -DWFLOP_ENABLE_TORCH=ON \
    -DCMAKE_PREFIX_PATH="${torch_prefix}" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t28" \
    --target core99_t28_hpc core99_t28_test -j 20
ctest --test-dir "${project_root}/build-waffle-t28" \
    -R '^core99_t28_(cpp_libtorch|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t28_admission.py" \
    --binary \
        "${project_root}/build-waffle-t28/hpc/t28_libtorch/core99_t28_hpc" \
    --data "${project_root}/shared/data/core99_t28" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 3 \
    --phase all
