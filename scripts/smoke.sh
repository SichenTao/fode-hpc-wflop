#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-${repo_root}/build}"
binary="${build_dir}/hpc/wflop_cpp/wflop_cpp_hpc"

"${binary}" \
  --self-check \
  --workers 2 \
  --cases "${repo_root}/shared/contracts/benchmark_cases.json" \
  --models "${repo_root}/shared/models/sugga_cpp"
