#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T27 immutable source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi

# T27 follows the CPU-only T31 campaign. It owns all CPU workers for H6; its
# paper-scale formal phase starts only when LibTorch reports CUDA available.
while tmux has-session -t core99-t31-deferred 2>/dev/null; do
    sleep 20
done

torch_prefix=$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)')
cmake -S "${project_root}" -B "${project_root}/build-waffle-t27" \
    -DBUILD_TESTING=ON \
    -DWFLOP_ENABLE_TORCH=ON \
    -DCMAKE_PREFIX_PATH="${torch_prefix}" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t27" \
    --target core99_t27_hpc core99_t27_test -j 20
ctest --test-dir "${project_root}/build-waffle-t27" \
    -R '^core99_t27_(cpp_libtorch|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t27_admission.py" \
    --binary \
        "${project_root}/build-waffle-t27/hpc/t27_libtorch/core99_t27_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 5 \
    --phase all
