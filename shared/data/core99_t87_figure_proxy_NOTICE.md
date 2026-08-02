# T87 declared Qianjiang figure proxy

The target paper does not publish its measured one-year mast data, CFD arrays,
terrain mesh, cost inputs, source code, random seeds or final coordinate
arrays. It explicitly states: “The authors do not have permission to share
data.” The compact binary beside this notice is therefore an independently
versioned, paper-figure-derived proxy, not author data.

- Target paper: *Wind Farm Layout Optimization in Complex Terrain Based on
  CFD and IGA-PSO*, DOI `10.1016/j.energy.2023.129745`.
- Direct IGA predecessor: DOI `10.1016/j.energy.2022.123970`.
- Direct DGWM predecessor: DOI `10.1016/j.renene.2021.02.078`.
- Target PDF SHA-256:
  `83182bf0fa4e7e604543e9a89a682be76ef3f4264fe1aa3c0b2de78a34a85ec6`.
- Fixture SHA-256:
  `e8fbb28bd24f97aaef4923907e03d91afd4b4e07adb4a370c2d2c9bc2f13ea8a`.
- Fig. 2 raster SHA-256:
  `8f449ccf624e3838480b43d66e44dd194d21dbf72dceff548327daf5141f4564`.
- Fig. 5 raster SHA-256:
  `3a09bd6436e6885aaf607a0250ebaed2c82286bccabb1b3182fc1b0eb2c13576`.
- Fig. 9 raster SHA-256:
  `f0fb82fc53364ae5dc5a3464503297daacead45e8079c2e44445aeea964855aa`.
- Extraction: Fig. 9 yields 522 red candidate centers on a 41 by 185,
  0.5D lattice. `522/7585=6.882%`, which rounds to the paper's 6.9%.
- AEH reconstruction: rank each candidate by visible neighboring contour
  colors and anchor the ranking to the paper's feasible interval of
  2000–2500 h. This preserves the displayed spatial ordering but not the
  unavailable CFD values hidden by the red markers.
- Wind reconstruction: digitize the four visible speed stacks in all 16
  directions of Fig. 5 and normalize their rounded radial mass to one,
  producing 49 nonzero direction-speed states.
- Turbine reconstruction: digitize the normalized power and thrust markers in
  Fig. 2 at the displayed 0.5 m/s resolution. The displayed low-speed
  `Ct>1` values are preserved in the fixture; the evaluator explicitly clamps
  only the wake-equation input to 0.999 because the paper uses
  `sqrt(1-Ct)`.
- Binary format: magic `T87PXY2\0`; little-endian counts for candidates,
  wind states and turbine-curve points; then float32 triples
  `(x/D,y/D,AEH)`, `(direction,speed,probability)` and
  `(speed,normalized power,Ct)`.

Claim boundary: this asset supports an academic declared reproduction of the
paper's problem structure, equations and IGA-PSO lifecycle. It is not the
private Qianjiang CFD field, terrain, measured time series, author source or
an exact numerical replay.
