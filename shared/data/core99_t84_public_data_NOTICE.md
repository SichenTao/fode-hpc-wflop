# T84 public numeric fixture notice

`core99_t84_public_data.bin` contains factual numeric observations extracted
from the paper-linked `byuflowlab/thomas2021-wec` repository at commit
`8ff27d66079591f25619abeedbfc970d70e2b520`. Its SHA-256 is
`ef46821e47e4e13a5bc79a77a25660f7b2b58677496506e4dc03a8c1b92a2d8f`.

The upstream revision has no `LICENSE`, `COPYING`, or `README` file. The
fixture therefore does not redistribute executable upstream source. It
encodes only the Vestas V80 power and thrust observations, four wind-resource
tables, square/circle/14-facet boundary data, and the 200 public starting
coordinate arrays for each case. The independent extractor is
`scripts/prepare_core99_t84_public_data.py`.

One source/paper conflict is resolved at extraction: Section 5.1 states that
Case 1 uses 10 m/s, whereas the public wind file and final archived run use
8 m/s. The fixture follows the paper-first 10 m/s definition, and the
conflicting archived Case-1 result is not used as a numeric oracle. Cases 2
and 3 retain their published/source probability mass of 0.962 without
renormalization. Public starts retain their paper-described one-dimensional
separation; final optimization feasibility uses the two-dimensional 2D rule.

This is an independently encoded academic-reproduction input asset, not an
author software distribution or a claim of exact author-environment replay.

Last evidence audit: 2026-08-01.
