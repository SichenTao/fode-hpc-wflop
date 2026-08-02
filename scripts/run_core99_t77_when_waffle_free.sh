#!/usr/bin/env bash
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T77 immutable source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi

# T77 follows the already-queued T07 campaign and never oversubscribes Waffle.
while tmux has-session -t core99-t07-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t77" \
    -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t77" \
    --target core99_t77_hpc core99_t77_test -j 20
ctest --test-dir "${project_root}/build-waffle-t77" \
    -R '^core99_t77_(cpp|h5)$' --output-on-failure

python3 "${project_root}/scripts/run_core99_t77_admission.py" \
    --binary \
        "${project_root}/build-waffle-t77/hpc/core99_cpp/core99_t77_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --h6-observations 5 \
    --repeat-count 5
