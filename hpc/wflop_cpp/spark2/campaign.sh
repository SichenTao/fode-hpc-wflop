#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/../../.." && pwd)"
build_root="${project_root}/hpc/build/wflop_cpp_spark2"
sanitizer_root="${project_root}/hpc/build/wflop_cpp_spark2_sanitizers"
binary="${build_root}/wflop_cpp_hpc"
cases="${project_root}/shared/contracts/benchmark_cases.json"
models="${project_root}/shared/models/sugga_cpp"
freeze="${project_root}/formal/evidence/eight_algorithm_native_cpp_spark2_final_freeze.json"
campaign="${project_root}/formal/contracts/eight_algorithm_cpp_hpc_spark2_v2.json"
p0_receipt="${project_root}/shared/contracts/p0_freeze_receipt.json"
receipt_filter="${project_root}/hpc/wflop_cpp/analysis/performance_receipt.jq"
admission_root="${project_root}/results/admission/wflop_cpp_hpc_spark2_final_50_cases_seed20260728"
performance_root="${project_root}/results/performance/eight_algorithm_cpp_spark2_final_1_vs_20"
formal_root="${project_root}/results/formal/eight_algorithm_cpp_hpc_spark2_final_25_seeds"
load_log="${performance_root}/external_load.tsv"
build_receipt="${build_root}/spark2_build_receipt.txt"
admission_receipt="${admission_root}/admission_receipt.json"
performance_receipt="${performance_root}/performance_receipt.json"
algorithms=(fode aga sugga ise agpso cgpso lshade clshade)
representative_cases=(WS1tn10 WS5tn30 WS10tn80)
expected_host="spark-9ab3"
workers=20
affinity="0-19"
monitor_pid=""

usage() {
  cat <<'EOF'
Usage: campaign.sh {build|admission|performance|formal|status}

build        verify the frozen source, build Release and sanitizer binaries,
             and run the bounded CTest suite on Spark2
admission    run eight algorithms x 50 cases x one registered seed
performance run paired 1-worker and 20-worker timing on three cases
formal       run eight algorithms x 50 cases x 25 independent seeds
status       print the receipts and completed-result counts
EOF
}

require_host() {
  if [[ "$(hostname -s)" != "${expected_host}" ]]; then
    echo "error: Spark2 campaign requires hostname ${expected_host}" >&2
    exit 1
  fi
  if [[ "$(uname -m)" != "aarch64" ]]; then
    echo "error: Spark2 campaign requires the frozen aarch64 host" >&2
    exit 1
  fi
  if [[ "$(nproc)" -ne "${workers}" ]]; then
    echo "error: Spark2 must expose exactly ${workers} physical CPU cores" >&2
    exit 1
  fi
  if [[ "$(taskset -pc $$ | sed 's/.*: //')" != "${affinity}" ]]; then
    echo "error: Spark2 campaign shell must see CPU affinity ${affinity}" >&2
    exit 1
  fi
}

source_sha256() {
  (
    cd "${project_root}"
    {
      find \
        hpc/fode_cpp/include \
        hpc/wflop_cpp/include \
        hpc/wflop_cpp/src \
        -type f \( -name '*.hpp' -o -name '*.cpp' \) -print
      printf '%s\n' \
        hpc/fode_cpp/src/case_loader.cpp \
        hpc/fode_cpp/src/evaluator.cpp \
        hpc/fode_cpp/src/executor.cpp \
        hpc/fode_cpp/src/optimizer.cpp
    } | sort -u | xargs sha256sum | sha256sum | cut -d' ' -f1
  )
}

model_sha256() {
  (
    cd "${project_root}"
    find shared/models/sugga_cpp -type f -print0 \
      | sort -z \
      | xargs -0 sha256sum \
      | sha256sum \
      | cut -d' ' -f1
  )
}

workflow_sha256() {
  (
    cd "${project_root}"
    sha256sum \
      hpc/wflop_cpp/spark2/*.sh \
      hpc/wflop_cpp/analysis/performance_receipt.jq \
      | sha256sum \
      | cut -d' ' -f1
  )
}

require_frozen_inputs() {
  local source_sha model_sha cmake_sha p0_sha campaign_sha workflow_sha
  if [[ ! -f "${freeze}" || ! -f "${campaign}" \
      || ! -f "${p0_receipt}" ]]; then
    echo "error: final Spark2 source freeze, campaign contract or P0 receipt is missing" >&2
    exit 1
  fi
  source_sha="$(source_sha256)"
  model_sha="$(model_sha256)"
  cmake_sha="$(sha256sum \
    "${project_root}/hpc/wflop_cpp/CMakeLists.txt" | cut -d' ' -f1)"
  p0_sha="$(sha256sum "${p0_receipt}" | cut -d' ' -f1)"
  campaign_sha="$(sha256sum "${campaign}" | cut -d' ' -f1)"
  workflow_sha="$(workflow_sha256)"
  jq -e \
    --arg source "${source_sha}" \
    --arg models "${model_sha}" \
    --arg cmake "${cmake_sha}" \
    --arg p0 "${p0_sha}" \
    --arg campaign "${campaign_sha}" \
    --arg workflow "${workflow_sha}" \
    '.status == "frozen_for_spark2_final_campaign"
     and .source_aggregate.sha256 == $source
     and .sugga_native_model_aggregate.sha256 == $models
     and .build_contract.sha256 == $cmake
     and .paper_and_semantics_contract.sha256 == $p0
     and .campaign_contract.sha256 == $campaign
     and .workflow_aggregate.sha256 == $workflow
     and .formal_host_contract.hostname == "spark-9ab3"
     and .formal_host_contract.visible_physical_cores == 20
     and .formal_host_contract.workers_per_optimization == 20
     and .formal_host_contract.cpu_affinity == "0-19"' \
    "${freeze}" >/dev/null
  (
    cd "${project_root}"
    jq -r \
      '.contracts[] | "\(.sha256)  \(.path)"' \
      shared/contracts/p0_freeze_receipt.json \
      | sha256sum -c -
    jq -r \
      '.paper_pdf_receipt | "\(.sha256)  \(.path)"' \
      shared/contracts/p0_freeze_receipt.json \
      | sha256sum -c -
  )
}

require_build_identity() {
  local binary_sha freeze_sha workflow_sha
  if [[ ! -x "${binary}" || ! -f "${build_receipt}" ]]; then
    echo "error: frozen Spark2 Release binary or build receipt is missing" >&2
    exit 1
  fi
  binary_sha="$(sha256sum "${binary}" | cut -d' ' -f1)"
  freeze_sha="$(sha256sum "${freeze}" | cut -d' ' -f1)"
  workflow_sha="$(workflow_sha256)"
  if ! grep -Fxq "binary_sha256=${binary_sha}" "${build_receipt}" \
      || ! grep -Fxq "source_freeze_sha256=${freeze_sha}" "${build_receipt}" \
      || ! grep -Fxq "workflow_sha256=${workflow_sha}" "${build_receipt}"; then
    echo "error: Spark2 binary or workflow differs from the build receipt" >&2
    exit 1
  fi
}

external_user_cpu() {
  ps -eo uid=,pcpu= \
    | awk -v owner_uid="$(id -u)" \
      '$1 != owner_uid && $1 != 0 && $1 != 126 {
         total += $2 + 0.0
       }
       END {printf "%.3f\n", total + 0.0}'
}

require_external_users_quiet() {
  local total
  total="$(external_user_cpu)"
  if awk -v total="${total}" 'BEGIN {exit !(total >= 25.0)}'; then
    echo "error: external non-owner users consume ${total}% of one CPU or more; timing gate stopped" >&2
    ps -eo user=,pid=,pcpu=,pmem=,etime=,comm=,args= \
      --sort=-pcpu | head -15 >&2
    exit 1
  fi
}

start_external_load_monitor() {
  mkdir -p "${performance_root}"
  printf 'unix_time\tload1\texternal_user_cpu_percent_of_one_core\n' \
    > "${load_log}"
  (
    while true; do
      printf '%s\t%s\t%s\n' \
        "$(date +%s)" \
        "$(cut -d' ' -f1 /proc/loadavg)" \
        "$(external_user_cpu)" \
        >> "${load_log}"
      sleep 1
    done
  ) &
  monitor_pid="$!"
}

stop_external_load_monitor() {
  if [[ -n "${monitor_pid}" ]] && kill -0 "${monitor_pid}" 2>/dev/null; then
    kill "${monitor_pid}" 2>/dev/null || true
    wait "${monitor_pid}" 2>/dev/null || true
  fi
  monitor_pid=""
}

augment_result() {
  local raw="$1"
  local destination="$2"
  local binary_sha="$3"
  local freeze_sha="$4"
  jq -c \
    --arg binary_sha256 "${binary_sha}" \
    --arg source_freeze_sha256 "${freeze_sha}" \
    --arg execution_host "${expected_host}" \
    '. + {
      binary_sha256: $binary_sha256,
      source_freeze_sha256: $source_freeze_sha256,
      execution_host: $execution_host
    }' \
    "${raw}" > "${destination}"
}

run_build() {
  local source_sha model_sha cmake_sha p0_sha campaign_sha workflow_sha
  require_host
  require_frozen_inputs
  source_sha="$(source_sha256)"
  model_sha="$(model_sha256)"
  cmake_sha="$(sha256sum \
    "${project_root}/hpc/wflop_cpp/CMakeLists.txt" | cut -d' ' -f1)"
  p0_sha="$(sha256sum "${p0_receipt}" | cut -d' ' -f1)"
  campaign_sha="$(sha256sum "${campaign}" | cut -d' ' -f1)"
  workflow_sha="$(workflow_sha256)"

  cmake \
    -S "${project_root}/hpc/wflop_cpp" \
    -B "${build_root}" \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "${build_root}" --parallel "${workers}"
  ctest --test-dir "${build_root}" --output-on-failure

  cmake \
    -S "${project_root}/hpc/wflop_cpp" \
    -B "${sanitizer_root}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
  cmake --build "${sanitizer_root}" --parallel "${workers}"
  ASAN_OPTIONS=detect_leaks=1 \
    UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir "${sanitizer_root}" --output-on-failure

  {
    echo "hostname=$(hostname -s)"
    echo "architecture=$(uname -m)"
    echo "online_physical_cores=$(nproc)"
    echo "cpu_affinity=$(taskset -pc $$ | sed 's/.*: //')"
    echo "compiler=$(c++ --version | head -n 1)"
    echo "cmake=$(cmake --version | head -n 1)"
    echo "binary_sha256=$(sha256sum "${binary}" | cut -d' ' -f1)"
    echo "source_sha256=${source_sha}"
    echo "sugga_model_sha256=${model_sha}"
    echo "cmake_sha256=${cmake_sha}"
    echo "p0_receipt_sha256=${p0_sha}"
    echo "campaign_contract_sha256=${campaign_sha}"
    echo "workflow_sha256=${workflow_sha}"
    echo "source_freeze_sha256=$(sha256sum "${freeze}" | cut -d' ' -f1)"
    echo "release_ctest=pass"
    echo "asan_ubsan_ctest=pass"
  } > "${build_receipt}"
  echo "PASS ${build_receipt}"
}

run_admission() {
  local binary_sha freeze_sha
  require_host
  require_frozen_inputs
  require_build_identity
  binary_sha="$(sha256sum "${binary}" | cut -d' ' -f1)"
  freeze_sha="$(sha256sum "${freeze}" | cut -d' ' -f1)"
  mkdir -p "${admission_root}"

  for algorithm in "${algorithms[@]}"; do
    local output temporary raw
    output="${admission_root}/${algorithm}.jsonl"
    if [[ -s "${output}" ]] \
        && [[ "$(wc -l < "${output}")" -eq 50 ]] \
        && jq -e -s \
          --arg algorithm "${algorithm}" \
          --arg binary_sha256 "${binary_sha}" \
          --arg source_freeze_sha256 "${freeze_sha}" \
          --arg execution_host "${expected_host}" \
          'length == 50
           and all(.algorithm_id == $algorithm)
           and all(.seed == 20260728)
           and all(.physical_fes == 24000)
           and all(.requested_workers == 20)
           and all(.observed_workers == 20)
           and all(.binary_sha256 == $binary_sha256)
           and all(.source_freeze_sha256 == $source_freeze_sha256)
           and all(.execution_host == $execution_host)
           and ([.[].case_id] | unique | length == 50)' \
          "${output}" >/dev/null; then
      echo "reuse ${algorithm}: existing 50-case Spark2 receipt passes"
      continue
    fi
    output="${admission_root}/${algorithm}.jsonl"
    temporary="${output}.partial"
    raw="${output}.raw"
    taskset -c "${affinity}" "${binary}" \
      --algorithm "${algorithm}" \
      --all-cases \
      --cases "${cases}" \
      --models "${models}" \
      --physical-fes 24000 \
      --seed 20260728 \
      --workers "${workers}" \
      --output "${raw}"
    augment_result "${raw}" "${temporary}" "${binary_sha}" "${freeze_sha}"
    rm "${raw}"
    jq -e -s \
      --arg algorithm "${algorithm}" \
      --arg binary_sha256 "${binary_sha}" \
      --arg source_freeze_sha256 "${freeze_sha}" \
      --arg execution_host "${expected_host}" \
      'length == 50
       and all(.algorithm_id == $algorithm)
       and all(.seed == 20260728)
       and all(.physical_fes == 24000)
       and all(.requested_workers == 20)
       and all(.observed_workers == 20)
       and all(.best_expected_power_kw > 0)
       and all(.binary_sha256 == $binary_sha256)
       and all(.source_freeze_sha256 == $source_freeze_sha256)
       and all(.execution_host == $execution_host)
       and ([.[].case_id] | unique | length == 50)' \
      "${temporary}" >/dev/null
    mv "${temporary}" "${output}"
    echo "pass ${algorithm}: 50 cases"
  done

  jq -s \
    --arg binary_sha256 "${binary_sha}" \
    --arg source_freeze_sha256 "${freeze_sha}" \
    --arg execution_host "${expected_host}" \
    '{
      schema_version: 1,
      gate: "spark2_eight_algorithms_x_50_cases_x_one_seed",
      result_count: length,
      algorithms: ([.[].algorithm_id] | unique | sort),
      cases: ([.[].case_id] | unique | sort),
      physical_fes_values: ([.[].physical_fes] | unique),
      requested_workers: ([.[].requested_workers] | unique),
      observed_workers: ([.[].observed_workers] | unique),
      execution_hosts: ([.[].execution_host] | unique),
      binary_sha256: $binary_sha256,
      source_freeze_sha256: $source_freeze_sha256,
      status: (
        length == 400
        and ([.[].algorithm_id] | unique | length == 8)
        and ([.[].case_id] | unique | length == 50)
        and ([.[] | "\(.algorithm_id)|\(.case_id)|\(.seed)"]
          | unique | length == 400)
        and ([.[].seed] | unique) == [20260728]
        and ([.[].physical_fes] | unique) == [24000]
        and ([.[].requested_workers] | unique) == [20]
        and ([.[].observed_workers] | unique) == [20]
        and ([.[].execution_host] | unique) == [$execution_host]
        and all(.[]; .best_expected_power_kw > 0)
      )
    }' "${admission_root}"/*.jsonl > "${admission_receipt}"
  jq -e '.status == true' "${admission_receipt}" >/dev/null
  sha256sum "${admission_root}"/*.jsonl \
    > "${admission_root}/result_files.sha256"
  echo "PASS ${admission_receipt}"
}

run_performance() {
  local binary_sha freeze_sha
  require_host
  require_frozen_inputs
  require_build_identity
  binary_sha="$(sha256sum "${binary}" | cut -d' ' -f1)"
  freeze_sha="$(sha256sum "${freeze}" | cut -d' ' -f1)"
  if [[ ! -f "${admission_receipt}" ]] \
      || ! jq -e \
        --arg binary_sha256 "${binary_sha}" \
        --arg source_freeze_sha256 "${freeze_sha}" \
        --arg execution_host "${expected_host}" \
        '.status == true
         and .result_count == 400
         and .binary_sha256 == $binary_sha256
         and .source_freeze_sha256 == $source_freeze_sha256
         and .execution_hosts == [$execution_host]' \
        "${admission_receipt}" >/dev/null; then
    echo "error: identical Spark2 binary has not passed the 400-result admission gate" >&2
    exit 1
  fi
  for sample in 1 2 3 4; do
    require_external_users_quiet
    if [[ "${sample}" -lt 4 ]]; then
      sleep 5
    fi
  done

  mkdir -p "${performance_root}"
  start_external_load_monitor
  trap stop_external_load_monitor EXIT
  for algorithm in "${algorithms[@]}"; do
    for case_id in "${representative_cases[@]}"; do
      for repeat in $(seq 1 21); do
        local endpoints
        if (( repeat % 2 == 1 )); then
          endpoints=(cpp1 cpp20)
        else
          endpoints=(cpp20 cpp1)
        fi
        for endpoint in "${endpoints[@]}"; do
          local endpoint_workers endpoint_affinity output temporary raw
          if [[ "${endpoint}" == "cpp1" ]]; then
            endpoint_workers=1
            endpoint_affinity=0
          else
            endpoint_workers=20
            endpoint_affinity="${affinity}"
          fi
          output="${performance_root}/${algorithm}_${case_id}_${endpoint}_r$(printf '%02d' "${repeat}").json"
          if [[ -s "${output}" ]] \
              && jq -e \
                --arg algorithm "${algorithm}" \
                --arg case_id "${case_id}" \
                --arg endpoint "${endpoint}" \
                --argjson repeat "${repeat}" \
                --argjson endpoint_workers "${endpoint_workers}" \
                --arg binary_sha256 "${binary_sha}" \
                --arg source_freeze_sha256 "${freeze_sha}" \
                '.algorithm_id == $algorithm
                 and .case_id == $case_id
                 and .performance_endpoint == $endpoint
                 and .repeat == $repeat
                 and .physical_fes == 24000
                 and .requested_workers == $endpoint_workers
                 and .observed_workers == $endpoint_workers
                 and .binary_sha256 == $binary_sha256
                 and .source_freeze_sha256 == $source_freeze_sha256' \
                "${output}" >/dev/null; then
            continue
          fi
          require_external_users_quiet
          temporary="${output}.partial"
          raw="${output}.raw"
          taskset -c "${endpoint_affinity}" "${binary}" \
            --algorithm "${algorithm}" \
            --case "${case_id}" \
            --cases "${cases}" \
            --models "${models}" \
            --physical-fes 24000 \
            --seed 20260728 \
            --workers "${endpoint_workers}" \
            --output "${raw}"
          jq \
            --arg endpoint "${endpoint}" \
            --argjson repeat "${repeat}" \
            --arg binary_sha256 "${binary_sha}" \
            --arg source_freeze_sha256 "${freeze_sha}" \
            --arg execution_host "${expected_host}" \
            '. + {
              performance_endpoint: $endpoint,
              repeat: $repeat,
              binary_sha256: $binary_sha256,
              source_freeze_sha256: $source_freeze_sha256,
              execution_host: $execution_host
            }' \
            "${raw}" > "${temporary}"
          rm "${raw}"
          mv "${temporary}" "${output}"
        done
      done
    done
  done
  stop_external_load_monitor
  trap - EXIT

  jq -s \
    --arg binary_sha256 "${binary_sha}" \
    --arg source_freeze_sha256 "${freeze_sha}" \
    -f "${receipt_filter}" \
    "${performance_root}"/*_r[0-9][0-9].json \
    > "${performance_receipt}"
  jq \
    --arg execution_host "${expected_host}" \
    --arg external_load_log_sha256 "$(sha256sum "${load_log}" | cut -d' ' -f1)" \
    '. + {
      execution_host: $execution_host,
      external_load_log_sha256: $external_load_log_sha256
    }' \
    "${performance_receipt}" > "${performance_receipt}.tmp"
  mv "${performance_receipt}.tmp" "${performance_receipt}"
  jq -e \
    --arg execution_host "${expected_host}" \
    '.status == true
     and .result_count == 1008
     and .execution_host == $execution_host' \
    "${performance_receipt}" >/dev/null
  sha256sum "${performance_root}"/*_r[0-9][0-9].json \
    > "${performance_root}/result_files.sha256"
  echo "PASS ${performance_receipt}"
}

run_formal() {
  local binary_sha freeze_sha
  require_host
  require_frozen_inputs
  require_build_identity
  binary_sha="$(sha256sum "${binary}" | cut -d' ' -f1)"
  freeze_sha="$(sha256sum "${freeze}" | cut -d' ' -f1)"
  if [[ ! -f "${admission_receipt}" ]] \
      || ! jq -e \
        --arg binary_sha256 "${binary_sha}" \
        --arg source_freeze_sha256 "${freeze_sha}" \
        --arg execution_host "${expected_host}" \
        '.status == true
         and .result_count == 400
         and .binary_sha256 == $binary_sha256
         and .source_freeze_sha256 == $source_freeze_sha256
         and .execution_hosts == [$execution_host]' \
        "${admission_receipt}" >/dev/null; then
    echo "error: the Spark2 400-result admission gate has not passed" >&2
    exit 1
  fi
  if [[ ! -f "${performance_receipt}" ]] \
      || ! jq -e \
        --arg binary_sha256 "${binary_sha}" \
        --arg source_freeze_sha256 "${freeze_sha}" \
        --arg execution_host "${expected_host}" \
        '.status == true
         and .result_count == 1008
         and .binary_sha256 == $binary_sha256
         and .source_freeze_sha256 == $source_freeze_sha256
         and .execution_host == $execution_host' \
        "${performance_receipt}" >/dev/null; then
    echo "error: the Spark2 paired 1-worker/20-worker performance gate has not passed" >&2
    exit 1
  fi
  mkdir -p "${formal_root}"

  for seed_index in $(seq 1 25); do
    local seed seed_root
    seed=$((2026072800 + seed_index))
    seed_root="${formal_root}/seed_${seed}"
    mkdir -p "${seed_root}"
    for algorithm in "${algorithms[@]}"; do
      local output temporary raw
      output="${seed_root}/${algorithm}.jsonl"
      if [[ -s "${output}" ]] \
          && [[ "$(wc -l < "${output}")" -eq 50 ]] \
          && jq -e -s \
            --arg algorithm "${algorithm}" \
            --argjson seed "${seed}" \
            --arg binary_sha256 "${binary_sha}" \
            --arg source_freeze_sha256 "${freeze_sha}" \
            --arg execution_host "${expected_host}" \
            'length == 50
             and all(.algorithm_id == $algorithm)
             and all(.seed == $seed)
             and all(.physical_fes == 24000)
             and all(.requested_workers == 20)
             and all(.observed_workers == 20)
             and all(.binary_sha256 == $binary_sha256)
             and all(.source_freeze_sha256 == $source_freeze_sha256)
             and all(.execution_host == $execution_host)
             and ([.[].case_id] | unique | length == 50)' \
            "${output}" >/dev/null; then
        echo "reuse seed=${seed} algorithm=${algorithm}"
        continue
      fi
      temporary="${output}.partial"
      raw="${output}.raw"
      taskset -c "${affinity}" "${binary}" \
        --algorithm "${algorithm}" \
        --all-cases \
        --cases "${cases}" \
        --models "${models}" \
        --physical-fes 24000 \
        --seed "${seed}" \
        --workers "${workers}" \
        --output "${raw}"
      augment_result "${raw}" "${temporary}" "${binary_sha}" "${freeze_sha}"
      rm "${raw}"
      jq -e -s \
        --arg algorithm "${algorithm}" \
        --argjson seed "${seed}" \
        --arg binary_sha256 "${binary_sha}" \
        --arg source_freeze_sha256 "${freeze_sha}" \
        --arg execution_host "${expected_host}" \
        'length == 50
         and all(.algorithm_id == $algorithm)
         and all(.seed == $seed)
         and all(.physical_fes == 24000)
         and all(.requested_workers == 20)
         and all(.observed_workers == 20)
         and all(.best_expected_power_kw > 0)
         and all(.binary_sha256 == $binary_sha256)
         and all(.source_freeze_sha256 == $source_freeze_sha256)
         and all(.execution_host == $execution_host)
         and ([.[].case_id] | unique | length == 50)' \
        "${temporary}" >/dev/null
      mv "${temporary}" "${output}"
      echo "pass seed=${seed} algorithm=${algorithm}"
    done
  done

  find "${formal_root}" -mindepth 2 -maxdepth 2 -name '*.jsonl' -print0 \
    | sort -z \
    | xargs -0 jq -s \
    --arg binary_sha256 "${binary_sha}" \
    --arg source_freeze_sha256 "${freeze_sha}" \
    --arg execution_host "${expected_host}" \
    --arg admission_receipt_sha256 "$(
      sha256sum "${admission_receipt}" | cut -d' ' -f1
    )" \
    --arg performance_receipt_sha256 "$(
      sha256sum "${performance_receipt}" | cut -d' ' -f1
    )" \
    '{
      schema_version: 1,
      gate: "spark2_eight_algorithms_x_50_cases_x_25_seeds",
      result_count: length,
      algorithm_count: ([.[].algorithm_id] | unique | length),
      case_count: ([.[].case_id] | unique | length),
      seed_count: ([.[].seed] | unique | length),
      physical_fes_values: ([.[].physical_fes] | unique),
      requested_workers: ([.[].requested_workers] | unique),
      observed_workers: ([.[].observed_workers] | unique),
      execution_hosts: ([.[].execution_host] | unique),
      binary_sha256: $binary_sha256,
      source_freeze_sha256: $source_freeze_sha256,
      admission_receipt_sha256: $admission_receipt_sha256,
      performance_receipt_sha256: $performance_receipt_sha256,
      status: (
        length == 10000
        and ([.[].algorithm_id] | unique | length == 8)
        and ([.[].case_id] | unique | length == 50)
        and ([.[].seed] | unique | length == 25)
        and ([.[] | "\(.algorithm_id)|\(.case_id)|\(.seed)"]
          | unique | length == 10000)
        and ([.[].seed] | unique) == [range(2026072801; 2026072826)]
        and ([.[].physical_fes] | unique) == [24000]
        and ([.[].requested_workers] | unique) == [20]
        and ([.[].observed_workers] | unique) == [20]
        and ([.[].execution_host] | unique) == [$execution_host]
        and all(.[]; .best_expected_power_kw > 0)
      )
    }' > "${formal_root}/formal_receipt.json"
  jq -e '.status == true' "${formal_root}/formal_receipt.json" >/dev/null
  find "${formal_root}" -mindepth 2 -maxdepth 2 -name '*.jsonl' -print0 \
    | sort -z \
    | xargs -0 sha256sum > "${formal_root}/result_files.sha256"
  echo "PASS ${formal_root}/formal_receipt.json"
}

show_status() {
  printf 'host=%s\n' "$(hostname -s)"
  printf 'build_receipt=%s\n' "$(
    [[ -f "${build_receipt}" ]] && echo present || echo absent
  )"
  printf 'admission_files=%s\n' "$(
    find "${admission_root}" -maxdepth 1 -name '*.jsonl' 2>/dev/null | wc -l
  )"
  printf 'admission_records=%s\n' "$(
    find "${admission_root}" -maxdepth 1 -name '*.jsonl' -print0 2>/dev/null \
      | xargs -0 -r cat | wc -l
  )"
  printf 'performance_records=%s\n' "$(
    find "${performance_root}" -maxdepth 1 -name '*_r[0-9][0-9].json' \
      2>/dev/null | wc -l
  )"
  printf 'formal_files=%s\n' "$(
    find "${formal_root}" -mindepth 2 -maxdepth 2 -name '*.jsonl' \
      2>/dev/null | wc -l
  )"
  printf 'formal_records=%s\n' "$(
    find "${formal_root}" -mindepth 2 -maxdepth 2 -name '*.jsonl' -print0 \
      2>/dev/null | xargs -0 -r cat | wc -l
  )"
  for receipt in \
    "${admission_receipt}" \
    "${performance_receipt}" \
    "${formal_root}/formal_receipt.json"; do
    if [[ -f "${receipt}" ]]; then
      jq -c '{gate, result_count, status}' "${receipt}"
    fi
  done
}

if [[ "$#" -ne 1 ]]; then
  usage >&2
  exit 2
fi
case "$1" in
  build) run_build ;;
  admission) run_admission ;;
  performance) run_performance ;;
  formal) run_formal ;;
  status) show_status ;;
  *)
    usage >&2
    exit 2
    ;;
esac
