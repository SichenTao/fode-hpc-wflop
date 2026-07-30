#!/usr/bin/env python3
"""Audit the source-top fact declaration for every target paper package."""

from __future__ import annotations

import csv
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "docs" / "target_source_fact_declaration_registry.tsv"
COMPLETION = ROOT / "docs" / "paper_package_completion.tsv"
DECLARATION_LIMIT = 8000
REQUIRED_TARGET_COUNT = 23

FIELD_GROUPS = {
    "public source/data": (
        "public asset",
        "public author",
        "public code",
        "public source",
        "public implementation",
        "publicly",
    ),
    "missing information": ("missing",),
    "paper/source conflict": ("conflict",),
    "resolution": ("reconstruction", "resolution", "completion"),
    "semantic identity": ("semantic id", "semantic ids"),
    "production backend": (
        "production backend",
        "backend",
        "parallel completion",
        "pure c++",
        "c++ cpu",
    ),
    "controlling contract": ("contract",),
    "claim boundary": ("claim boundary",),
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def main() -> int:
    registry = read_tsv(REGISTRY)
    completion = read_tsv(COMPLETION)
    failures: list[str] = []

    if len(registry) != REQUIRED_TARGET_COUNT:
        failures.append(
            f"registry has {len(registry)} rows; expected {REQUIRED_TARGET_COUNT}"
        )

    completion_identity = {
        (row["corpus_id"], row["doi"].lower(), row["target_algorithm"])
        for row in completion
    }
    registry_identity = {
        (row["corpus_id"], row["doi"].lower(), row["target_algorithm"])
        for row in registry
    }
    if registry_identity != completion_identity:
        missing = sorted(completion_identity - registry_identity)
        extra = sorted(registry_identity - completion_identity)
        failures.append(
            f"registry/completion identity mismatch missing={missing} extra={extra}"
        )

    seen_corpus_ids: set[str] = set()
    for row in registry:
        corpus_id = row["corpus_id"]
        if corpus_id in seen_corpus_ids:
            failures.append(f"duplicate corpus_id {corpus_id}")
        seen_corpus_ids.add(corpus_id)

        fact_files = [
            item.strip() for item in row["fact_files"].split(";") if item.strip()
        ]
        if not fact_files:
            failures.append(f"{corpus_id} has no fact_files")
            continue

        for relative in fact_files:
            path = ROOT / relative
            if not path.is_file():
                failures.append(f"{corpus_id} missing fact file {relative}")
                continue

            prefix = path.read_text(encoding="utf-8")[:DECLARATION_LIMIT]
            lowered = prefix.lower()
            label = f"{corpus_id}:{relative}"

            begin = prefix.find("WFLOP IMPLEMENTATION FACT DECLARATION")
            end = prefix.find("END WFLOP IMPLEMENTATION FACT DECLARATION")
            code_positions = [
                value
                for value in (prefix.find("#include"), prefix.find("#pragma"))
                if value >= 0
            ]
            first_code = min(code_positions) if code_positions else len(prefix)
            if begin < 0 or end < 0 or end < begin:
                failures.append(f"{label} has no complete declaration")
            elif begin > first_code or end > first_code:
                failures.append(f"{label} declaration is not before code")

            if row["doi"].lower() not in lowered:
                failures.append(f"{label} does not state DOI {row['doi']}")

            for field, alternatives in FIELD_GROUPS.items():
                if not any(marker in lowered for marker in alternatives):
                    failures.append(
                        f"{label} does not state {field}; "
                        f"accepted markers={alternatives}"
                    )

    if failures:
        raise RuntimeError("\n".join(failures))

    source_links = sum(
        len(row["fact_files"].split(";")) for row in registry
    )
    print(
        "target_source_fact_declaration_audit_pass "
        f"papers={len(registry)} source_links={source_links}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
