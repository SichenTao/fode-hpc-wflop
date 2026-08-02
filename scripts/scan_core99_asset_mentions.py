#!/usr/bin/env python3
"""Index public-code/data mentions without copying private full-text content."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

from pypdf import PdfReader

from core99_common import ROOT, read_tsv, sha256_file, write_json


PUBLIC_ASSET_HOST_HINTS = (
    "github.com",
    "gitlab.",
    "bitbucket.",
    "zenodo.org",
    "figshare.",
    "osf.io",
    "codeocean.",
    "dataverse.",
    "data.mendeley.",
    "kaggle.",
    "sourceforge.",
    "doi.org/10.5281/zenodo.",
)
URL_PATTERN = re.compile(r"https?://[^\s<>{}\"']+")
AVAILABILITY_PATTERN = re.compile(
    r"\b(code|data|software|model|repository|implementation)"
    r"\b.{0,45}\b(available|availability|repository|github|gitlab|zenodo)\b",
    re.IGNORECASE,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registry",
        type=Path,
        default=ROOT / "docs/core99_expansion_registry.tsv",
    )
    parser.add_argument(
        "--text-cache",
        type=Path,
        default=ROOT / ".source-cache/core99-pdf-text",
    )
    parser.add_argument("--pdf-root", type=Path, required=True)
    parser.add_argument(
        "--output-root",
        type=Path,
        default=ROOT / "evidence/core99/source-mentions",
    )
    return parser.parse_args()


def normalize_url(raw: str) -> str:
    candidate = raw.rstrip(".,;:!?)")
    candidate = candidate.replace("\u00ad", "")
    parts = urlsplit(candidate)
    return urlunsplit(
        (
            parts.scheme.lower(),
            parts.netloc.lower(),
            parts.path.rstrip("/"),
            parts.query,
            "",
        )
    )


def annotation_urls(pdf_path: Path) -> set[str]:
    urls: set[str] = set()
    reader = PdfReader(pdf_path)
    for page in reader.pages:
        for annotation_ref in page.get("/Annots", []):
            annotation = annotation_ref.get_object()
            action = annotation.get("/A")
            if action is None:
                continue
            uri = action.get("/URI")
            if isinstance(uri, str) and uri.startswith(("http://", "https://")):
                urls.add(normalize_url(uri))
    return urls


def main() -> int:
    args = parse_args()
    private_index = json.loads(
        (args.text_cache / "index.json").read_text(encoding="utf-8")
    )
    text_receipts = {
        row["corpus_id"]: row for row in private_index["records"]
    }
    registry_rows = [
        row
        for row in read_tsv(args.registry)
        if row["scope_status"]
        in {"new_direct_ready", "review_evidence_package"}
    ]
    summary_rows: list[dict[str, object]] = []
    for row in registry_rows:
        corpus_id = row["corpus_id"]
        text_path = args.text_cache / f"{corpus_id}.txt"
        text = text_path.read_text(encoding="utf-8", errors="replace")
        pdf_path = args.pdf_root / row["pdf_basename"]
        if sha256_file(pdf_path) != row["pdf_sha256"]:
            raise RuntimeError(f"{corpus_id}: PDF hash drift during URI scan")
        text_urls = sorted(
            {
                normalized
                for raw in URL_PATTERN.findall(
                    text.replace("\u200b", "").replace("\u00ad", "")
                )
                if any(
                    hint in (normalized := normalize_url(raw)).lower()
                    for hint in PUBLIC_ASSET_HOST_HINTS
                )
            }
        )
        embedded_urls = sorted(
            url
            for url in annotation_urls(pdf_path)
            if any(hint in url.lower() for hint in PUBLIC_ASSET_HOST_HINTS)
        )
        # Embedded PDF annotations preserve wrapped links exactly.  When they
        # exist, do not mix in truncated single-line text candidates.
        public_urls = embedded_urls or text_urls
        availability_lines = [
            number
            for number, line in enumerate(text.splitlines(), start=1)
            if AVAILABILITY_PATTERN.search(line)
        ]
        payload = {
            "schema_version": 1,
            "corpus_id": corpus_id,
            "doi": row["doi"],
            "paper_pdf_sha256": row["pdf_sha256"],
            "private_text_sha256": text_receipts[corpus_id]["text_sha256"],
            "embedded_annotation_asset_urls": embedded_urls,
            "single_line_text_asset_url_candidates": text_urls,
            "candidate_public_asset_urls": public_urls,
            "availability_mention_line_numbers": availability_lines,
            "interpretation": (
                "candidate mentions only; each URL requires identity, revision, "
                "license, and paper/source semantic audit before use"
            ),
        }
        write_json(args.output_root / f"{corpus_id}.json", payload)
        summary_rows.append(
            {
                "corpus_id": corpus_id,
                "url_count": len(public_urls),
                "availability_line_count": len(availability_lines),
            }
        )
    write_json(
        args.output_root / "index.json",
        {
            "schema_version": 1,
            "record_count": len(summary_rows),
            "records": summary_rows,
        },
    )
    with_urls = sum(int(row["url_count"] > 0) for row in summary_rows)
    print(
        "core99_asset_mention_scan_pass "
        f"papers={len(summary_rows)} with_public_url_candidates={with_urls} "
        f"index_sha256={sha256_file(args.output_root / 'index.json')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
