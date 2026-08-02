# T58 TOPFARM high-performance analysis

T58 is Rethore et al., *Wind Energy* 17 (2014), DOI
`10.1002/we.1667`. Its scientific unit is a multi-fidelity economic layout
optimization: a coarse binary simple genetic algorithm (SGA) supplies a
global layout, and a fine continuous sequential linear programming (SLP)
stage refines it. The objective combines lifetime energy revenue, foundation
and cable investment, fatigue degradation, and operation and maintenance.

## Mathematical work graph

Let `N` be the turbine count, `S` the direction-speed state count, `P=20`
the SGA population, and `d=2N` the continuous coordinate dimension. One
stationary wake evaluation costs `O(S N^2 Q)`, where `Q=9` is the fixed rotor
quadrature size. The fine evaluator has 12 directions times 12 speeds, hence
`S=144`; the probability-preserving coarse evaluator has `S=36`.

Each wind state is independent until annual energy and fatigue damage are
reduced. Each SGA generation contains `P` independent complete layouts. One
SLP iteration evaluates `2d` plus/minus layouts for its local linear model
and four move-limit trial layouts. Cable construction costs `O(N^2)`, and
spacing checks cost `O(N^2)`. These three independent axes contain nearly
all legal parallel work.

The generation transition, rank selection, crossover/mutation event
assignment, SGA-to-SLP handoff, SLP move-limit adjustment, and optimizer
iteration sequence are dependency chains. They remain ordered because
parallelizing their state commits would change the target algorithms.

## High-performance realization

One persistent C++ worker team serves every inner parallel region. A single
layout partitions wind states. SGA partitions its 20 complete layouts and
uses serial wind-state kernels within each layout. SLP partitions finite-
difference and line-search layouts. This runtime selection avoids nested
thread teams and gives each stage the coarsest available independent work.

All parallel tasks write fixed output slots. Annual energy, lifetime loads,
fitness ranks and accepted trial states are reduced or committed in a fixed
order. Random crossover, mutation and selection events are addressed by
generation, individual and bit through a counter-keyed generator. Therefore
one-core and all-core runs execute the same physical evaluations and produce
the same layouts, values and scientific hashes.

The formal campaign has a second legal axis. The paper reports one run for
each of five native roles; the reconstruction adds 25 explicitly labelled
seeds for every stochastic SGA role. These independent runs use single-worker
processes scheduled across all Waffle cores, while H6 separately measures
single-optimization acceleration with one and all cores.

## Admission evidence and boundary

H5 covers all three paper cases, all five native roles, 144 fine states,
reported baseline efficiencies, cost-branch sensitivity, feasibility,
physical evaluation counts, multicore participation and exact one/all-core
scientific identity.

Local complete-budget development probes on 2026-08-01 reduced Stags
SGA+SLP from 66.21 to 7.27 seconds (9.11x) and Middelgrunden SGA+SLP from
84.13 to 9.74 seconds (8.63x). Both used 1000 SGA generations followed by
the paper's 30 or 20 SLP iterations and retained identical scientific
hashes. These values are development evidence; the immutable Waffle campaign
is the authoritative H6 result.

The implementation is a source-backed flexible academic reproduction. It
does not claim the unavailable HAWTOPT source, HAWC2-DWM load database,
complete site arrays, maintenance distribution parameters, author random
state or exact optimized numerical trajectory.
