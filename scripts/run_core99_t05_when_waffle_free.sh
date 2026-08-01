#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T05 Waffle dependency-safe launcher
# Paper DOI: 10.1016/j.renene.2013.10.023.
# Public source: no author code found.
# Missing: author CPLEX model and numeric Figure-5 array.
# Reconstruction: build/test/run the versioned pure-C++ open-solver package.
# Claim boundary: academic equation-level reconstruction, not CPLEX replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
result_root=${3:?result root required}

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T05 immutable source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi

while tmux has-session -t core99-t80-deferred 2>/dev/null; do
    sleep 20
done

cmake -S "${project_root}" -B "${project_root}/build-waffle-t05" \
    -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${project_root}/build-waffle-t05" \
    --target core99_t05_hpc core99_t05_test -j 20
ctest --test-dir "${project_root}/build-waffle-t05" \
    -R '^core99_t05_(cpp|h5)$' --output-on-failure
python3 "${project_root}/scripts/run_core99_t05_admission.py" \
    --binary "${project_root}/build-waffle-t05/hpc/core99_cpp/core99_t05_hpc" \
    --output-root "${result_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20
