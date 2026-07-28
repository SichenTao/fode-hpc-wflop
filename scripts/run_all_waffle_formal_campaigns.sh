#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

workers="${WFLOP_WORKERS:-$(nproc)}"
build_dir="${WFLOP_BUILD_DIR:-${repo_root}/build-waffle-formal}"
validate_only="${WFLOP_VALIDATE_ONLY:-0}"
suite_contract="${repo_root}/formal/contracts/waffle_campaign_suite_v1.json"
common_contract="${repo_root}/formal/contracts/eighteen_algorithm_cpp_hpc_waffle_v1.json"
bde_contract="${repo_root}/formal/contracts/bde_source_replay_waffle_v1.json"
pbea_contract="${repo_root}/formal/contracts/pbea_six_algorithm_waffle_v1.json"
offshore_contract="${repo_root}/formal/contracts/offshore_cpp_hpc_waffle_v1.json"

if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the job." >&2
  exit 2
fi
if [[ "${validate_only}" != "0" && "${validate_only}" != "1" ]]; then
  echo "WFLOP_VALIDATE_ONLY must be 0 or 1." >&2
  exit 2
fi
if [[ "${validate_only}" == "0"
    && -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaigns require a clean tracked worktree." >&2
  exit 2
fi
python3 - "${suite_contract}" "${common_contract}" \
    "${bde_contract}" "${pbea_contract}" "${offshore_contract}" <<'PY'
import json
import sys
from pathlib import Path

suite = json.loads(Path(sys.argv[1]).read_text())
contracts = [json.loads(Path(path).read_text()) for path in sys.argv[2:]]
campaigns = suite["campaigns"]
if [row["campaign_id"] for row in campaigns] != [
    contract["campaign_id"] for contract in contracts
]:
    raise SystemExit("campaign suite and child contract identities differ")
run_total = sum(contract["formal_run_count"] for contract in contracts)
evaluation_total = sum(
    contract["formal_complete_layout_evaluations"]
    for contract in contracts
)
if run_total != suite["total_optimization_runs"]:
    raise SystemExit("campaign suite optimization-run total is inconsistent")
if evaluation_total != suite["total_complete_layout_evaluations"]:
    raise SystemExit("campaign suite physical-evaluation total is inconsistent")
print(
    f"campaign_suite_valid runs={run_total} "
    f"complete_layout_evaluations={evaluation_total}"
)
PY

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" -j "${workers}"
ctest --test-dir "${build_dir}" --output-on-failure

if [[ "${validate_only}" == "1" ]]; then
  echo "Campaign suite validation, build, and tests passed; optimization launch skipped."
  exit 0
fi

WFLOP_WORKERS="${workers}" \
WFLOP_BUILD_DIR="${build_dir}" \
WFLOP_CAMPAIGN_CONTRACT="${common_contract}" \
WFLOP_RESULT_DIR="${repo_root}/results/eighteen_algorithm_cpp_hpc_waffle_v1" \
  bash scripts/run_fixed_work_campaign.sh

WFLOP_WORKERS="${workers}" \
BDE_BUILD_DIR="${build_dir}" \
BDE_CAMPAIGN_CONTRACT="${bde_contract}" \
BDE_RESULT_DIR="${repo_root}/results/bde_source_replay_waffle_v1" \
  bash scripts/run_bde_source_replay_formal_campaign.sh

WFLOP_WORKERS="${workers}" \
PBEA_BUILD_DIR="${build_dir}" \
PBEA_CAMPAIGN_CONTRACT="${pbea_contract}" \
PBEA_RESULT_DIR="${repo_root}/results/pbea_six_algorithm_waffle_v1" \
  bash scripts/run_pbea_formal_campaign.sh

WFLOP_WORKERS="${workers}" \
OFFSHORE_BUILD_DIR="${build_dir}" \
OFFSHORE_CAMPAIGN_CONTRACT="${offshore_contract}" \
OFFSHORE_RESULT_DIR="${repo_root}/results/offshore_cpp_hpc_waffle_v1" \
  bash scripts/run_offshore_formal_campaign.sh

echo "All admitted Waffle campaigns completed and validated."
echo "Common: results/eighteen_algorithm_cpp_hpc_waffle_v1"
echo "BDE source replay: results/bde_source_replay_waffle_v1"
echo "Three-objective: results/pbea_six_algorithm_waffle_v1"
echo "Offshore: results/offshore_cpp_hpc_waffle_v1"
