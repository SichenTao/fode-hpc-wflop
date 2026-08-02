#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: fail-closed sequential Core-99 Waffle campaign.
# Paper scope: 68 direct Gao/Tao WFLOP papers admitted by Plan 002; T32 remains
# the declared missing-primary-PDF skip and seven review-only papers do not
# create independent algorithm/problem formal tasks.
# Public/project assets: the per-paper wrappers and declared immutable source
# snapshot supplied to this script.
# Missing/conflicts and reconstruction: per-paper source facts remain in each
# wrapper and implementation declaration.  This scheduler changes only task
# order.  Its order is a stable topological sort of the 69 wrapper dependency
# edges audited on 2026-08-02.
# Semantic IDs and contracts: inherited unchanged from each paper wrapper.
# HPC design: one paper task owns the Waffle node at a time; the task itself
# uses its admitted all-core implementation.  A failure retains control and
# prevents every downstream task from starting.
# Claim boundary: scheduling/provenance only; no algorithm, benchmark, budget,
# objective, training lifecycle, or paper protocol is changed.
# Last evidence-audit date: 2026-08-02
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

order=(
    t12 t22 t62 t17 t63 t16 l0124 t87 l0371 l0499 l0590 l0341 l0623
    t74 t80 t05 t07 t77 t31 t27 t28 t30 t21 t72 t60 t11 l0259 t82
    t85 t24 t33 t64 t67 t76 t81 t68 t78 t83 t69 l0079 y09 y14 y16
    l0368 l0373 l0298 t73 t18 t25 t19 t08 t10 t84 t58 t26 y13 l0649
    l0805 l0581 l0245
)

if [[ "${1:-}" == "--print-order" ]]; then
    printf '%s\n' "${order[@]}"
    exit 0
fi

project_root=${1:?immutable project root required}
source_commit=${2:?source commit required}
results_root=${3:?Core-99 results root required}

observed_commit=$(git -C "${project_root}" rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Core-99 queue source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi

mkdir -p "${results_root}/sequential-queue/${source_commit:0:7}"
queue_root="${results_root}/sequential-queue/${source_commit:0:7}"
queue_state="${queue_root}/queue.state"

write_state() {
    local state=${1:?state required}
    local unit=${2:?unit required}
    local return_code=${3:?return code required}
    local temporary="${queue_state}.tmp.$$"
    {
        printf 'state=%s\n' "${state}"
        printf 'unit=%s\n' "${unit}"
        printf 'return_code=%s\n' "${return_code}"
        printf 'source_commit=%s\n' "${source_commit}"
        printf 'timestamp_utc=%s\n' "$(date -u +%FT%TZ)"
    } > "${temporary}"
    mv "${temporary}" "${queue_state}"
}

for unit in "${order[@]}"; do
    wrapper="${project_root}/scripts/run_core99_${unit}_when_waffle_free.sh"
    if [[ ! -f "${wrapper}" ]]; then
        write_state failed "${unit}" 2
        echo "Core-99 queue wrapper missing: ${wrapper}" >&2
        while true; do sleep 300; done
    fi
    bash -n "${wrapper}"
done

for unit in "${order[@]}"; do
    corpus=${unit^^}
    output_root="${results_root}/${corpus}/${source_commit:0:7}"
    write_state running "${unit}" -1
    set +e
    bash "${project_root}/scripts/run_core99_waffle_task.sh" \
        "${unit}" "${project_root}" "${source_commit}" "${output_root}"
    return_code=$?
    set -e
    if [[ "${return_code}" -ne 0 ]]; then
        write_state failed "${unit}" "${return_code}"
        echo "Core-99 queue stopped at ${unit}: ${return_code}" >&2
        while true; do sleep 300; done
    fi
done

write_state pass complete 0
