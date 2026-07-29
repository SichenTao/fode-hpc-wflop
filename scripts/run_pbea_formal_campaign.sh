#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Formal campaign requires a clean tracked worktree." >&2
  exit 2
fi

workers="${WFLOP_WORKERS:-$(nproc)}"
if [[ "${workers}" != "$(nproc)" ]]; then
  echo "WFLOP_WORKERS must equal all processors visible to the frozen job." >&2
  exit 2
fi

build_dir="${PBEA_BUILD_DIR:-build-pbea-formal}"
result_dir="${PBEA_RESULT_DIR:-results/pbea_six_algorithm_waffle_v1}"
contract="${PBEA_CAMPAIGN_CONTRACT:-formal/contracts/pbea_six_algorithm_waffle_v1.json}"
binary="${build_dir}/hpc/pbea_cpp/pbea_cpp_hpc"

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" -j "${workers}" --target pbea_cpp_hpc
ctest --test-dir "${build_dir}" --output-on-failure \
  -R 'pbea_cpp_(evaluator_oracle|determinism|front_artifacts|help)'

mkdir -p "${result_dir}"
environment_tmp="${result_dir}/environment.json.tmp"
environment_file="${result_dir}/environment.json"
python3 - "${binary}" "${workers}" "${contract}" >"${environment_tmp}" <<'PY'
import hashlib
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

binary = Path(sys.argv[1])
workers = int(sys.argv[2])
contract = Path(sys.argv[3])
campaign = json.loads(contract.read_text())

def command(*args):
    return subprocess.check_output(args, text=True).strip()

print(json.dumps({
    "schema_version": 1,
    "campaign_id": campaign["campaign_id"],
    "host": platform.node(),
    "git_head": command("git", "rev-parse", "HEAD"),
    "git_status_tracked": command(
        "git", "status", "--porcelain", "--untracked-files=no"
    ),
    "compiler": command("c++", "--version").splitlines()[0],
    "cmake": command("cmake", "--version").splitlines()[0],
    "python": platform.python_version(),
    "python_packages": command(
        sys.executable, "-m", "pip", "freeze"
    ),
    "nproc": int(command("nproc")),
    "workers": workers,
    "affinity": command("taskset", "-pc", str(os.getpid())),
    "lscpu": command("lscpu"),
    "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "contract_sha256": hashlib.sha256(contract.read_bytes()).hexdigest(),
}, indent=2))
PY
mv "${environment_tmp}" "${environment_file}"

mapfile -t algorithms < <(jq -r '.algorithms[]' "${contract}")
mapfile -t scenarios < <(jq -r '.wind_scenarios[]' "${contract}")
mapfile -t turbine_counts < <(jq -r '.turbine_counts[]' "${contract}")
population="$(jq -r '.population_size' "${contract}")"
generations="$(jq -r '.offspring_generations' "${contract}")"
repeat_count="$(jq -r '.repeat_count' "${contract}")"
seed_base="$(jq -r '.seed_base' "${contract}")"
for algorithm in "${algorithms[@]}"; do
  for scenario in "${scenarios[@]}"; do
    for turbines in "${turbine_counts[@]}"; do
      for repeat in $(seq 1 "${repeat_count}"); do
        seed=$((seed_base + repeat))
        stem="${algorithm}__${scenario}__tn${turbines}__seed${seed}"
        front="${result_dir}/${stem}.front.json"
        summary="${result_dir}/${stem}.summary.json"
        if python3 scripts/validate_pbea_formal_run.py \
          --front "${front}" --summary "${summary}" \
          --algorithm "${algorithm}" --scenario "${scenario}" \
          --turbines "${turbines}" --seed "${seed}" \
          >/dev/null 2>&1; then
          continue
        fi
        summary_tmp="${summary}.tmp"
        rm -f "${summary_tmp}"
        "${binary}" \
          --algorithm "${algorithm}" \
          --scenario "${scenario}" \
          --turbines "${turbines}" \
          --population "${population}" \
          --generations "${generations}" \
          --workers "${workers}" \
          --seed "${seed}" \
          --ipd 3 \
          --mu-c 80 \
          --output-front "${front}" \
          >"${summary_tmp}"
        mv "${summary_tmp}" "${summary}"
        python3 scripts/validate_pbea_formal_run.py \
          --front "${front}" --summary "${summary}" \
          --algorithm "${algorithm}" --scenario "${scenario}" \
          --turbines "${turbines}" --seed "${seed}" \
          >/dev/null
      done
    done
  done
done

python3 scripts/finalize_pbea_campaign.py \
  --contract "${contract}" \
  --results "${result_dir}" \
  --receipt "${result_dir}/campaign_file_receipt.json"
