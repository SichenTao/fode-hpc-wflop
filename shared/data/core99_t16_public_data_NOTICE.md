# T16 same-lineage public numeric fixture

The T16 paper does not link an original 2019 source archive. This 5,756-byte
fixture contains only numeric facts extracted from the later same-author
repository `byuflowlab/thomas2021-wec` at commit
`8ff27d66079591f25619abeedbfc970d70e2b520`: the twelve-direction Nantucket
rose, NREL 5-MW CP/CT arrays, and concentric 38-turbine layout.

The repository has no explicit license, so its executable source is not copied
into the production implementation. The fixture supports an independently
written C++ reconstruction. Its SHA-256 is
`b169391622f3ad7d2e9d6fe2a06b63cb17500372f7e6a68123d280e4808df662`.

Important source conflict: `NREL5MWCPCT_dict.txt` labels columns two and three
as CP and CT, but the repository's own `readandwritedict.py` and source pickle
show that they are actually CT and CP. The preparation script corrects this
reversal. The same-lineage driver also supplies generator efficiency 0.944;
retaining it makes the independently reconstructed baseline agree with the
paper's BP Table-2 AEP and directional powers.

This is declared same-lineage evidence, not an author-2019 source release,
original random-state replay, or redistribution of SOWFA cases.
