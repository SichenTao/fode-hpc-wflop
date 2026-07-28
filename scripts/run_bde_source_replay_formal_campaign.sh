#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

contract="${BDE_CAMPAIGN_CONTRACT:-${repo_root}/formal/contracts/bde_source_replay_waffle_v1.json}"
build_dir="${BDE_BUILD_DIR:-${repo_root}/build-bde-formal}"
result_dir="${BDE_RESULT_DIR:-${repo_root}/results/bde_source_replay_waffle_v1}"
source_root="${BDE_SOURCE_ROOT:-${repo_root}/.source-cache/official/BDE-WindFarm_code/code}"
cases="${BDE_SOURCE_REPLAY_CASES:-${repo_root}/.source-cache/generated/bde_source_replay/benchmark_cases.json}"
binary="${build_dir}/hpc/wflop_cpp/wflop_cpp_hpc"
workers="${WFLOP_WORKERS:-$(nproc)}"

if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the job." >&2
  exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaign requires a clean tracked worktree." >&2
  exit 2
fi
if [[ ! -d "${source_root}" ]]; then
  echo "Authorized official BDE source tree is missing: ${source_root}" >&2
  exit 2
fi

mkdir -p "${result_dir}"
python3 scripts/audit_bde_source_problem.py \
  --source "${source_root}" \
  --receipt "${result_dir}/source_asset_audit.json"
python3 scripts/prepare_bde_source_problem.py \
  --source "${source_root}" \
  --output "${cases}"

expected_cases="$(jq -r '.case_axes.case_count' "${contract}")"
observed_cases="$(jq -r '.case_count' "${cases}")"
if [[ "${observed_cases}" -ne "${expected_cases}" ]]; then
  echo "Generated BDE case count ${observed_cases} != ${expected_cases}" >&2
  exit 2
fi
if [[ "$(jq -r '.problem_id' "${cases}")" != "$(jq -r '.problem_id' "${contract}")" ]]; then
  echo "Generated BDE problem identity differs from the formal contract." >&2
  exit 2
fi

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" -j "${workers}" --target wflop_cpp_hpc
ctest --test-dir "${build_dir}" --output-on-failure \
  -R 'bde_source_replay|lineage_r0_r4_completion|problem_package_registry'

environment_tmp="${result_dir}/environment.json.tmp"
environment_file="${result_dir}/environment.json"
jq -n \
  --arg campaign_id "$(jq -r '.campaign_id' "${contract}")" \
  --arg host "$(hostname)" \
  --arg git_head "$(git rev-parse HEAD)" \
  --arg git_status_tracked "$(git status --porcelain --untracked-files=no)" \
  --arg compiler "$(c++ --version | head -1)" \
  --arg cmake "$(cmake --version | head -1)" \
  --argjson nproc "$(nproc)" \
  --argjson workers "${workers}" \
  --arg affinity "$(taskset -pc $$)" \
  --arg lscpu "$(lscpu)" \
  --arg binary_sha256 "$(sha256sum "${binary}" | cut -d' ' -f1)" \
  --arg contract_sha256 "$(sha256sum "${contract}" | cut -d' ' -f1)" \
  --arg cases_sha256 "$(sha256sum "${cases}" | cut -d' ' -f1)" \
  --arg case_collection_hash "$(jq -r '.collection_hash' "${cases}")" \
  --arg source_audit_sha256 "$(sha256sum "${result_dir}/source_asset_audit.json" | cut -d' ' -f1)" \
  '{
    schema_version: 1,
    campaign_id: $campaign_id,
    host: $host,
    git_head: $git_head,
    git_status_tracked: $git_status_tracked,
    compiler: $compiler,
    cmake: $cmake,
    nproc: $nproc,
    workers: $workers,
    affinity: $affinity,
    lscpu: $lscpu,
    binary_sha256: $binary_sha256,
    contract_sha256: $contract_sha256,
    cases_sha256: $cases_sha256,
    case_collection_hash: $case_collection_hash,
    source_audit_sha256: $source_audit_sha256
  }' > "${environment_tmp}"
mv "${environment_tmp}" "${environment_file}"

physical_fes="$(jq -r '.physical_fes_per_run' "${contract}")"
mapfile -t seeds < <(jq -r '.seeds[]' "${contract}")
for seed in "${seeds[@]}"; do
  output="${result_dir}/bde__seed${seed}.jsonl"
  if [[ -s "${output}" ]] && python3 \
      scripts/validate_bde_source_formal_seed.py \
      --result "${output}" \
      --cases "${cases}" \
      --seed "${seed}" \
      --workers "${workers}" \
      --physical-fes "${physical_fes}" >/dev/null; then
    echo "reuse BDE source-replay seed ${seed}"
    continue
  fi
  "${binary}" \
    --algorithm bde \
    --problem bde2025_source_replay_ws1_ws4 \
    --cases "${cases}" \
    --all-cases \
    --seed "${seed}" \
    --physical-fes "${physical_fes}" \
    --workers "${workers}" \
    --output "${output}"
  python3 scripts/validate_bde_source_formal_seed.py \
    --result "${output}" \
    --cases "${cases}" \
    --seed "${seed}" \
    --workers "${workers}" \
    --physical-fes "${physical_fes}"
done

result_count="$(find "${result_dir}" -maxdepth 1 -name 'bde__seed*.jsonl' | wc -l)"
expected_seed_count="${#seeds[@]}"
if [[ "${result_count}" -ne "${expected_seed_count}" ]]; then
  echo "Formal BDE seed files ${result_count} != ${expected_seed_count}" >&2
  exit 2
fi
manifest="${result_dir}/manifest.sha256"
find "${result_dir}" -maxdepth 1 -type f \
  ! -name 'campaign_receipt.json' \
  ! -name 'manifest.sha256' \
  -print0 | sort -z | xargs -0 sha256sum > "${manifest}"
receipt_tmp="${result_dir}/campaign_receipt.json.tmp"
receipt="${result_dir}/campaign_receipt.json"
jq -n \
  --arg campaign_id "$(jq -r '.campaign_id' "${contract}")" \
  --argjson seed_files "${result_count}" \
  --argjson formal_runs "$(jq -r '.formal_run_count' "${contract}")" \
  --argjson complete_layout_evaluations "$(jq -r '.formal_complete_layout_evaluations' "${contract}")" \
  --arg manifest_sha256 "$(sha256sum "${manifest}" | cut -d' ' -f1)" \
  '{
    schema_version: 1,
    campaign_id: $campaign_id,
    seed_files: $seed_files,
    formal_runs: $formal_runs,
    complete_layout_evaluations: $complete_layout_evaluations,
    manifest_sha256: $manifest_sha256,
    status: "complete_file_matrix",
    evidence_boundary: "This is the official-source WS1-WS4 problem profile, not the complete paper six-scenario reproduction."
  }' > "${receipt_tmp}"
mv "${receipt_tmp}" "${receipt}"
echo "BDE source-replay formal campaign complete: ${result_count} seed files."
