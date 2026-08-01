#!/usr/bin/env bash
# T62 is deferred, not reduced: wait for approved T12/T14/T22 jobs to release
# Waffle, then build and run the paper-native H6 on all 20 logical CPUs.
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T62 immutable source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi
while tmux has-session -t core99-t12-h6 2>/dev/null \
   || tmux has-session -t core99-t14-formal 2>/dev/null \
   || tmux has-session -t core99-t22-h6-w1 2>/dev/null; do
    sleep 30
done
cmake -S . -B build-core99 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-core99 --target core99_t62_test core99_t62_hpc -j 20
ctest --test-dir build-core99 -R 'core99_t62_(cpp|h5)' --output-on-failure
exec python3 scripts/run_core99_t62_admission.py \
    --binary build-core99/hpc/core99_cpp/core99_t62_hpc \
    --output-root "${output_root}" \
    --workers 20 \
    --seed 20260731 \
    --source-commit "${source_commit}"
