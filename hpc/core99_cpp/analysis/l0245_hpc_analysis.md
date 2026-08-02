# L0245 HPC analysis: polynomial-chaos AEP and layout optimization under uncertainty

## Scientific object

L0245 asks how to reduce the number of expensive wake-model calls required to
compute and differentiate annual energy production when wind direction and
wind speed are uncertain. Its target method is polynomial chaos with
regression (PC-R): draw Latin-hypercube wind states, evaluate physical power,
fit a total-order polynomial expansion, choose its order by ten-fold
cross-validation, and use the zeroth coefficient and its gradient as the AEP
objective. The paper compares this with the rectangle rule and applies both to
a 120-variable, 60-turbine constrained layout problem.

The controlled scientific unit is therefore one wind-state evaluator shared by
PC-R, rectangle integration and the Monte-Carlo reference. Changing the wake
model between methods would confound sample efficiency with simulator choice.

## Source and model audit

The official article links a CC-BY-4.0 Zenodo record containing four exact
60-turbine layouts and the 72-bin direction resource. The authors also publish
an older OUU repository that confirms the boundary, turbine abstraction,
FLORIS parameters, coordinate transform, OpenMDAO/DAKOTA/SNOPT lifecycle and
rectangle construction. That repository has no license and predates the final
two-variable experiments, so it is read-only evidence rather than copied
source or a target executable.

The continuous three-zone FLORIS equations are independently translated from
the contemporaneous Apache-2.0 PlantEnergy lineage. The author driver uses
constant `Ct=4a(1-a)`, constant
`Cp=(0.7737/0.944)4a(1-a)^2`, generator efficiency 0.944 and a 5 MW cap. The
paper's 3-25 m/s operational truncation takes precedence over the precursor
driver's 0-30 m/s interval. Exact two-variable DAKOTA arrays, SNOPT execution,
selected orders and trajectories are absent and are declared in the contract.

## Mathematical work decomposition

For fixed layout `x`, each wind state `xi_s` supplies a farm power `P(x,xi_s)`.
PC-R forms a basis matrix `Psi`, solves

`min_alpha ||Psi alpha - P||_2`,

and obtains the expected power from the coefficient of the constant basis.
Because `Psi` depends only on frozen wind samples, the mean-gradient weights
can be computed once and applied to every physical response gradient during
the optimizer. The rectangle rule instead supplies direct tensor-product
probability weights. Both therefore reduce to a fixed ordered weighted sum of
independent state responses after their plans are constructed.

The dominant physical kernel evaluates all directed turbine pairs for every
wind state. A gradient has 120 layout coordinates. Fixed-width eight-lane
forward automatic differentiation traverses the wake equations 15 times per
state rather than using 120 scalar finite-difference calls, while retaining
the exact derivative of the reconstructed value path away from documented
piecewise boundaries.

## HPC mapping

- One persistent executor owns all CPU workers during an evaluation or SLSQP
  optimization.
- Wind states are independent and write fixed indexed result slots, so every
  AEP and gradient call uses all available cores without nested thread teams.
- Each worker performs all 15 fixed-width AD blocks for its assigned state;
  this keeps derivative data local and avoids fine-grained scheduling.
- Regression, cross-validation, small dense factorizations, constraints and
  SLSQP control stay in deterministic serial orchestration because their work
  is small relative to the physical kernels.
- AEP and gradient reductions follow fixed state and coordinate order, making
  one-core and all-core scientific hashes identical.
- The 200000-sample Monte-Carlo reference uses the same state kernel without
  gradients and is parallelized over its complete sample set.

## Admission and formal protocol

H5 checks source-data cardinalities, the exact paper sample counts, four
layouts, finite and feasible AEP values, automatic derivatives against central
finite differences, identical one/all-core science, real worker participation,
PC-R/rectangle agreement with the 200000-sample reference, and non-worsening
smoke optimization.

H6 repeats the same 630-state PC-R-fine AEP-plus-gradient kernel three times at
one worker and at every available Waffle core. It reports median physical-kernel
time, speedup, observed workers and scientific hashes; setup and regression
times remain separately visible.

The formal campaign contains 160 fixed-layout method/set roles, four
200000-sample layout references and the paper's 120 optimizations: three starts,
four methods and ten sample sets. Every target role uses all available Waffle
CPU cores. Paper comparison baselines beyond the target PC-R and its necessary
rectangle/Monte-Carlo controls are not admission blockers.

## Claim boundary

The result is a source-backed flexible academic reproduction of the target
method, paper-native problem and protocol. It does not claim the authors'
target DAKOTA/FLORISSE/SNOPT executable, random samples, selected orders,
layouts, trajectories, numerical tables or timings.
