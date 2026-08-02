#!/usr/bin/env python3
"""Audit BR-04 fact declarations for every C++ scientific unit."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_MARKERS = (
    "WFLOP IMPLEMENTATION FACT DECLARATION",
    "Implementation unit:",
    "END WFLOP IMPLEMENTATION FACT DECLARATION",
)
SEMANTIC_GROUPS = (
    ("Paper title", "Paper titles", "Paper:", "Papers:", "Paper DOI", "DOI:",
     "Paper/source", "multipaper",
     "not_applicable_shared_infrastructure"),
    ("Public asset", "Public author", "Public code", "Related public asset",
     "Public source", "project-native implementation", "source provenance"),
    ("Missing", "missing", "Claim boundary"),
    ("Reconstruction", "reconstruction", "completion", "reused unchanged",
     "not applicable"),
    ("semantic ID", "semantic IDs", "Semantic ID", "Semantic IDs"),
    ("contract", "Contract", "paper ledger", "source dossier"),
    ("Claim boundary", "claim boundary"),
)


def main() -> int:
    units = sorted(
        path
        for suffix in ("*.cpp", "*.hpp")
        for path in (ROOT / "hpc").glob(f"**/{suffix}")
        if "/build/" not in path.as_posix()
    )
    failures: list[str] = []
    for path in units:
        text = path.read_text(encoding="utf-8")
        prefix = text[:6000]
        missing = [marker for marker in REQUIRED_MARKERS if marker not in prefix]
        for group in SEMANTIC_GROUPS:
            if not any(marker in prefix for marker in group):
                missing.append("one of " + repr(group))
        if missing:
            failures.append(
                f"{path.relative_to(ROOT)} missing markers {missing}"
            )
        marker = prefix.find("WFLOP IMPLEMENTATION FACT DECLARATION")
        first_include = prefix.find("#include")
        first_pragma = prefix.find("#pragma")
        first_code = min(
            value for value in (first_include, first_pragma) if value >= 0
        )
        if marker < 0 or marker > first_code:
            failures.append(
                f"{path.relative_to(ROOT)} declaration is not before code"
            )
    if failures:
        raise RuntimeError("\n".join(failures))
    print(f"source_fact_declaration_audit_pass units={len(units)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
