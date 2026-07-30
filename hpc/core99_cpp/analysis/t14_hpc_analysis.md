# T14 boundary-grid H0–H4 analysis

Paper: Stanley and Ning, *Massive Simplification of the Wind Farm Layout
Optimization Problem*, DOI `10.5194/wes-4-663-2019`.

## H0 — paper contribution and exact workload

The target contribution is the five-variable boundary-grid layout
parameterization, not SNOPT. The paper compares it with a four-variable grid
and 200-variable direct-coordinate representation for 100 turbines. Three
one-factor experiment axes collapse to seven unique cases:

- Princess Amalia boundary and North Island wind rose at average spacings
  4D, 6D, and 8D;
- Princess Amalia boundary and 4D spacing with North Island, Ukiah, and
  Victorville wind roses;
- 4D spacing and North Island wind rose with Amalia, circle, and square
  equal-area boundaries.

Every representation/case pair has 100 random starts. Optimization uses
24 directions, five Weibull speed samples, and four rotor points. Final
evaluation uses 360 directions, 50 speed samples, and 100 rotor points.

SNOPT and the authors' random states are unavailable. Therefore the platform
reproduces the paper contribution and problem protocol, but labels its
deterministic feasibility-first parallel evolution strategy as a
reconstruction rather than author SNOPT.

## H1 — mathematical decomposition

For a layout \(X\), one optimization evaluation is

\[
 AEP(X)=8760\eta\sum_{d=1}^{24}\sum_{s=1}^{5}
 p_{d,s}\sum_{i=1}^{100}P(u_i(X,d,s)).
\]

The final evaluator replaces \(24\times5\times4\) with
\(360\times50\times100\). For one wind state:

1. rotate positions into the wind frame;
2. order turbines from upstream to downstream;
3. evaluate Gaussian deficits at rotor sample points;
4. combine upstream deficits in local-velocity linear order;
5. apply the thrust and power curves.

The representation decoder, boundary constraint, spacing constraint, and
deterministic selection are separate from the physical evaluator.

## H2 — cost and parallel grain

The dominant optimization work is approximately

\[
O(N_dN_sN_rN_t^2),
\]

with independent candidates. The dominant final-evaluation work has
independent wind states. Candidate evaluation is therefore the preferred
search-time parallel grain; wind-state evaluation is the preferred
single-layout final-evaluation grain. Pair, rotor-point, or tiny decoder loops
are not separate thread regions because their scheduling overhead would exceed
their useful work.

The direct representation increases optimizer dimension but does not change
physical-evaluator complexity. Boundary-grid and grid decoding cost
\(O(N_t)\), which is negligible beside the wake model.

## H3 — pure-C++ optimization decisions

- One persistent worker team is created per optimization.
- A search generation emits at least one candidate per available worker.
- Candidate layouts are evaluated concurrently; their results are committed in
  deterministic candidate order.
- The final 360-by-50 evaluation flattens wind states across the same worker
  team.
- Wind-resource arrays and the Amalia boundary are generated once from the
  public archive and compiled as immutable arrays.
- Boundary construction, topology selection, and physical FES accounting are
  outside timed inner kernels.
- The 24-by-5 optimizer and 360-by-50 final evaluator use the same equations;
  only quadrature resolution and rotor sampling differ.

## H4 — semantic and performance hazards

1. The public archive omits SNOPT and the legacy Akima dependency.
2. The paper states cut-in 3 m/s, rated 10 m/s, and efficiency 0.93, while the
   public driver uses 4 m/s, 9.8 m/s, and 0.936. Paper values control.
3. The public wind-resource code repeats the positive half-bin at direction
   zero instead of wrapping the negative half-bin. This released behavior is
   retained and declared so it cannot silently drift.
4. The public driver uses a dynamic local-TI wake expansion while the paper
   presents \(k=0.3837TI+0.003678\). The paper equation controls this semantic
   baseline.
5. Exact numerical equality to the paper is not a gate because SNOPT,
   Tapenade runtime state, and author initial states are unavailable.
6. H6 must report observed participants, physical FES, coarse and fine
   evaluator time, feasibility, and a deterministic scientific hash.
7. Formal 100-start execution is admitted only after the all-core H6 receipt
   establishes a practical policy for the expensive final quadrature.
