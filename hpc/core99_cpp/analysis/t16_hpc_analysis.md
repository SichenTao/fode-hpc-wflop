# T16 FODE-HPC-level analysis

## H0-H2: paper semantics and mathematical work

T16 optimizes 76 continuous coordinates for 38 turbines. One physical
objective evaluation solves twelve independent wind directions. Within each
direction it sorts turbines upstream-to-downstream, evaluates the modified
Bastankhah-Porté-Agel wake and the Niayifar-Porté-Agel linear superposition,
and optionally propagates local turbulence intensity. The dominant work is
\(O(N_D N_T^2 Q)\), where \(N_D=12\), \(N_T=38\), and rotor quadrature
\(Q=1\) during optimization or \(Q=100\) during final assessment.

The objective gradient has 76 components. A finite-difference implementation
would multiply evaluator work by approximately 77 and would not reproduce the
paper's exact-gradient premise. The implementation therefore uses a
fixed-width forward automatic-differentiation scalar and evaluates all 76
derivatives in one traversal. The 741 pair-spacing and 38 circular-boundary
constraint Jacobians are analytic and sparse.

The public text CP/CT header conflict was detected through numerical
cross-validation. Following its source pickle, conversion script, and
generator efficiency reproduces the paper's 481 GWh BP baseline within the H5
tolerance. SOWFA observations remain validation data; unavailable LES cases
are never replaced with a synthetic claim.

## H3-H4: high-performance implementation

The evaluator uses contiguous coordinate and turbine-curve arrays, stack
storage for all twelve directional gradients, allocation-free pair loops, and
a fixed upstream reduction order. One persistent C++ worker team evaluates
the twelve directions concurrently. Exact gradients, objective values, and
one-worker/multicore results share the same source and fixed reduction order.

The paper-native experiment contains one baseline and 199 independent random
starts. Inner direction parallelism has only twelve tasks and is too fine for
twenty workers at every short SLSQP callback. A fixed-work Waffle check found
1.732, 0.707, and 0.542 seconds end-to-end with respectively 1, 4, and 12
workers, with the identical scientific hash. A single run therefore benefits
from inner parallelism, but twenty concurrent one-worker starts have much
higher projected campaign throughput than one twelve-worker plus one
eight-worker run. The formal campaign uses twenty concurrent starts: all
twenty CPU cores are consumed while every optimizer retains its mathematically
sequential SLSQP state and nested oversubscription is absent.

The WEC lifecycle is not shortened: factors 3.00, 2.75, ..., 1.00 are followed
by the smooth-local-TI stage. Final assessment uses hard local TI and 100 rotor
points exactly as the paper specifies. NLopt SLSQP replaces proprietary SNOPT;
the replacement and missing author random states are declared in every source
unit and semantic contract.

## H5-H6 admission and formal execution

H5 validates the 76-dimensional exact gradient against central finite
differences, constraints, deterministic feasible starts, CP/CT source
correction, one-worker/multicore scientific parity, fixed-seed replay, and the
published BP Table-2 AEP and twelve directional powers.

H6 runs the maximum-throughput full-resource Waffle configuration and records
worker participation, evaluator/optimizer/end-to-end time, every WEC-stage
receipt, final feasibility, scientific hashes, and source commit. The same
resumable campaign then completes all 200 paper-native starts. This is an
academic declared reproduction of the equations, problem, and optimization
lifecycle, not a claim of bitwise SNOPT/Tapenade or SOWFA replay.
