# T04 H0-H3 HPC analysis

Authority: Chowdhury et al., DOI `10.1016/j.renene.2011.09.017`, plus
the declared completions in `shared/contracts/core99_t04_uwflo_cases.json`.

- H0: one physical FES evaluates every turbine's partial overlap and combined
  wake deficit, power, farm efficiency, and all geometric/cost violations.
- H1: the dominant work is `swarm × turbines²`; particle transitions and
  complete farm evaluations are independent. Personal/global best decisions
  depend on completed constraint/objective pairs and use fixed-order commits.
- H2: pure C++ uses stack-local reductions, one persistent CPU team, and
  counter-keyed random events. It exposes every paper-native Case 1--3 and
  parametric-study configuration through one problem switch.
- H3: transition and evaluation loops are parallelized without nested teams;
  worker creation is amortized across all generations. Evaluator, algorithm,
  and end-to-end wall times remain separately reported. H6 uses only the
  serial semantic check and all-20-logical-CPU Waffle configuration.
