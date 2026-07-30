#!/usr/bin/env python3
"""Regenerate the mechanical Core-99 dossier identity index."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from core99_common import ROOT, read_tsv


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    rows = [
        row for row in read_tsv(ROOT / "docs/core99_expansion_registry.tsv")
        if row["scope_status"] != "retained_direct"
    ]
    dossier_root = ROOT / "evidence/core99/paper-dossiers"
    records = []
    for row in rows:
        path = dossier_root / f"{row['corpus_id']}.json"
        payload = json.loads(path.read_text(encoding="utf-8"))
        records.append(
            {
                "corpus_id": row["corpus_id"],
                "dossier_sha256": digest(path),
                "dossier_status": payload["dossier_status"],
                "role": row["role"],
            }
        )
    output = {"record_count": len(records), "records": records}
    (dossier_root / "index.json").write_text(
        json.dumps(output, indent=2, sort_keys=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
