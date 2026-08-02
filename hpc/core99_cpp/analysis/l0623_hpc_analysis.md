# L0623 adaptive CFD-Kriging-GA HPC analysis

## Scientific work units and fidelity boundary

The paper separates expensive truth calls from cheap surrogate evaluations.
Each truth call evaluates one complete eight-turbine layout for one or eight
wind states. The three cases use 437, 400 and 399 truth calls, respectively,
including 360 initial LHS samples. A surrogate FE is one Kriging prediction
for one complete layout and is recorded separately; it is never counted as a
truth call.

The authors did not release the modified OpenFOAM/ADM solver, cases, meshes or
CFD response arrays. This package therefore reproduces the paper problem and
adaptive Kriging-GA lifecycle against a declared terrain-aware ADM/Gaussian
response proxy. The proxy is not called CFD and does not support CFD speed or
accuracy claims.

## Mathematical factorization

The initial 360 truth responses are independent. Once a Kriging model is
frozen for an adaptive step, every population prediction and every GA
offspring construction is independent. These form the safe parallel regions:

- initial LHS truth-response generation;
- population-wide Gaussian-kernel predictions;
- tournament, crossover, feasible repair and mutation for each offspring.

The paper's repeated surrogate retraining admits a more important algorithmic
optimization. Appending one truth response changes the Gaussian kernel matrix
by one row and column. A rank-one Cholesky extension costs quadratic work and
exactly replaces a fresh cubic factorization. Kernel hyperparameter MLE is
performed once on a deterministic subset because the paper omits its repeated
search policy. Best-design reductions remain fixed-order.

## Implemented backend and local admission

The pure-C++ implementation owns one persistent worker team. Counter-keyed
events make LHS repair and GA offspring independent of thread scheduling. The
local complete Case-I probe used all 360 initial samples, 437 truth calls and
102,000 surrogate FEs. One and four workers produced the same scientific hash
(`5f616a882351a9d`):

| component | one worker | four workers | speedup |
|---|---:|---:|---:|
| declared truth proxy | 0.001576 s | 0.000795 s | 1.983x |
| Kriging inference | 0.222423 s | 0.075790 s | 2.935x |
| end to end | 0.502353 s | 0.192701 s | 2.607x |

Kriging training was 0.014579 s versus 0.019734 s because deterministic
rank-one linear algebra is serial and already small; this component is
reported rather than hidden. The larger GA orchestration term also includes
feasible set construction and fixed reductions. These are local admission
measurements, not Waffle H6 or paper conclusions.

## Waffle H6 and formal policy

H6 runs complete Case I with one worker and all twenty Waffle workers and
requires equal truth calls, surrogate FEs, scientific hash and result. It
reports truth, training, inference, orchestration and end-to-end times
separately; only components that actually accelerate receive a speedup claim.
The formal campaign runs all three paper cases for 25 independent seeds using
twenty concurrent one-worker processes and no nested oversubscription.

## Claim boundary

The implementation supports claims about the paper's discrete problem,
adaptive truth/surrogate budget separation, Kriging-GA behavior and pure-C++
HPC execution on the declared response proxy. It cannot support claims about
author OpenFOAM fidelity, CFD numerical accuracy, exact CFD cost, original
random state or numerical replay.
