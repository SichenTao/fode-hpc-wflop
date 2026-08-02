# T17 FODE-HPC-level analysis

## H0-H2: paper semantics and mathematical work

T17 evaluates 25 turbines over 12 wind sectors. For every source-target pair,
the new wake model traces a pseudo-three-dimensional streamline up to 40 rotor
diameters, integrates a Gaussian deficit over the target rotor, and combines
multiple wakes by root-sum-square. A complete physical FES therefore has
approximately
\(O(12N^2LQ)\) work, where \(L\) is the number of streamline steps and \(Q\)
is the fixed rotor quadrature size.

The private Northwest-China terrain, WAsP result grids, exact coordinates, and
complete turbine curves are not public. The package separates paper-literal
equations and lifecycle from a declared open flow-field proxy. It cannot be
mistaken for an author-site numerical replay.

## H3-H4: high-performance implementation

The immutable terrain and four sector-dependent flow fields use contiguous
sector-major arrays. Bilinear interpolation is allocation-free. One persistent
thread team owns the entire optimization and evaluates twelve independent wind
sectors in parallel; scalar reductions remain in sector order. The team is not
recreated for each FES.

Within each sector, cheap domain and 2D/3D-distance screens precede Gaussian
rotor integration. Formal independent initial layouts are the outer throughput
axis: the scheduler assigns the remaining Waffle cores across starts while
preventing nested oversubscription. This preserves the paper's sequential
random-search state transition and still uses the available machine.

## H5-H6 admission

H5 independently evaluates Eqs. (3)-(4), the one-percent exclusion, the
Figure-2-digitized geometry, wake/no-wake ordering, exact physical-FES
accounting, feasibility, and fixed-seed replay.

H6 uses every currently available Waffle CPU core and exercises one original
plus ten random feasible starts under fixed-work budgets before the
paper-native 5000-second no-wake plus 20000-second wake protocol is admitted.
It records observed worker participation, evaluator and algorithm time,
objective histories, feasibility, and scientific hashes. Author objective
values are context anchors only because the private fields and exact layouts
are unavailable.
