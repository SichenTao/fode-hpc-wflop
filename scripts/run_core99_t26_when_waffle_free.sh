#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T26 immutable-snapshot Waffle CPU/CUDA queue wrapper
# Paper/DOI: Li et al.; 10.1016/j.apenergy.2025.125908.
# Public source provenance, Missing facts, Reconstruction, semantic IDs,
# production backend, controlling Contract and Claim boundary:
# hpc/t26_libtorch/include/core99/li_t26.hpp.
# Protocol: wait for T58; strict LibTorch release build and H5; paper-scale
# CPU H6 plus CUDA selection; full 10000-step training and 26 GTDE runs.
# Last evidence-audit date: 2026-08-01
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t58-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T26 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi
workers=$(getconf _NPROCESSORS_ONLN)
torch_prefix=$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)')
cmake -S . -B build-waffle-t26 \
    -DBUILD_TESTING=ON -DWFLOP_ENABLE_TORCH=ON \
    -DCMAKE_PREFIX_PATH="${torch_prefix}" -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t26 \
    --target core99_t26_hpc core99_t26_test -j "${workers}"
ctest --test-dir build-waffle-t26 \
    -R '^core99_t26_(cpp_libtorch|h5)$' --output-on-failure
python3 scripts/run_core99_t26_admission.py \
    --binary build-waffle-t26/hpc/t26_libtorch/core99_t26_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers "${workers}" --stage all
