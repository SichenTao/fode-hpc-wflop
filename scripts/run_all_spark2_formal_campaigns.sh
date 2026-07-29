#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

workers="${WFLOP_WORKERS:-$(nproc)}"
build_dir="${WFLOP_BUILD_DIR:-${repo_root}/build-spark2-formal}"
validate_only="${WFLOP_VALIDATE_ONLY:-0}"
suite_contract="${repo_root}/formal/contracts/spark2_campaign_suite_v1.json"
common_contract="${repo_root}/formal/contracts/eighteen_algorithm_cpp_hpc_spark2_v3.json"
bde_contract="${repo_root}/formal/contracts/bde_source_replay_spark2_v1.json"
pbea_contract="${repo_root}/formal/contracts/pbea_six_algorithm_spark2_v1.json"
offshore_contract="${repo_root}/formal/contracts/offshore_cpp_hpc_spark2_v1.json"

expected_hostname="$(jq -r '.execution_hostname' "${suite_contract}")"
observed_hostname="$(hostname -s | tr '[:upper:]' '[:lower:]')"
if [[ "${observed_hostname}" != "${expected_hostname}" ]]; then
  echo "Spark2 formal suite requires ${expected_hostname}; got ${observed_hostname}." >&2
  exit 2
fi
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
if [row["role"] for row in campaigns] != [
    "common",
    "bde",
    "pbea",
    "offshore",
]:
    raise SystemExit("campaign suite role order is inconsistent")
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

bash scripts/prepare_formal_problem_assets.sh
cmake -S . -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "${build_dir}" -j "${workers}"
ctest --test-dir "${build_dir}" --output-on-failure

if [[ "${validate_only}" == "1" ]]; then
  echo "Spark2 suite validation, build, and tests passed; optimization launch skipped."
  exit 0
fi

WFLOP_WORKERS="${workers}" \
WFLOP_BUILD_DIR="${build_dir}" \
WFLOP_CAMPAIGN_CONTRACT="${common_contract}" \
WFLOP_RESULT_DIR="${repo_root}/results/eighteen_algorithm_cpp_hpc_spark2_v3" \
  bash scripts/run_fixed_work_campaign.sh

WFLOP_WORKERS="${workers}" \
BDE_BUILD_DIR="${build_dir}" \
BDE_CAMPAIGN_CONTRACT="${bde_contract}" \
BDE_RESULT_DIR="${repo_root}/results/bde_source_replay_spark2_v1" \
  bash scripts/run_bde_source_replay_formal_campaign.sh

WFLOP_WORKERS="${workers}" \
PBEA_BUILD_DIR="${build_dir}" \
PBEA_CAMPAIGN_CONTRACT="${pbea_contract}" \
PBEA_RESULT_DIR="${repo_root}/results/pbea_six_algorithm_spark2_v1" \
  bash scripts/run_pbea_formal_campaign.sh

WFLOP_WORKERS="${workers}" \
OFFSHORE_BUILD_DIR="${build_dir}" \
OFFSHORE_CAMPAIGN_CONTRACT="${offshore_contract}" \
OFFSHORE_RESULT_DIR="${repo_root}/results/offshore_cpp_hpc_spark2_v1" \
  bash scripts/run_offshore_formal_campaign.sh

python3 scripts/summarize_formal_suite.py \
  --suite-contract "${suite_contract}" \
  --results-root "${repo_root}/results" \
  --output-dir "${repo_root}/results/spark2_campaign_suite_v1/analysis"

echo "All admitted Spark2 campaigns completed and validated."
echo "Common: results/eighteen_algorithm_cpp_hpc_spark2_v3"
echo "BDE source replay: results/bde_source_replay_spark2_v1"
echo "Three-objective: results/pbea_six_algorithm_spark2_v1"
echo "Offshore: results/offshore_cpp_hpc_spark2_v1"
