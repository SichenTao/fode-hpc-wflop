#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

bde_source="${BDE_SOURCE_ROOT:-${repo_root}/.source-cache/official/BDE-WindFarm_code/code}"
rpso_source="${RPSO_SOURCE_ROOT:-${repo_root}/.source-cache/official/RPSO_Wind_Code/RPSO_Wind}"
gga_assets="${GGA_ASSET_DIR:-${repo_root}/.source-cache/generated/gga_repaired}"
bde_cases="${repo_root}/.source-cache/generated/bde_source_replay/benchmark_cases.json"
rpso_cases="${repo_root}/.source-cache/generated/rpso_source_problem/benchmark_cases.json"
bde_audit="${repo_root}/.source-cache/generated/bde_source_replay/source_asset_audit.json"

python3 - <<'PY'
import numpy
import scipy

print(
    "formal_python_environment_pass "
    f"numpy={numpy.__version__} scipy={scipy.__version__}"
)
PY
python3 scripts/audit_bde_source_problem.py \
  --source "${bde_source}" \
  --receipt "${bde_audit}"
python3 scripts/prepare_bde_source_problem.py \
  --source "${bde_source}" \
  --output "${bde_cases}"
python3 scripts/prepare_rpso_source_problem.py \
  --source "${rpso_source}" \
  --output "${rpso_cases}"
python3 scripts/validate_gga_problem_assets.py --assets "${gga_assets}"
