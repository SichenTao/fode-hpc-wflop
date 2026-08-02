#!/usr/bin/env bash
# WFLOP IMPLEMENTATION FACT DECLARATION
# Implementation unit: fail-closed Core-99 Waffle paper-task supervisor.
# Purpose: execute one immutable paper wrapper, record its terminal state and
# retain the tmux dependency session after failure so downstream papers cannot
# silently start from an incomplete predecessor.
# Scientific boundary: scheduling/provenance only; no benchmark, objective or
# algorithm semantics are changed.
# Last evidence-audit date: 2026-08-02
# END WFLOP IMPLEMENTATION FACT DECLARATION
set -euo pipefail

unit=${1:?paper unit required}
project_root=${2:?project root required}
source_commit=${3:?source commit required}
output_root=${4:?output root required}

if [[ ! "${unit}" =~ ^[a-z][a-z0-9]+$ ]]; then
    echo "invalid Core-99 paper unit: ${unit}" >&2
    exit 2
fi
wrapper="${project_root}/scripts/run_core99_${unit}_when_waffle_free.sh"
if [[ ! -x "${wrapper}" ]]; then
    echo "missing Core-99 paper wrapper: ${wrapper}" >&2
    exit 2
fi
cd "${project_root}"
observed_commit=$(git rev-parse HEAD)
if [[ "${observed_commit}" != "${source_commit}" ]]; then
    echo "Core-99 supervisor source mismatch: expected ${source_commit}, observed ${observed_commit}" >&2
    exit 2
fi
mkdir -p "${output_root}"

write_state() {
    local state=${1:?state required}
    local return_code=${2:?return code required}
    local temporary="${output_root}/queue.state.tmp.$$"
    {
        printf 'unit=%s\n' "${unit}"
        printf 'state=%s\n' "${state}"
        printf 'return_code=%s\n' "${return_code}"
        printf 'source_commit=%s\n' "${source_commit}"
        printf 'timestamp_utc=%s\n' "$(date -u +%FT%TZ)"
    } > "${temporary}"
    mv "${temporary}" "${output_root}/queue.state"
}

interrupted() {
    write_state interrupted 130
    exit 130
}
trap interrupted HUP INT TERM

write_state running -1
set +e
bash "${wrapper}" \
    "${project_root}" "${source_commit}" "${output_root}"
return_code=$?
set -e
printf '%s\n' "${return_code}" > "${output_root}/queue.exit"

if [[ "${return_code}" -eq 0 ]]; then
    write_state pass 0
    exit 0
fi

write_state failed "${return_code}"
echo "Core-99 ${unit} failed with return code ${return_code}; dependency session remains active" >&2
while true; do
    sleep 300
done
