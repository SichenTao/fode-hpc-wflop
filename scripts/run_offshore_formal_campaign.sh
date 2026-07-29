#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

contract="${OFFSHORE_CAMPAIGN_CONTRACT:-${repo_root}/formal/contracts/offshore_cpp_hpc_waffle_v1.json}"
build_dir="${OFFSHORE_BUILD_DIR:-${repo_root}/build-offshore-formal}"
result_dir="${OFFSHORE_RESULT_DIR:-${repo_root}/results/offshore_cpp_hpc_waffle_v1}"
assets="${GGA_ASSET_DIR:-${repo_root}/.source-cache/generated/gga_repaired}"
binary="${build_dir}/hpc/gga_cpp/gga_cpp_hpc"
workers="${WFLOP_WORKERS:-$(nproc)}"

if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the job." >&2
  exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaign requires a clean tracked worktree." >&2
  exit 2
fi
if [[ ! -f "${assets}/manifest.json" ]]; then
  echo "Frozen local GGA assets are missing: ${assets}" >&2
  exit 2
fi

cmake -S . -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$(command -v python3)"
cmake --build "${build_dir}" -j "${workers}" --target gga_cpp_hpc
ctest --test-dir "${build_dir}" --output-on-failure \
  -R 'gga_cpp_(evaluator_oracle|determinism|help)|geoga_cpp_declared_reconstruction|tmoea_cpp_nysted_reconstruction'
python3 scripts/validate_gga_problem_assets.py --assets "${assets}"

mkdir -p "${result_dir}"
environment_tmp="${result_dir}/environment.json.tmp"
environment_file="${result_dir}/environment.json"
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
  --arg asset_manifest_sha256 "$(sha256sum "${assets}/manifest.json" | cut -d' ' -f1)" \
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
    asset_manifest_sha256: $asset_manifest_sha256,
    contract_sha256: $contract_sha256
  }' > "${environment_tmp}"
mv "${environment_tmp}" "${environment_file}"

validate_result() {
  local path="$1"
  local algorithm="$2"
  local method="$3"
  local problem_id="$4"
  local case_id="$5"
  local seed="$6"
  local physical_fes="$7"
  local turbine_count="$8"
  jq -e \
    --arg algorithm "${algorithm}" \
    --arg method "${method}" \
    --arg problem_id "${problem_id}" \
    --arg case_id "${case_id}" \
    --argjson seed "${seed}" \
    --argjson physical_fes "${physical_fes}" \
    --argjson workers "${workers}" \
    --argjson turbine_count "${turbine_count}" \
    '.algorithm_id == $algorithm
     and .method_id == $method
     and .problem_id == $problem_id
     and .case_id == $case_id
     and .seed == $seed
     and .physical_fes == $physical_fes
     and .requested_workers == $workers
     and .observed_workers == $workers
     and (.best_aep_kwh | isfinite)
     and (.best_capacity_factor | isfinite)
     and (.best_layout_0based | length) == $turbine_count
     and ((.best_layout_0based | unique | length) == $turbine_count)
     and (
       if $algorithm == "gga" then
         (.best_lcoe | isfinite)
       elif $algorithm == "geoga" then
         .best_lcoe == null and .best_cable_cost == 0
       else
         .best_lcoe == null
         and .nondominated_count > 0
         and (.front | length) == .nondominated_count
       end
     )' "${path}" >/dev/null
}

mapfile -t seeds < <(jq -r '.seeds[]' "${contract}")
mapfile -t all_cases < <(jq -r '.cases[]' "${contract}")
for case_id in "${all_cases[@]}"; do
  problem="${assets}/${case_id}.wfp"
  if [[ ! -f "${problem}" ]]; then
    echo "Contract case has no frozen asset: ${problem}" >&2
    exit 2
  fi
  if ! jq -e --arg case_id "${case_id}" \
      'any(.cases[]; .case == $case_id)' \
      "${assets}/manifest.json" >/dev/null; then
    echo "Contract case is absent from the frozen asset manifest: ${case_id}" >&2
    exit 2
  fi
done
manifest_case_count="$(jq '.cases | length' "${assets}/manifest.json")"
if [[ "${#all_cases[@]}" -ne "${manifest_case_count}" ]]; then
  echo "Contract case count ${#all_cases[@]} != frozen manifest case count ${manifest_case_count}" >&2
  exit 2
fi
for profile_index in $(seq 0 "$(( $(jq '.profiles | length' "${contract}") - 1 ))"); do
  algorithm="$(jq -r ".profiles[${profile_index}].algorithm_id" "${contract}")"
  method="$(jq -r ".profiles[${profile_index}].method_id" "${contract}")"
  problem_id="$(jq -r ".profiles[${profile_index}].problem_id" "${contract}")"
  physical_fes="$(jq -r ".profiles[${profile_index}].physical_fes_per_run" "${contract}")"
  if [[ "$(jq -r ".profiles[${profile_index}].cases | type" "${contract}")" == "string" ]]; then
    cases=("${all_cases[@]}")
  else
    mapfile -t cases < <(jq -r ".profiles[${profile_index}].cases[]" "${contract}")
  fi
  for case_id in "${cases[@]}"; do
    problem="${assets}/${case_id}.wfp"
    if [[ ! -f "${problem}" ]]; then
      echo "Missing frozen case: ${problem}" >&2
      exit 2
    fi
    turbine_count="$(awk '$1 == "turbine_count" {print $2; exit}' "${problem}")"
    for seed in "${seeds[@]}"; do
      output="${result_dir}/${algorithm}__${case_id}__seed${seed}.json"
      if [[ -s "${output}" ]] && validate_result \
          "${output}" "${algorithm}" "${method}" "${problem_id}" \
          "${case_id}" "${seed}" "${physical_fes}" "${turbine_count}"; then
        continue
      fi
      "${binary}" \
        --algorithm "${algorithm}" \
        --problem "${problem}" \
        --physical-fes "${physical_fes}" \
        --workers "${workers}" \
        --seed "${seed}" \
        --output "${output}"
      validate_result \
        "${output}" "${algorithm}" "${method}" "${problem_id}" \
        "${case_id}" "${seed}" "${physical_fes}" "${turbine_count}"
    done
  done
done

result_count="$(find "${result_dir}" -maxdepth 1 -name '*.json' \
  ! -name 'environment.json' ! -name 'campaign_receipt.json' | wc -l)"
expected_count="$(jq -r '.formal_run_count' "${contract}")"
if [[ "${result_count}" -ne "${expected_count}" ]]; then
  echo "Formal result count ${result_count} != ${expected_count}" >&2
  exit 2
fi
manifest="${result_dir}/manifest.sha256"
find "${result_dir}" -maxdepth 1 -name '*.json' \
  ! -name 'campaign_receipt.json' -type f -print0 \
  | sort -z | xargs -0 sha256sum > "${manifest}"
receipt_tmp="${result_dir}/campaign_receipt.json.tmp"
receipt="${result_dir}/campaign_receipt.json"
jq -n \
  --arg campaign_id "$(jq -r '.campaign_id' "${contract}")" \
  --argjson formal_runs "${result_count}" \
  --argjson complete_layout_evaluations "$(jq -r '.formal_complete_layout_evaluations' "${contract}")" \
  --arg manifest_sha256 "$(sha256sum "${manifest}" | cut -d' ' -f1)" \
  '{
    schema_version: 1,
    campaign_id: $campaign_id,
    formal_runs: $formal_runs,
    complete_layout_evaluations: $complete_layout_evaluations,
    manifest_sha256: $manifest_sha256,
    status: "complete_file_matrix",
    evidence_boundary: "Quality and statistical claims require a separate analysis receipt."
  }' > "${receipt_tmp}"
mv "${receipt_tmp}" "${receipt}"
echo "Offshore formal campaign complete: ${result_count} runs."
