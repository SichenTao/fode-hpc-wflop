# T05 mathematical-programming HPC analysis

T05 is Turner et al., *Renewable Energy* 63 (2014), DOI
`10.1016/j.renene.2013.10.023`. The paper supplies the QIP and equivalent
MILP equations but not the proprietary CPLEX 12.1 models or numerical Case-C
wind array. This package is therefore an equation-level open-solver
reconstruction, not a CPLEX or author-code replay.

## Mathematical work and critical path

For \(N=100\) candidate cells and \(D\) wind directions, the immutable
interaction matrix costs \(O(DN^2)\). A fixed-cardinality layout objective is
the sum of symmetric pair weights. QIP and MILP objectives are identical when
the linearization variables satisfy \(x_{ij}=y_i y_j\). The in-house solver
constructs deterministic multistart incumbents, then expands a best-bound
fixed-cardinality search frontier. Its admissible node bound contains the
current pair cost plus the smallest remaining interactions with selected
cells; nonnegative interactions among future cells are omitted.

The adaptive global frontier is a synchronization boundary. Within one
frontier batch, node expansions are independent. Multistart searches,
interaction rows, paper-specified one-cell local searches and final
wind-scenario power evaluations are also independent. A worker-count-
independent batch fixes the explored node set, result and scientific hash.

## Production topology

One persistent pure-C++ CPU team is created for the complete run and reused
for matrix assembly, 4096 incumbent searches, deterministic frontier batches,
and scenario power evaluation. Formal cases run sequentially, so Waffle never
receives nested 20-by-case oversubscription. The declared node cap is 200000;
an unfinished frontier reports its admissible lower bound and gap and never
claims exact optimality.

H5 independently reconstructs the QIP and power equations in Python for every
returned layout, checks QIP/MILP equivalence, all six paper cases, cardinality,
one/multicore replay and published-power scale. H6 compares identical
Case-C/K=39 work using one and all twenty Waffle cores.
