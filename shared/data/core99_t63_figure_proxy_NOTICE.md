# T63 figure-digitized terrain and wind proxy

The paper does not publish the Carleton-sur-Mer terrain array, twelve numeric
wind probabilities, no-turbine CFD fields, or single-turbine CFD fields. The
binary beside this notice is deterministically digitized from Figure 4 and
Figure 5 embedded in the hash-verified primary PDF.

- Paper DOI: `10.1016/j.apenergy.2016.06.085`
- Paper PDF SHA-256:
  `7c4f515af4da82bb235045871b132f05d66f2760a2de0f61faef38b82aed8155`
- Terrain source: PDF page 12, embedded 1124×845 RGB raster.
- Wind source: PDF page 13, embedded 950×860 RGB raster.
- Transformation: sample Figure 4 at the centers of the paper's 20×20 cells
  and map each color to the nearest printed 20 m colorbar band; detect the
  twelve green radial bars in Figure 5 and normalize their pixel radii to sum
  to one.
- Builder: `scripts/prepare_core99_t63_figure_proxy.py`.
- Derived SHA-256:
  `643fbafaca90e0e6c9dd8a271b1abea55a7b05d6bf64b12fc7c19e4fdc8ca51e`.
- Claim boundary: this recovers published-figure information only. It is not
  the authors' CFD data and cannot support author-numerical replay claims.

The T63 implementation preserves the printed MIP equations and iterative
CFD-to-MIP update lifecycle. Because the CFD fields are missing, the package
uses a separately declared terrain-aware single-turbine surrogate to exercise
that lifecycle; it never labels surrogate values as author CFD results.
