#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T60 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
# Search Algorithm; 10.1016/j.renene.2015.01.005
# Public source: pinned same-lineage PyWake revision and its exact consumed
# fields are recorded in shared/contracts/core99_t60_feng_shen_2015.json
# Missing/conflicting fields and Reconstruction:
# hpc/core99_cpp/include/core99/feng_t60.hpp
# Semantic IDs: t60_improved_rs_incremental_v1;
# t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
# Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
# HPC design: wait for the preceding immutable T72 campaign, then exclusively
# use all 20 Waffle cores for build, H5, H6 and the formal 840 target runs
# Claim boundary: declared flexible academic reproduction, not author replay
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t72-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T60 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t60 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T60=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t60 \
    --target core99_t60_hpc core99_t60_test -j 20
ctest --test-dir build-waffle-t60 \
    -R 'core99_t60_(cpp|h5)' --output-on-failure

python3 scripts/run_core99_t60_h6_formal.py \
    --binary build-waffle-t60/hpc/core99_cpp/core99_t60_hpc \
    --contract shared/contracts/core99_t60_feng_shen_2015.json \
    --output-root "${output_root}" \
    --workers 20 \
    --physical-fes 100000 \
    --probe-physical-fes 100000 \
    --observations 5 \
    --mode all

python3 - "${source_commit}" "${output_root}" <<'PY'
import json
import platform
import sys
from pathlib import Path

commit, root = sys.argv[1], Path(sys.argv[2])
formal = json.loads((root / "formal_summary.json").read_text())
h6 = json.loads((root / "h6.json").read_text())
receipt = {
    "schema_version": 1,
    "corpus_id": "T60",
    "status": "pass",
    "source_commit": commit,
    "host": platform.node(),
    "selected_workers": 20,
    "h6_status": h6["status"],
    "formal_status": formal["status"],
    "target_method_runs": formal["target_method_runs"],
    "total_target_method_physical_fes":
        formal["total_target_method_physical_fes"],
    "claim_boundary": formal["claim_boundary"],
}
(root / "campaign_receipt.json").write_text(
    json.dumps(receipt, indent=2, sort_keys=True) + "\n"
)
PY
