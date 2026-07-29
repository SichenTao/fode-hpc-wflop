#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${WFLOP_BUILD_DIR:-${repo_root}/build}"
binary="${build_dir}/hpc/wflop_cpp/wflop_cpp_hpc"
contract="${WFLOP_CAMPAIGN_CONTRACT:-${repo_root}/formal/contracts/eighteen_algorithm_cpp_hpc_waffle_v1.json}"
result_root="${WFLOP_RESULT_DIR:-${repo_root}/results/eighteen_algorithm_cpp_hpc_waffle_v1}"
workers="${WFLOP_WORKERS:-$(nproc)}"
cases="${repo_root}/shared/contracts/benchmark_cases.json"
models="${repo_root}/shared/models/sugga_cpp"

if [[ ! -x "${binary}" ]]; then
  echo "Release binary is missing; run scripts/build.sh first." >&2
  exit 1
fi
if ! [[ "${workers}" =~ ^[1-9][0-9]*$ ]]; then
  echo "WFLOP_WORKERS must be a positive integer." >&2
  exit 1
fi
if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the job." >&2
  exit 1
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaign requires a clean tracked worktree." >&2
  exit 1
fi

mkdir -p "${result_root}"
physical_fes="$(jq -r '.physical_fes_per_run' "${contract}")"
algorithm_csv="$(jq -r '.algorithms | join(",")' "${contract}")"
expected_algorithms="$(jq -c '.algorithms' "${contract}")"
expected_cases="$(jq -r '.cases' "${contract}")"
expected_results="$(( $(jq -r '.algorithms | length' "${contract}") * expected_cases ))"
mapfile -t seeds < <(jq -r '.seeds[]' "${contract}")

environment_file="${result_root}/environment.json"
environment_tmp="${environment_file}.tmp"
jq -n \
  --arg campaign_id "$(jq -r '.campaign_id' "${contract}")" \
  --arg host "$(hostname)" \
  --arg git_head "$(git rev-parse HEAD)" \
  --arg git_status_tracked "$(git status --porcelain --untracked-files=no)" \
  --arg compiler "$(c++ --version | head -1)" \
  --arg cmake "$(cmake --version | head -1)" \
  --arg python "$(python3 --version)" \
  --arg python_packages "$(python3 -m pip freeze | sort)" \
  --argjson nproc "$(nproc)" \
  --argjson workers "${workers}" \
  --arg affinity "$(taskset -pc $$)" \
  --arg lscpu "$(lscpu)" \
  --arg binary_sha256 "$(sha256sum "${binary}" | cut -d' ' -f1)" \
  --arg contract_sha256 "$(sha256sum "${contract}" | cut -d' ' -f1)" \
  '{
    schema_version: 1,
    campaign_id: $campaign_id,
    host: $host,
    git_head: $git_head,
    git_status_tracked: $git_status_tracked,
    compiler: $compiler,
    cmake: $cmake,
    python: $python,
    python_packages: $python_packages,
    nproc: $nproc,
    workers: $workers,
    affinity: $affinity,
    lscpu: $lscpu,
    binary_sha256: $binary_sha256,
    contract_sha256: $contract_sha256
  }' > "${environment_tmp}"
mv "${environment_tmp}" "${environment_file}"

validate_seed_file() {
  local path="$1"
  local seed="$2"
  jq -se \
    --argjson seed "${seed}" \
    --argjson fes "${physical_fes}" \
    --argjson workers "${workers}" \
    --argjson expected_algorithms "${expected_algorithms}" \
    --argjson expected_cases "${expected_cases}" \
    --argjson expected_results "${expected_results}" \
    'length == $expected_results
     and (([.[].algorithm_id] | unique | sort)
          == ($expected_algorithms | sort))
     and ([.[].case_id] | unique | length) == $expected_cases
     and all(.[];
       .seed == $seed
       and .physical_fes == $fes
       and .requested_workers == $workers
       and .observed_workers == $workers
       and (.best_expected_power_kw | isfinite)
       and (.best_layout_1based | length > 0))
     and ([.[] | [.algorithm_id, .case_id] | join(":")]
          | unique | length) == $expected_results' \
    "${path}" >/dev/null
}

for seed in "${seeds[@]}"; do
  output="${result_root}/seed_${seed}.jsonl"
  if [[ -s "${output}" ]] && validate_seed_file "${output}" "${seed}"; then
    echo "reuse seed ${seed}: ${expected_results}-result receipt passes"
    continue
  fi
  temporary="${output}.partial"
  rm -f "${temporary}"
  OMP_DYNAMIC=FALSE OMP_NUM_THREADS="${workers}" \
    "${binary}" \
      --algorithms "${algorithm_csv}" \
      --all-cases \
      --physical-fes "${physical_fes}" \
      --seed "${seed}" \
      --workers "${workers}" \
      --cases "${cases}" \
      --models "${models}" \
      --output "${temporary}"
  validate_seed_file "${temporary}" "${seed}"
  mv "${temporary}" "${output}"
done

manifest="${result_root}/manifest.sha256"
find "${result_root}" -maxdepth 1 -name 'seed_*.jsonl' -type f -print0 \
  | sort -z \
  | xargs -0 sha256sum > "${manifest}"

seed_file_count="$(find "${result_root}" -maxdepth 1 \
  -name 'seed_*.jsonl' -type f | wc -l)"
if [[ "${seed_file_count}" -ne "${#seeds[@]}" ]]; then
  echo "Formal seed files ${seed_file_count} != ${#seeds[@]}" >&2
  exit 2
fi
receipt_tmp="${result_root}/campaign_receipt.json.tmp"
receipt="${result_root}/campaign_receipt.json"
jq -n \
  --arg campaign_id "$(jq -r '.campaign_id' "${contract}")" \
  --argjson seed_files "${seed_file_count}" \
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
    evidence_boundary: "Quality and statistical claims require a separate analysis receipt."
  }' > "${receipt_tmp}"
mv "${receipt_tmp}" "${receipt}"

echo "Fixed-work campaign complete: ${#seeds[@]} seeds × $(jq -r '.algorithms | length' "${contract}") algorithms × ${expected_cases} cases."
echo "Results: ${result_root}"
