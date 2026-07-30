# L0124 FODE-HPC-level analysis

## H0-H2: paper semantics and mathematical work

L0124 minimizes the fixed-turbine-count cost-to-expected-power ratio on six
paper-native grid cases. The decision vector has 60 or 78 integer coordinates;
the two grids contain 100 or 400 candidate cells. A physical evaluation first
checks all turbine pairs, then evaluates 36 wind directions and up to three
wind speeds per direction. For \(N_T\) turbines and \(N_D=36\) directions,
the repeated wake work is \(O(N_D N_T^2)\).

The target paper exposes its case-C joint distribution only as the vector
stacked-bar Fig. 6. The implementation recovers every rectangle height against
the vector y-axis and rounds to the displayed 0.001 precision. The resulting
probabilities sum to 1.002 because the paper publishes rounded bars. They are
not silently renormalized: the reconstructed 36.506188 MW no-wake power closes
to within 0.01% of the 36.502605 MW identity implied by Table 4.

The paper names MI-LXPM but omits its operator parameters. Its cited direct
predecessor supplies tournament size 3, crossover probability 0.8, mutation
probability 0.005, Laplace integer scale 0.35, power-mutation index 4,
stochastic integer conversion, systematic size-three tournaments in which
every individual participates three times, and feasibility-first selection.
Population 600 follows the paper's approximately 3.00e5 evaluated individuals
over 500 generations.

The target paper also states that ranked best individuals survive, whereas the
cited MI-LXPM predecessor specifies complete generational replacement. Target
semantics take precedence: the implementation retains the best 5% as a
declared OPTIMTOOL-style completion. This setting is not presented as an
author-disclosed number.

## H3-H4: high-performance implementation

Gaussian pair-deficit squares are precomputed once for every direction and
ordered grid-cell pair. Each later evaluation becomes contiguous table lookup,
fixed-order square-deficit reduction and cubic power accumulation. The
optimizer uses one persistent C++ worker team. It parallelizes independent
tournament selections, parent-pair crossover, per-individual mutation and all
population evaluations. Population and mating buffers are allocated once and
reused by swapping; reconstructed random events are counter-keyed, so worker
scheduling cannot change the scientific trajectory.

The six cases and five paper repeats provide 30 independent outer tasks. The
formal Waffle mapping runs twenty one-worker tasks concurrently. This consumes
all twenty CPU cores without nested oversubscription, while each single task
still uses the same precomputed, allocation-reduced pure-C++ evaluator and
MI-LXPM implementation. Inner multicore mode remains available for an isolated
run and is admitted by one-worker/four-worker scientific parity.

## H5-H6 admission and formal execution

H5 independently checks the Gaussian centreline equation, all six case
identities, spacing feasibility, physical efficiency bounds, Fig. 6/Table 4
closure, exact physical FES, fixed-seed replay, and one-worker/four-worker
scientific parity.

H6 uses all twenty Waffle cores and runs five independent paper repeats for
each of the six cases, each with population 600 and 500 generations
(300,600 physical evaluations). Every raw result records the source commit,
semantic IDs, worker receipt, evaluator/algorithm/end-to-end time, physical
FES, feasibility, best layout, history and scientific hash. This is an
academic declared reconstruction, not author-source or exact-layout replay.
