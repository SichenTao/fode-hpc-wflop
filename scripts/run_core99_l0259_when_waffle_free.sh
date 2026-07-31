#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: L0259 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Wind farm layout optimization based on support vector regression
# guided genetic algorithm with consideration of participation among
# landowners; 10.1016/j.enconman.2019.06.082.
# Public source, conflicts, missing facts, reconstruction completion,
# semantic IDs, production backend and claim boundary:
# hpc/core99_cpp/include/core99/ju_l0259.hpp.
# Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
# HPC design: wait for T11, then exclusively use all 20 Waffle cores for
# build, H5, H6 and 11,700 paper-target runs over all 117 native problems.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t11-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "L0259 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-l0259 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_L0259=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-l0259 \
    --target core99_l0259_hpc core99_l0259_test -j 20
ctest --test-dir build-waffle-l0259 \
    -R '^core99_l0259_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_l0259_h6_formal.py \
    --binary build-waffle-l0259/hpc/core99_cpp/core99_l0259_hpc \
    --output-root "${output_root}" \
    --source-commit "${source_commit}" \
    --total-workers 20 \
    --repeat-count 100

python3 - "${source_commit}" "${output_root}" <<'PY'
import json
import platform
import sys
from pathlib import Path

commit, root = sys.argv[1], Path(sys.argv[2])
summary = json.loads((root / "summary.json").read_text())
receipt = {
    "schema_version": 1,
    "corpus_id": "L0259",
    "status": summary["status"],
    "source_commit": commit,
    "host": platform.node(),
    "selected_workers": 20,
    "h6_status": summary["h6"]["status"],
    "formal_run_count": summary["formal_run_count"],
    "total_physical_fes": summary["total_physical_fes"],
    "claim_boundary": summary["claim_boundary"],
}
(root / "campaign_receipt.json").write_text(
    json.dumps(receipt, indent=2, sort_keys=True) + "\n"
)
PY
