#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

host="$(hostname -s)"
if [[ "${host,,}" != "waffle" ]]; then
  echo "Formal suite launcher is restricted to hostname waffle; got ${host}." >&2
  exit 2
fi
workers="$(nproc)"
control_dir="${repo_root}/results/waffle_campaign_suite_v1/control"
status_file="${control_dir}/status.json"
lock_file="${control_dir}/suite.lock"
mkdir -p "${control_dir}"

exec 9>"${lock_file}"
if ! flock -n 9; then
  echo "A Waffle formal suite process already holds ${lock_file}." >&2
  exit 3
fi

started_at="$(date --iso-8601=seconds)"
git_head="$(git rev-parse HEAD)"
write_status() {
  local state="$1"
  local exit_code="$2"
  local finished_at="$3"
  local temporary="${status_file}.tmp"
  jq -n \
    --arg state "${state}" \
    --arg host "${host}" \
    --argjson pid "$$" \
    --argjson workers "${workers}" \
    --arg git_head "${git_head}" \
    --arg started_at "${started_at}" \
    --arg finished_at "${finished_at}" \
    --argjson exit_code "${exit_code}" \
    '{
      schema_version: 1,
      suite_id: "waffle_campaign_suite_v1",
      state: $state,
      host: $host,
      pid: $pid,
      workers: $workers,
      git_head: $git_head,
      started_at: $started_at,
      finished_at: (if $finished_at == "" then null else $finished_at end),
      exit_code: (if $exit_code < 0 then null else $exit_code end)
    }' > "${temporary}"
  mv "${temporary}" "${status_file}"
}

write_status "running" -1 ""
set +e
WFLOP_WORKERS="${workers}" bash scripts/run_all_waffle_formal_campaigns.sh
exit_code="$?"
set -e
if [[ "${exit_code}" -eq 0 ]]; then
  final_state="completed"
else
  final_state="failed"
fi
write_status "${final_state}" "${exit_code}" "$(date --iso-8601=seconds)"
exit "${exit_code}"
