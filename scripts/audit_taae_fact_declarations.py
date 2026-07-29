#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: TAAE implementation fact-declaration audit
DOI: 10.1109/JAS.2026.126233
Fixture scope: checks that every TAAE scientific implementation unit begins with the canonical evidence and claim boundary
Problem evidence tier: P4_FORMULA_FIXTURE over P3_DECLARED_PROXY
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1
Controlling contract: shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json
Claim boundary: declaration-structure audit only; no problem-accuracy, method, performance, or paper-result claim
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DOI = "10.1109/JAS.2026.126233"
SEMANTIC_ID = "taae_zhangbei_structured_declared_proxy_v1"
FILES = (
    ROOT / "hpc/wflop_cpp/include/wflop/taae_problem.hpp",
    ROOT / "hpc/wflop_cpp/src/problems/taae_zhangbei_structured_proxy.cpp",
    ROOT / "hpc/wflop_cpp/tests/taae_problem_test.cpp",
    ROOT / "scripts/audit_taae_semantic_contract.py",
)
REQUIRED_FIELDS = (
    "WFLOP IMPLEMENTATION FACT DECLARATION",
    "Implementation unit:",
    f"DOI: {DOI}",
    "Problem evidence tier:",
    f"Problem semantic ID: {SEMANTIC_ID}",
    "Controlling contract",
    "Claim boundary:",
    "END WFLOP IMPLEMENTATION FACT DECLARATION",
)


def main() -> int:
    for path in FILES:
        text = path.read_text(encoding="utf-8")
        if text.count("WFLOP IMPLEMENTATION FACT DECLARATION") != 2:
            raise RuntimeError(
                f"{path.relative_to(ROOT)}: declaration boundary count mismatch"
            )
        declaration_end = text.index(
            "END WFLOP IMPLEMENTATION FACT DECLARATION"
        )
        first_code_token = min(
            position
            for token in ("#pragma once", "#include", "from __future__")
            if (position := text.find(token)) >= 0
        )
        if declaration_end > first_code_token:
            raise RuntimeError(
                f"{path.relative_to(ROOT)}: declaration is not first"
            )
        for field in REQUIRED_FIELDS:
            if field not in text[: declaration_end + len(field)]:
                raise RuntimeError(
                    f"{path.relative_to(ROOT)}: missing {field}"
                )
    print(
        "taae_fact_declaration_audit_pass "
        f"implementation_units={len(FILES)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
