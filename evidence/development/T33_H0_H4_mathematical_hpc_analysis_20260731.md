# T33 H0--H4 mathematical and HPC analysis

## Identity and evidence boundary

- Corpus: `T33`
- Paper: *Combined Layout and Cable Optimization of Offshore Wind Farms*
- DOI: `10.1016/j.ejor.2023.04.046`
- Method semantic ID: `t33_combined_layout_cable_vns_declared_v1`
- Problem semantic ID: `t33_official_synthetic10_low_high_joint_npv_v1`
- Claim: academic flexible reconstruction of the paper's target combined
  layout-cable heuristic and all twenty paper problems. It does not claim
  author source, unpublished manual substation coordinates, an identical
  Gurobi execution, the original random stream, or numerical replay.

## H0: scientific contract and authoritative assets

The paper jointly optimizes the discrete turbine-layout variables `x` and
balanced radial cable variables `y`. Its maximization objective is

`NPV = EPF * AEP - foundation cost - cable cost`,

where `AEP` is annual energy production and `EPF=0.45` in the paper's
million-Euro/GWh scale, equivalently 450 EUR/MWh. The cable model enforces
flow capacity, one outgoing cable per selected turbine, balanced radial
strings, no crossings, and obstacle avoidance. The direct target is the
combined VNS that changes turbine positions and their cable connections
together.

The paper explicitly reuses the ten official synthetic offshore instances
from dataset DOI `10.11583/DTU.13134731`. The verified archive is consumed
byte-for-byte. It provides candidate positions, fixed neighboring turbines,
foundation costs, geometry and obstacles, RVO TNW wind states, and the NREL
15 MW curve. The paper delegates cable details to *Balanced Cable Routing
for Offshore Wind Farms with Obstacles*, DOI `10.1002/net.22100`; its
publisher PDF was retrieved and consumed. Exact-title, DOI, author and
GitHub searches found no target implementation.

Two provenance conflicts are frozen. Paper Table 1 calls its counts
available positions but reports `info.json` total points, including fixed
turbines; the binary decision set therefore follows
`availablePositions.txt`. Site A's README states high-density zone quotas
26/8, while the zone areas and paper total 40 imply 26/14; the latter
formula- and paper-consistent values are used.

The authors do not publish the manually placed substations, neighborhood
controls, source, random states, or exact proprietary-solver lifecycle.
The declared reconstruction places one fixed substation at each official
zone's candidate centroid, uses the cited predecessor's two cable types
(240 EUR/m for capacity four and 336 EUR/m for capacity six), and freezes
all other missing controls in the semantic contract.

## H1: dominant mathematical work

For a site with `N` available positions and `S` positive-probability wind
states, building the pairwise wake approximation costs
`O(N^2 S)` arithmetic and `O(N^2)` packed storage. Site J has 21,634 legal
positions, so this preprocessing dominates a naive implementation.

PDSP initialization begins with every candidate active. Its contribution
construction and progressive removal updates together also expose
`O(N^2)` pair work. Once a layout contains `T` turbines, obstacle-aware
cable routing requires visibility paths, balanced angular partitions, and
exact ordering of strings whose capacity is at most six. Combined 1-opt and
2-opt then evaluate independent candidate deltas against `T` selected
turbines.

## H2: high-performance decomposition

One persistent worker team owns each optimization. The implementation:

1. reuses the independently validated T31 Jensen evaluator and official
   input parser;
2. stores one symmetric wake value per unordered candidate pair in a
   memory-mapped cache;
3. builds matrix rows and PDSP contribution/update rows in parallel;
4. precomputes immutable polygon visibility and all-pairs vertex paths and
   assigns separable lateral lanes to the resulting rooted-path forest;
5. evaluates ten independent balanced Sweep rotations in parallel;
6. solves every capacity-six radial string exactly by subset dynamic
   programming;
7. evaluates independent 1-opt and 2-opt candidates in parallel, then
   applies one stable ordered move;
8. reuses each site's wake cache across both densities and all 25 seeds.

Counter-keyed random events bind every stochastic choice to a logical VNS
cycle. Fixed-index writes, stable ties and ordered commits preserve the
scientific trajectory across worker counts.

## H3: semantic and numerical validation

The independent Python validator checks all twenty registered cases against
the official archive and constructs a feasible reference layout and cable
network for every case:

- candidate counts: 3,196 through 21,634;
- neighboring fixed turbines: 8 through 75;
- published low-density totals: 20 through 156 turbines;
- published high-density totals: 40 through 313 turbines;
- 177 positive-probability RVO TNW wind states.

For the official A low-density fixture, the validator independently sums
foundation costs from `availablePositions.txt`, recomposes
`revenue = 450 * AEP` and
`NPV = revenue - foundation - cable`, checks 1,200 m spacing, balanced cable
capacity and zero crossings, and requires the cable component to lie within
50% of the paper's 13 MEUR scale. A two-cycle run has identical objectives
and scientific hash with one and four workers.

## H4: Spark full-work profile

The representative comparison uses the same optimized C++ source, official
A low-density problem, seed 330046, complete 860-cycle formal workload, and
separate empty pair-matrix caches for one and twenty workers. Both runs
evaluate 323,539 combined layout candidates and 8,610 Sweep rotations,
produce scientific hash `2324969962124435034`, and return the same feasible
best NPV.

| Component | 1 worker C++ | 20 worker C++ | 20/1 speedup |
|---|---:|---:|---:|
| packed Jensen pair matrix | 10.390202 s | 0.759817 s | 13.675x |
| PDSP plus first routing | 0.032014 s | 0.024204 s | 1.323x |
| combined 1-opt/2-opt candidates | 1.990411 s | 0.355459 s | 5.600x |
| cable recomputation | 1.457085 s | 0.945721 s | 1.541x |
| VNS optimization stage | 3.508215 s | 1.364007 s | 2.572x |
| end to end | 14.002562 s | 2.172638 s | 6.445x |

The public data provide obstacle polygons but no lateral lane widths or
offset costs for cables sharing a mandatory visibility corridor. The
declared completion therefore assigns separable parallel lanes to each
zone's rooted-path forest; obstacle-avoiding shortest-path length remains
charged, and final routed crossings are zero by planar construction. The
dominant matrix, candidate, cable, optimization and end-to-end stages all
accelerate. Waffle H6 will repeat the complete one/all-twenty-worker
comparison on the immutable committed snapshot.
