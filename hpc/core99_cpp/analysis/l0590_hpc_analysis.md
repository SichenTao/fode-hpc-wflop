# L0590 3-D ANN wake and joint layout-height HPC analysis

## Scientific work units

The paper has two distinct computational kernels:

1. train the 3-5-6-1 ANN from a high-resolution three-dimensional analytical
   wake field; and
2. evaluate 64 independent 30-turbine candidate layouts in every GA
   generation, including 13 rotor points, ordered source-target wake queries,
   RSS superposition, power and height-dependent cost.

The physical function-evaluation unit is one complete 30-turbine farm
evaluation. ANN training samples and rotor-point queries are internal work and
are not counted as optimization FEs.

## Mathematical factorization

The recovered source equations factor the teacher into downstream-only wake
radius and flux coefficients plus independent pointwise Gaussian terms.
Training samples are independent once those equations and the deterministic
lattice coordinate are fixed. ANN forward/backward operations are independent
by sample until gradient reduction. Farm candidates are independent within a
generation. These facts expose three safe parallel levels:

- teacher-data generation;
- deterministic gradient chunks followed by a fixed-order reduction; and
- population evaluation.

GA selection, crossover, mutation, survival and history reduction remain
deterministically ordered. They are light compared with farm evaluation and
parallelizing their short loops would create synchronization overhead.

## Implemented backend

The pure-C++ backend uses the persistent FODE-HPC executor. It creates the
worker team once per training or optimization run. Training always uses 64
logical chunks independent of worker count, so one-core and all-core floating
point reduction order is identical. Counter-keyed random events make GA
offspring independent of scheduling. No nested parallelism is used.

## Admission measurements

The local four-worker paper-generation probe on E4 used identical source,
weights, seed, 838 generations and 53,696 physical FEs:

| component | one worker | four workers | speedup |
|---|---:|---:|---:|
| farm evaluation | 2.181642 s | 0.837560 s | 2.605x |
| GA orchestration | 0.440341 s | 0.446942 s | 0.985x |
| end to end | 2.621983 s | 1.284502 s | 2.041x |

The scientific hash was identical (`4b1e3525863f841c`), and the best total
power improved from 18,967.45 kW to 45,208.91 kW. These are local admission
measurements, not Waffle H6 or paper conclusions.

The separate 32,768-sample, 1000-epoch from-scratch training probe used the
same sample/weight hash with one and four workers. Time decreased from
2.149948 s to 0.657921 s, or 3.268x. Its held-out MSE was 8.234e-4; the
paper's 1e-6 target was not reached on the declared proxy and remains an
explicit evidence boundary.

## Waffle H6 and formal policy

H6 trains from scratch and runs E4 once with one worker and all available
Waffle workers, requiring identical weight and optimization hashes. It reports
training, evaluator, optimization and CLI speedups separately. The formal
campaign trains one frozen 1000-epoch surrogate and runs all eight named paper
cases for 25 repeats. Fixed aliases are retained as named receipts. The runner
chooses independent one-worker case/repeat tasks when that maximizes aggregate
throughput and never exceeds the twenty-worker Waffle allocation.

## Claim boundary

The implementation reproduces the paper architecture, lifecycle, equations,
constraints, objectives and named cases. Missing Shiren arrays, trained
weights, GA settings and cost values are versioned completions. Failure to
reach the paper's 1e-6 MSE on the declared proxy is recorded and is not
silently converted into author-equivalent accuracy.
