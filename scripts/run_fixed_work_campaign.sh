#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${WFLOP_BUILD_DIR:-${repo_root}/build}"
binary="${build_dir}/hpc/wflop_cpp/wflop_cpp_hpc"
contract="${repo_root}/formal/contracts/eight_algorithm_cpp_hpc_spark2_v2.json"
result_root="${WFLOP_RESULT_DIR:-${repo_root}/results/fixed-work}"
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

mkdir -p "${result_root}"
physical_fes="$(jq -r '.physical_fes_per_run' "${contract}")"
mapfile -t seeds < <(jq -r '.seeds[]' "${contract}")

validate_seed_file() {
  local path="$1"
  local seed="$2"
  jq -se \
    --argjson seed "${seed}" \
    --argjson fes "${physical_fes}" \
    --argjson workers "${workers}" \
    'length == 400
     and ([.[].algorithm_id] | unique | length) == 8
     and ([.[].case_id] | unique | length) == 50
     and all(.[];
       .seed == $seed
       and .physical_fes == $fes
       and .requested_workers == $workers
       and .observed_workers == $workers
       and (.best_expected_power_kw | isfinite)
       and (.best_layout_1based | length > 0))
     and ([.[] | [.algorithm_id, .case_id] | join(":")]
          | unique | length) == 400' \
    "${path}" >/dev/null
}

for seed in "${seeds[@]}"; do
  output="${result_root}/seed_${seed}.jsonl"
  if [[ -s "${output}" ]] && validate_seed_file "${output}" "${seed}"; then
    echo "reuse seed ${seed}: 400-result receipt passes"
    continue
  fi
  temporary="${output}.partial"
  rm -f "${temporary}"
  OMP_DYNAMIC=FALSE OMP_NUM_THREADS="${workers}" \
    "${binary}" \
      --all-algorithms \
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

echo "Fixed-work campaign complete: ${#seeds[@]} seeds × 8 algorithms × 50 cases."
echo "Results: ${result_root}"
