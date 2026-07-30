#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

mapfile -d '' public_files < <(
  git ls-files --cached --others --exclude-standard -z
)

public_regular_files=()
export_ignored_files=()
for path in "${public_files[@]}"; do
  if [[ ! -f "${path}" ]]; then
    continue
  fi
  export_ignore="$(
    git check-attr export-ignore -- "${path}" | awk -F': ' '{print $3}'
  )"
  if [[ "${export_ignore}" == "set" ]]; then
    export_ignored_files+=("${path}")
    continue
  fi
  public_regular_files+=("${path}")
  case "${path}" in
    *.pdf|*.mex|*.mex*|*.mat|*.npz)
      echo "Public audit failed: prohibited asset ${path} is public." >&2
      exit 1
      ;;
  esac
done

if ((${#public_regular_files[@]} > 0)) && grep -IInE \
    --exclude='public_audit.sh' \
    '(/home/|/Users/|BEGIN [A-Z ]*PRIVATE KEY|ghp_|github_pat_|@tohoku)' \
    "${public_regular_files[@]}"; then
  echo "Public audit failed: a private path or credential pattern is present." >&2
  exit 1
fi

echo \
  "Public audit passed. exportable_files=${#public_regular_files[@]} local_forensic_export_ignored=${#export_ignored_files[@]}"
