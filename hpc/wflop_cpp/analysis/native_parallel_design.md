# Native parallel design freeze

One optimization creates one `PersistentExecutor` with the requested total
thread count.  The caller is worker zero; the other workers are created once,
report ready, and remain alive until that optimization ends.  Every
algorithm-safe loop and every shared evaluator phase submits work to this same
team.  No algorithm invokes seed-level concurrency.

The shared evaluator preserves the frozen mathematical phase order:

1. map one-based grid cells to Cartesian coordinates;
2. rotate coordinates for every wind direction and stably order turbines
   upstream-to-downstream;
3. evaluate all downstream wake-deficit tasks;
4. integrate the 13 wind-speed bins with joint direction-speed probability;
5. stably sort per-turbine accumulated power for MATLAB-compatible summation.

`TotalAndPerTurbine` additionally materializes the stable ascending turbine
power order and corresponding one-based cell indices.  It does not call a
second physics implementation.

Algorithm-level safe parallel work is frozen as follows:

- FODE, ISE, LSHADE and CLSHADE use read-only generation snapshots for
  independent offspring generation, repair and evaluation.  Archive, memory
  and population-size changes use deterministic ordered merges.
- AGA and SUGGA move the worst turbine independently for each layout and
  evaluate the population in parallel.  Selection and source-compatible
  genetic output order remain deterministic.
- AGPSO and CGPSO follow their papers' population-wide stages.  Particle and
  coordinate work is parallel inside a stage; a completed `parallel_for` is
  the stage barrier.  The global-best reduction is deterministic.
- CLSHADE and CGPSO complete their single current-best chaotic local-search
  candidate before the next population-wide stage.

The frozen Spark2 v2 execution used one process, `taskset -c 0-19`, and a
persistent 20-thread team. All result rows record both requested and observed
live team size. Its archival campaign script rejects a different host,
non-20-CPU affinity, missing admission evidence, and incomplete result files.
A Waffle or other-host campaign must use a separate host contract and receipt.
