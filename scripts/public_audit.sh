#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if find . \( -path './.git' -o -path './build' -o -path './.source-cache' \) -prune -o \
    \( -name '*.pdf' -o -name '*.mex*' -o -name '*.mat' -o -name '*.npz' \) \
    -print | grep -q .; then
  echo "Public audit failed: prohibited binary/research assets are present." >&2
  exit 1
fi

if grep -RInE \
    --exclude-dir=.git --exclude-dir=build --exclude-dir=.source-cache \
    --exclude='public_audit.sh' \
    '(/home/|/Users/|BEGIN [A-Z ]*PRIVATE KEY|ghp_|github_pat_|@tohoku)' .; then
  echo "Public audit failed: a private path or credential pattern is present." >&2
  exit 1
fi

echo "Public audit passed."
