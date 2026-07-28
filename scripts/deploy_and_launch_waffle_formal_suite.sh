#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

target="${WAFFLE_SSH_TARGET:-waffle}"
remote_dir="${WAFFLE_REMOTE_DIR:-fode-hpc-wflop-formal}"
expected_host="waffle"
bde_source="${repo_root}/.source-cache/official/BDE-WindFarm_code/code"
gga_assets="${repo_root}/.source-cache/generated/gga_repaired"
head="$(git rev-parse HEAD)"

if [[ -n "$(git status --porcelain)" ]]; then
  echo "Deployment requires a clean local worktree." >&2
  exit 2
fi
if [[ ! -d "${bde_source}" ]]; then
  echo "Missing authorized BDE source assets: ${bde_source}" >&2
  exit 2
fi
if [[ ! -f "${gga_assets}/manifest.json" ]]; then
  echo "Missing generated GGA problem assets: ${gga_assets}" >&2
  exit 2
fi

remote_host="$(ssh -o BatchMode=yes -o ConnectTimeout=8 "${target}" hostname -s)"
if [[ "${remote_host,,}" != "${expected_host}" ]]; then
  echo "Formal deployment is restricted to hostname ${expected_host}; got ${remote_host}." >&2
  exit 2
fi

temporary="$(mktemp -d)"
cleanup() {
  if [[ -n "${bundle_path:-}" && -f "${bundle_path}" ]]; then
    rm -f -- "${bundle_path}"
  fi
  if [[ -n "${temporary:-}" && -d "${temporary}" ]]; then
    rmdir -- "${temporary}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

bundle_name="fode-hpc-wflop-${head}.bundle"
bundle_path="${temporary}/${bundle_name}"
git bundle create "${bundle_path}" HEAD
scp -q "${bundle_path}" "${target}:${bundle_name}"

ssh "${target}" bash -s -- "${remote_dir}" "${bundle_name}" "${head}" <<'REMOTE_PREPARE'
set -euo pipefail
remote_dir="$1"
bundle_name="$2"
expected_head="$3"
bundle_path="${HOME}/${bundle_name}"
repo_path="${HOME}/${remote_dir}"

if [[ ! -e "${repo_path}" ]]; then
  git clone -q "${bundle_path}" "${repo_path}"
elif [[ ! -d "${repo_path}/.git" ]]; then
  echo "Remote target exists but is not a Git worktree: ${repo_path}" >&2
  exit 2
else
  if [[ -n "$(git -C "${repo_path}" status --porcelain)" ]]; then
    echo "Remote formal worktree is not clean: ${repo_path}" >&2
    exit 2
  fi
  git -C "${repo_path}" fetch -q "${bundle_path}" HEAD
  git -C "${repo_path}" merge --ff-only -q FETCH_HEAD
fi

actual_head="$(git -C "${repo_path}" rev-parse HEAD)"
if [[ "${actual_head}" != "${expected_head}" ]]; then
  echo "Remote Git identity differs: expected ${expected_head}, got ${actual_head}." >&2
  exit 2
fi
rm -f -- "${bundle_path}"
mkdir -p \
  "${repo_path}/.source-cache/official/BDE-WindFarm_code/code" \
  "${repo_path}/.source-cache/generated/gga_repaired" \
  "${repo_path}/logs"
REMOTE_PREPARE

rsync -a \
  "${bde_source}/" \
  "${target}:${remote_dir}/.source-cache/official/BDE-WindFarm_code/code/"
rsync -a \
  "${gga_assets}/" \
  "${target}:${remote_dir}/.source-cache/generated/gga_repaired/"

ssh "${target}" bash -s -- "${remote_dir}" "${head}" <<'REMOTE_LAUNCH'
set -euo pipefail
remote_dir="$1"
expected_head="$2"
repo_path="${HOME}/${remote_dir}"
cd "${repo_path}"

for command_name in cmake c++ python3 flock nproc; do
  command -v "${command_name}" >/dev/null
done
if [[ "$(hostname -s | tr '[:upper:]' '[:lower:]')" != "waffle" ]]; then
  echo "Formal execution host identity changed." >&2
  exit 2
fi
if [[ "$(git rev-parse HEAD)" != "${expected_head}" ]]; then
  echo "Formal execution Git identity changed." >&2
  exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "Tracked remote worktree changed before validation." >&2
  exit 2
fi

workers="$(nproc)"
WFLOP_VALIDATE_ONLY=1 \
WFLOP_WORKERS="${workers}" \
  bash scripts/run_all_waffle_formal_campaigns.sh

status_file="results/waffle_campaign_suite_v1/control/status.json"
if [[ -f "${status_file}" ]]; then
  readarray -t status_fields < <(
    python3 - "${status_file}" <<'PY'
import json
import sys

payload = json.load(open(sys.argv[1]))
print(payload.get("state", "unknown"))
print(payload.get("pid") or "")
print(payload.get("git_head", ""))
PY
  )
  state="${status_fields[0]}"
  pid="${status_fields[1]}"
  status_head="${status_fields[2]}"
  if [[ "${state}" == "completed" && "${status_head}" == "${expected_head}" ]]; then
    echo "Formal suite is already complete at Git ${expected_head}."
    cat "${status_file}"
    exit 0
  fi
  if [[ "${state}" == "running" && -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
    echo "Formal suite is already running as PID ${pid}."
    cat "${status_file}"
    exit 0
  fi
fi

nohup bash scripts/launch_waffle_formal_suite.sh \
  > logs/waffle_campaign_suite_v1.log 2>&1 < /dev/null &
launcher_pid="$!"
sleep 2
if ! kill -0 "${launcher_pid}" 2>/dev/null; then
  echo "Formal suite launcher exited during startup." >&2
  tail -n 80 logs/waffle_campaign_suite_v1.log >&2
  exit 2
fi
cat "${status_file}"
echo "log=${repo_path}/logs/waffle_campaign_suite_v1.log"
REMOTE_LAUNCH
