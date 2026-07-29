#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if [[ -x "${repo_root}/.venv-formal/bin/python3" ]]; then
  export PATH="${repo_root}/.venv-formal/bin:${PATH}"
fi
expected_host="${FORMAL_EXPECTED_HOST:?FORMAL_EXPECTED_HOST is required}"
suite_id="${FORMAL_SUITE_ID:?FORMAL_SUITE_ID is required}"
runner="${FORMAL_RUNNER:?FORMAL_RUNNER is required}"
host="$(hostname -s)"
if [[ "${host,,}" != "${expected_host,,}" ]]; then
  echo "Formal suite launcher requires ${expected_host}; got ${host}." >&2
  exit 2
fi
workers="$(nproc)"
control_dir="${repo_root}/results/${suite_id}/control"
status_file="${control_dir}/status.json"
lock_file="${control_dir}/suite.lock"
mkdir -p "${control_dir}"

exec 9>"${lock_file}"
if ! flock -n 9; then
  echo "A formal suite process already holds ${lock_file}." >&2
  exit 3
fi

started_at="$(date --iso-8601=seconds)"
git_head="$(git rev-parse HEAD)"
write_status() {
  local state="$1"
  local exit_code="$2"
  local finished_at="$3"
  local temporary="${status_file}.tmp"
  python3 - "${temporary}" "${suite_id}" "${state}" "${host}" "$$" "${workers}" \
    "${git_head}" "${started_at}" "${finished_at}" "${exit_code}" <<'PY'
import json
import sys
from pathlib import Path

(
    output,
    suite_id,
    state,
    host,
    pid,
    workers,
    git_head,
    started_at,
    finished_at,
    exit_code,
) = sys.argv[1:]
payload = {
    "schema_version": 1,
    "suite_id": suite_id,
    "state": state,
    "host": host,
    "pid": int(pid),
    "workers": int(workers),
    "git_head": git_head,
    "started_at": started_at,
    "finished_at": finished_at or None,
    "exit_code": None if int(exit_code) < 0 else int(exit_code),
}
Path(output).write_text(json.dumps(payload, indent=2) + "\n")
PY
  mv "${temporary}" "${status_file}"
}

write_status "running" -1 ""
set +e
WFLOP_WORKERS="${workers}" bash "${runner}"
exit_code="$?"
set -e
if [[ "${exit_code}" -eq 0 ]]; then
  final_state="completed"
else
  final_state="failed"
fi
write_status "${final_state}" "${exit_code}" "$(date --iso-8601=seconds)"
exit "${exit_code}"
