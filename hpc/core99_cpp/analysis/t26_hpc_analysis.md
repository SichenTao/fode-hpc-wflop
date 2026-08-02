# T26 PIDNN--GTDE HPC analysis

## Scientific work graph

The paper pair contains two distinct work ledgers. Offline PIDNN training uses
23 inflow speeds, 120000 paper-stated samples per speed, a 4--32--16--8--3
velocity encoder, a second Ct encoder and first/second coordinate derivatives
for the four physical residuals. The layout phase uses 300 layouts per
generation, 80 vector-valued turbine genes, 12 directions, six digitized
speed bands and 1000 ordered generations. One full target run therefore owns
300300 complete-layout evaluations; training work is never relabelled as FES.

## Legal parallel axes

- Training samples and every neural layer are batched tensor operations.
- The trained PIDNN is evaluated once on a fixed 6 by 121 by 81 wake table.
- All layouts, pair distances, six speed bands and turbine powers in one wind
  direction are batched; twelve directions are accumulated in a fixed order.
- All non-best GTDE mutation/crossover events use the frozen generation best.
- Independent formal seeds are an outer campaign axis only after H6 selects a
  complete target backend.

Generation commits, best selection and the 10000 Adam steps remain ordered.
Changing these dependencies would define a different algorithm.

## Backend design

LibTorch is called directly from C++. CPU execution sets the selected intra-op
worker count; CUDA keeps training, wake-table construction, population
evaluation and variation on device. The evaluator vectorizes all six speed
bands in one lookup so it does not repeat table-index construction six times.
The population tensor remains bounded (300 by 80 by 2); pair work is produced
one direction at a time, avoiding a 12-direction resident tensor. Host-side
random events make CPU/CUDA variation inputs identical.

## Development measurements

On the local 20-core development host, the paper population with two complete
generations took 7.7180 s at one LibTorch CPU worker and 4.2675 s at 20 workers,
or 1.8085 times end-to-end. In the paired warm backend-selection observation,
20-core CPU took 2.1364 s and CUDA took 0.5893 s, a 3.6255-times speedup. Both
backends returned the same final AEP and fitness in the bounded H6 profile.
These numbers are development admission evidence, not the authoritative
Waffle report.

## Admission boundary

H5 requires the exact 8 by 10 layout, 80 two-component genes, the 1554.20 GWh
regular-layout anchor, 300300 paper-profile FES, finite losses, an interpolation
MAE below 0.02 and worker-count scientific agreement. Waffle H6 repeats the
one/all-core measurements and admits CUDA only when the complete optimizer
agrees with CPU within 0.05 GWh and is faster. Formal execution then trains one
10000-step artifact from scratch and runs one native plus 25 declared
robustness seeds on only that highest-performance backend.
