#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: T11 immutable-snapshot Waffle all-core queue wrapper
# Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
# Layouts; 10.1109/CEC.2016.7743909
# Public source: pinned same-lineage WindFLO revision, exact 2014 competition
# XML files and same-author thesis are recorded in the controlling contract.
# Missing/conflicting fields and Reconstruction completion policy:
# hpc/core99_cpp/include/core99/blockcopy_t11.hpp
# Semantic IDs: t11_blockcopy_four_es_methods_v1;
# t11_kusiak_and_2014_competition_four_cases_v1
# Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
# HPC design: wait for the preceding immutable T60 campaign, then exclusively
# use all 20 Waffle cores for build, H5, H6 and the formal 480 target runs.
# Claim boundary: source-backed flexible academic reproduction, not author
# optimizer source, random-stream or exact-number replay.
# Last evidence-audit date: 2026-07-31
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

project_root=${1:?project root required}
source_commit=${2:?source commit required}
output_root=${3:?output root required}

while tmux has-session -t core99-t60-deferred 2>/dev/null; do
    sleep 30
done

cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "T11 immutable source mismatch expected=${source_commit} observed=${observed_commit}" >&2
    exit 2
fi

mkdir -p "${output_root}"
cmake -S . -B build-waffle-t11 \
    -DBUILD_TESTING=ON \
    -DCORE99_ENABLE_T11=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build-waffle-t11 \
    --target core99_t11_hpc core99_t11_test -j 20
ctest --test-dir build-waffle-t11 \
    -R '^core99_t11_(cpp|h5)$' --output-on-failure

python3 scripts/run_core99_t11_h6_formal.py \
    --binary build-waffle-t11/hpc/core99_cpp/core99_t11_hpc \
    --output-root "${output_root}" \
    --workers 20 \
    --observations 3 \
    --repeats 30 \
    --physical-fes 2000 \
    --mode all

python3 - "${source_commit}" "${output_root}" <<'PY'
import json
import platform
import sys
from pathlib import Path

commit, root = sys.argv[1], Path(sys.argv[2])
formal = json.loads((root / "formal_summary.json").read_text())
h6 = json.loads((root / "h6_performance.json").read_text())
receipt = {
    "schema_version": 1,
    "corpus_id": "T11",
    "status": "pass",
    "source_commit": commit,
    "host": platform.node(),
    "selected_workers": 20,
    "h6_status": h6["status"],
    "formal_status": formal["status"],
    "target_method_runs": formal["formal_runs"],
    "total_target_method_physical_fes": formal["formal_physical_fes"],
    "claim_boundary":
        "source-backed flexible academic reproduction, not author exact-number replay",
}
(root / "campaign_receipt.json").write_text(
    json.dumps(receipt, indent=2, sort_keys=True) + "\n"
)
PY
