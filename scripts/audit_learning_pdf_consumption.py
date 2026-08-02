#!/usr/bin/env python3
"""Verify that Plan-004 contracts consume the exact authoritative PDFs."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECEIPT = ROOT / "evidence/development/plan004_learning_pdf_consumption_20260730.json"
EXPECTED = {
    "T45": {
        "sha256": "770b56c0dccbd7683e3c33652d7062c385eb2f00c126d2e9f2a3b7129fdc5872",
        "pages": 22,
        "anchors": (
            "The input feature matrix can be represented",
            "Algorithm 1: The pseudo-code of ALGA",
            "all experiments in this study use a learning rate",
        ),
        "contract": "shared/contracts/plan004_alga_attention_architecture.json",
        "linked_contracts": (
            "shared/contracts/alga_attention_declared_reconstruction_contract.json",
        ),
    },
    "Y36": {
        "sha256": "243dd96dfa94a3d596f375a6c62e58015c735171958778d816b6afdbf99cd35b",
        "pages": 49,
        "anchors": (
            "100,000 wind farm layouts were randomly",
            "Algorithm 2: EvolutionaryOptimization",
            "Supplementary Material for",
            "TABLE S21",
        ),
        "contract": "shared/contracts/plan004_taae_transformer_architecture.json",
        "linked_contracts": (
            "shared/contracts/taae_transformer_declared_reconstruction_contract.json",
            "shared/contracts/taae_transformer_evolution_declared_reconstruction_contract.json",
        ),
    },
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pdf_pages(path: Path) -> int:
    result = subprocess.run(
        ["pdfinfo", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    for line in result.stdout.splitlines():
        if line.startswith("Pages:"):
            return int(line.split(":", 1)[1].strip())
    raise RuntimeError(f"{path}: pdfinfo did not report page count")


def pdf_text(path: Path) -> str:
    result = subprocess.run(
        ["pdftotext", "-raw", str(path), "-"],
        check=True,
        capture_output=True,
    )
    text = result.stdout.decode("utf-8", errors="replace").replace("\x00", "")
    return re.sub(r"\s+", " ", text)


def audit() -> None:
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    papers = {entry["corpus_id"]: entry for entry in receipt["papers"]}
    require(set(papers) == set(EXPECTED), "PDF receipt corpus set mismatch")
    for corpus, expected in EXPECTED.items():
        entry = papers[corpus]
        path = Path(entry["path"])
        require(path.is_file(), f"{corpus}: authoritative PDF absent")
        require(sha256(path) == expected["sha256"], f"{corpus}: PDF hash mismatch")
        require(entry["sha256"] == expected["sha256"], f"{corpus}: receipt hash mismatch")
        require(pdf_pages(path) == expected["pages"], f"{corpus}: PDF page mismatch")
        require(entry["pages"] == expected["pages"], f"{corpus}: receipt page mismatch")
        require(
            entry["consumed_page_ranges"] == [f"1-{expected['pages']}"],
            f"{corpus}: incomplete page range",
        )
        text = pdf_text(path)
        for anchor in expected["anchors"]:
            require(anchor in text, f"{corpus}: PDF anchor absent: {anchor}")
        contract = json.loads(
            (ROOT / expected["contract"]).read_text(encoding="utf-8")
        )
        paper = contract["evidence_status"]["paper"]
        require(paper["sha256"] == expected["sha256"], f"{corpus}: contract hash")
        require(paper["pages"] == expected["pages"], f"{corpus}: contract pages")
        require(paper["full_text_consumed"] is True, f"{corpus}: consumption flag")
        require(
            paper["plan004_consumption_receipt"]
            == "evidence/development/plan004_learning_pdf_consumption_20260730.json",
            f"{corpus}: receipt link",
        )
        for linked_path in expected["linked_contracts"]:
            linked = json.loads((ROOT / linked_path).read_text(encoding="utf-8"))
            linked_paper = linked["paper"]
            require(
                linked_paper["sha256"] == expected["sha256"],
                f"{corpus}: linked contract hash: {linked_path}",
            )
            require(
                linked_paper["local_authoritative_pdf"] == str(path),
                f"{corpus}: linked contract path: {linked_path}",
            )
            require(
                linked_paper["plan004_full_text_receipt"]
                == "evidence/development/plan004_learning_pdf_consumption_20260730.json",
                f"{corpus}: linked contract receipt: {linked_path}",
            )


def main() -> int:
    audit()
    print(
        "learning_pdf_consumption_audit_pass "
        "papers=2 pages=71 embedded_supplement=consumed exact_hashes=yes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
