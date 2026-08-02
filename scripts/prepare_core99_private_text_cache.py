#!/usr/bin/env python3
"""Extract verified private Core-99 PDFs into the ignored local source cache."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

from core99_common import ROOT, read_tsv, sha256_file


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registry",
        type=Path,
        default=ROOT / "docs/core99_expansion_registry.tsv",
    )
    parser.add_argument("--pdf-root", type=Path, required=True)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / ".source-cache/core99-pdf-text",
    )
    parser.add_argument(
        "--scope-status",
        action="append",
        default=[],
        help="Repeat to select states; default selects new-ready and reviews.",
    )
    return parser.parse_args()


def page_count(pdf: Path) -> int:
    completed = subprocess.run(
        ["pdfinfo", str(pdf)],
        check=True,
        capture_output=True,
        text=True,
    )
    match = re.search(r"^Pages:\s+(\d+)\s*$", completed.stdout, re.MULTILINE)
    if not match:
        raise RuntimeError(f"{pdf.name}: pdfinfo did not report page count")
    return int(match.group(1))


def main() -> int:
    args = parse_args()
    if shutil.which("pdftotext") is None or shutil.which("pdfinfo") is None:
        raise RuntimeError("Poppler pdftotext and pdfinfo are required")
    selected_states = set(
        args.scope_status
        or ("new_direct_ready", "review_evidence_package")
    )
    rows = [
        row
        for row in read_tsv(args.registry)
        if row["scope_status"] in selected_states
    ]
    args.output_root.mkdir(parents=True, exist_ok=True)
    receipts: list[dict[str, object]] = []
    for index, row in enumerate(rows, start=1):
        corpus_id = row["corpus_id"]
        pdf = args.pdf_root / row["pdf_basename"]
        actual_hash = sha256_file(pdf)
        if actual_hash != row["pdf_sha256"]:
            raise RuntimeError(
                f"{corpus_id}: PDF hash drift expected={row['pdf_sha256']} "
                f"found={actual_hash}"
            )
        text_path = args.output_root / f"{corpus_id}.txt"
        subprocess.run(
            ["pdftotext", "-layout", "-enc", "UTF-8", str(pdf), str(text_path)],
            check=True,
        )
        text_hash = sha256_file(text_path)
        receipts.append(
            {
                "corpus_id": corpus_id,
                "pdf_basename": row["pdf_basename"],
                "pdf_sha256": actual_hash,
                "page_count": page_count(pdf),
                "text_basename": text_path.name,
                "text_sha256": text_hash,
                "text_bytes": text_path.stat().st_size,
            }
        )
        print(
            f"core99_text_extract {index}/{len(rows)} "
            f"id={corpus_id} pages={receipts[-1]['page_count']}"
        )
    index_path = args.output_root / "index.json"
    index_path.write_text(
        json.dumps(
            {
                "schema_version": 1,
                "cache_identity": "private_core99_pdftotext_layout_v1",
                "record_count": len(receipts),
                "records": receipts,
            },
            indent=2,
            sort_keys=True,
            ensure_ascii=False,
        )
        + "\n",
        encoding="utf-8",
    )
    print(
        f"core99_text_cache_ready records={len(receipts)} "
        f"index_sha256={sha256_file(index_path)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
