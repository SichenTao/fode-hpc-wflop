# T63 FODE-HPC-level analysis

## H0-H2: paper semantics and mathematical work

T63 selects 20 binary cells from a 20 by 20 grid. Its objective contains a
base kinetic-energy term for each selected cell and directed wake-interaction
terms for every selected source-target pair over twelve wind states. The
published expression is quadratic in binary variables even though the text
calls it MIP. This implementation registers that fact and introduces one
binary product variable only for each nonzero unordered pair, with all three
exact linearization inequalities.

The target contribution is the six-step CFD-to-MIP information loop. The
author terrain and CFD arrays are not public. Figure 4 and Figure 5 are
digitized, while missing wake rows use separately named approximate and
terrain-aware surrogate fields. Surrogate values are never reported as CFD.

## H3-H4: high-performance implementation

The \(400\times400\times12\) approximate and terrain-aware wake work is
generated once. A persistent full-core team owns 400 independent source rows,
and both resulting matrices are contiguous source-major arrays. Every
iteration selects a row by a Boolean CFD-known mask rather than recomputing
physics.

HiGHS 1.15.1 is pinned by immutable revision and linked directly as C++.
Each sequential MIP receives its case's worker partition, a feasible warm
start, the paper's 30-second limit, zero requested MIP gap, and deterministic
random seed. The receipt distinguishes proven optimal, time-limited incumbent,
dual bound, and gap. Iterations inside one relaxation case are not concurrent
because each consumes the previous layout and wake-row state. The five paper
relaxation cases are independent, so the Waffle allocation is partitioned
across those cases to expose the available outer parallelism without solver
oversubscription.

## H5-H6 admission

H5 independently checks the binary asset hash, twenty-by-twenty dimensions,
wind-probability normalization, dominant-west sector, printed power-law
background speed, 20-cell cardinality, five-diameter spacing, 12 CFD
simulations per newly selected location, and feasible time-limited incumbent.

H6 executes all five paper relaxation values on Waffle, using all available
cores and a 30-second limit for every MIP. It runs until no new cell is selected.
The finite ceiling is 401 solves: at most 400 locations can become newly known,
and one final solve then proves that no new location remains. Admission requires
a feasible 20-cell incumbent at every iteration, exact lifecycle accounting,
finite final surrogate objective, no-wake upper-bound ordering, and explicit
solver status/bounds. Printed Table-1 values are context anchors only because
the private CFD fields are absent.
