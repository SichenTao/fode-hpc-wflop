# T03 H0-H3 HPC analysis

Authority: Kusiak and Song, DOI `10.1016/j.renene.2009.08.019`, plus the
declared completions in `shared/contracts/core99_t03_kusiak_cases.json`.

- H0: one physical FES evaluates one fixed-count continuous layout over all
  24 direction bins and the paper's 0.5 m/s Weibull quadrature. Population
  members are independent; reductions within one member remain fixed-order.
- H1: the dominant work is `population × directions × turbines² × speed
  bins`. Offspring mutation is also independent. SPEA strength, archive, and
  tournament decisions consume complete objective vectors and are committed
  in deterministic order.
- H2: the pure-C++ evaluator keeps per-layout wake sums stack-local. One
  persistent CPU team evaluates layouts and produces offspring. Counter-keyed
  random events prevent the worker schedule from changing the scientific
  trajectory.
- H3: the accepted implementation parallelizes initialization, mutation, and
  the complete population evaluation; avoids nested parallel regions and
  thread creation per generation; and reports evaluator, algorithm, and
  end-to-end wall time separately. The bounded H6 test compares 1 worker with
  all 20 Waffle logical CPUs only; it is not the formal paper timing claim.
