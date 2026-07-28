#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

workers="${WFLOP_WORKERS:-$(nproc)}"
build_dir="${WFLOP_BUILD_DIR:-${repo_root}/build-waffle-formal}"

if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the job." >&2
  exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaigns require a clean tracked worktree." >&2
  exit 2
fi

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" -j "${workers}"
ctest --test-dir "${build_dir}" --output-on-failure

WFLOP_WORKERS="${workers}" \
WFLOP_BUILD_DIR="${build_dir}" \
WFLOP_CAMPAIGN_CONTRACT="${repo_root}/formal/contracts/eighteen_algorithm_cpp_hpc_waffle_v1.json" \
WFLOP_RESULT_DIR="${repo_root}/results/eighteen_algorithm_cpp_hpc_waffle_v1" \
  bash scripts/run_fixed_work_campaign.sh

WFLOP_WORKERS="${workers}" \
PBEA_BUILD_DIR="${build_dir}" \
PBEA_RESULT_DIR="${repo_root}/results/pbea_six_algorithm_waffle_v1" \
  bash scripts/run_pbea_formal_campaign.sh

WFLOP_WORKERS="${workers}" \
OFFSHORE_BUILD_DIR="${build_dir}" \
OFFSHORE_CAMPAIGN_CONTRACT="${repo_root}/formal/contracts/offshore_cpp_hpc_waffle_v1.json" \
OFFSHORE_RESULT_DIR="${repo_root}/results/offshore_cpp_hpc_waffle_v1" \
  bash scripts/run_offshore_formal_campaign.sh

echo "All admitted Waffle campaigns completed and validated."
echo "Common: results/eighteen_algorithm_cpp_hpc_waffle_v1"
echo "Three-objective: results/pbea_six_algorithm_waffle_v1"
echo "Offshore: results/offshore_cpp_hpc_waffle_v1"
