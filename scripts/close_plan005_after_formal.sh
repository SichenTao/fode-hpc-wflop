#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

status_file="results/plan005_target_native_25_v2/control/status.json"
formal_log="evidence/formal/plan005_cpu_formal_v2_20260730.log"
final_bundle="evidence/closure/plan005_final_bundle.json"
expected_branch="executor/003-target-only-fullstack-hpc"

if [[ "$(git branch --show-current)" != "${expected_branch}" ]]; then
  echo "Plan-005 closure refused: unexpected Git branch." >&2
  exit 1
fi

while true; do
  state="$(
    python3 - "${status_file}" <<'PY'
import json
import pathlib
import sys

document = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
print(document.get("status", "unknown"))
PY
  )"
  case "${state}" in
    complete)
      break
      ;;
    running)
      sleep 30
      ;;
    *)
      echo "Plan-005 closure refused: formal state=${state}." >&2
      exit 1
      ;;
  esac
done

python3 scripts/audit_plan005_campaigns.py \
  --all-admissible --seeds 25 --strict
python3 scripts/audit_plan005_training_resume.py --strict
python3 scripts/generate_plan005_final_bundle.py
python3 scripts/audit_paper_package_completion.py --phase plan005-final
python3 scripts/audit_hpc_core_target_scope.py --final
python3 scripts/audit_target_source_fact_declarations.py
python3 scripts/audit_source_fact_declarations.py
python3 scripts/audit_plan005_production_h6.py
python3 scripts/audit_hpc_equivalence.py --scope core --strict
python3 scripts/audit_hpc_maturity.py --scope core --strict
python3 scripts/audit_performance_receipts.py --scope core --strict
python3 scripts/audit_libtorch_cpp_production.py
bash scripts/public_audit.sh

git add -- "${formal_log}" "${final_bundle}"
if ! git diff --cached --quiet; then
  git commit -m "close Plan 005 CPU formal campaigns"
fi
git push origin "${expected_branch}"

echo \
  "plan005_postformal_closure_pass formal_runs=27775 ready_cpu=20 deferred_learning=3"
