# T17 declared background-flow proxy

The original T17 Northwest-China WAsP CFD arrays are not public. The compact
binary beside this notice is derived from the public `ParqueFicticio` WAsP
grids in PyWake commit `5b07481ec9b3633a74844651648f266ba82a8b32`.

- Source: `https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git`
- Source paths: `py_wake/examples/data/ParqueFicticio/*.grd`
- Source license: MIT, Copyright 2018 DTU Wind And Energy Systems
- Transformation: interpolate the 30 m and 200 m speed-up, turning,
  inclination, and sector-frequency grids to the T17 67 m hub height; replace
  WAsP invalid cells by the nearest valid grid value; affinely map the public
  grid to the paper's 6000 m by 4000 m domain; retain 12 sectors.
- Claim boundary: this is an open, same-lineage complex-terrain background-flow
  proxy used because the paper-native private CFD arrays are unavailable. It
  is not the Northwest-China site and cannot support author-numerical claims.

The PyWake MIT license permits use, modification, and redistribution subject
to preservation of its copyright and permission notice. The full license text
is available in the source repository.
