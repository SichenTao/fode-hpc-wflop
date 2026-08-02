# Y14 H0-H4 mathematical and HPC analysis

## Evidence and executable boundary

The controlling paper is Zhang et al., DOI `10.1109/TSTE.2026.3661110`, PDF SHA-256 `1479f6921738c9485f27e181d515e21fcc6e52df67a2d1df8a6bf40e55665a08`. Exact-title, DOI, p-SDRDE, author, GitHub-code and institutional searches found no target source or native numeric archive. The implementation is therefore a paper-first flexible academic reconstruction, never an author-code or numeric-replay claim.

The paper fully determines the coordinate encoding, objectives, constraint, Algorithms 1-4, turbine table, three site sizes, population 50, subpopulation 10, replacement limit `N/2`, 150000 evaluation slots, two preference rounds and ten independent runs. It does not publish real layouts, measured wind arrays, exact octave/ground settings, `CR`, the SFM learning period, overflow behavior, seeds, or detailed tie rules. Every completion is frozen in `shared/contracts/core99_y14_zhang_2026.json` and repeated at the top of the source.

## H0-H1: mathematical contract

Each individual contains `2N` continuous coordinates. Feasible turbines remain one rotor radius inside the rectangular boundary and every pair satisfies `distance >= sqrt(8) R = 4R/sqrt(2)`, exactly as Eq. 1 prints. The two minimized objectives are negative AEP and maximum receiver A-weighted SPL.

AEP uses the Gaussian power curve, sixteen-direction Jensen wake, RSS deficit superposition, the figure-consistent Weibull scale 8.3/shape 2.0, and the digitized direction PMF. Noise uses all turbines, eight octave bands, boundary receivers plus the farm center, and a declared hard-ground ISO-9613-2 engineering specialization. The three original and three adjusted-reference roles retain Tables III and VI literally.

One physical FES is one complete AEP-plus-SPL evaluation of one feasible layout. The paper explicitly avoids evaluating infeasible repaired trials, while Algorithm 1 advances its evaluation counter by `NP`; the implementation therefore reports both nominal evaluation slots and actual physical FES instead of silently conflating them.

## H2: p-SDRDE reconstruction

For each frozen generation, the population is partitioned into groups of ten. `NSDE/best/1` uses the preference-ranked group best and Eq. 13 Gaussian/Cauchy scale sampling; non-finite/extreme samples are deterministically clipped to the declared `[0.05,1.5]` executable range. The mutant layout is recoded as a population of turbine coordinates, then local `DE/rand/1` and binomial crossover construct the ordered replacement candidates. The dual replacement first uses random indices and then Laplace-smoothed rolling SFM probabilities after a declared 50-generation learning period. Conflicting turbines are replaced sequentially, at most `N/2` times. Parent and offspring are merged and selected by non-r-dominated rank then crowding distance. The non-r threshold decreases linearly from 1 to 0.1; the adjusted-role noise weight increases linearly from 0.5 to 0.7.

## H3: high-performance transformations

1. Precompute expected turbine power over a dense wake-deficit table rather than repeating Weibull quadrature for every turbine and direction.
2. Precompute single-source octave intensity by 1 m three-dimensional distance, turning SPL into table lookup and deterministic summation.
3. Keep one layout evaluation internally serial; distribute the independent layouts over one persistent all-core executor to avoid nested oversubscription.
4. Construct all offspring from an immutable parent/rank/SFM snapshot in parallel, write fixed indices, evaluate feasible trials in parallel, then commit SFM events in target order.
5. Compute pairwise dominance rows in parallel; perform front propagation and crowding ties deterministically.
6. Use counter-keyed random events, fixed-index writes and ordered reduction so worker count cannot change the scientific trajectory.

## H4: validation and formal campaign

H5 must verify all six role identities, Table-II/III/VI constants, 67/91 receiver counts, Weibull interpretation, power/noise scalar fixtures, spacing and boundary feasibility, exact stopping counters, and one/multicore front/FES/hash identity. H6 compares the same pure-C++ executable at one versus every Waffle core on fixed reduced work for all six roles. Formal work then runs the maximum-performance configuration for `3 farm sizes x 2 preference rounds x 10 independent seeds`, each with population 50 and 150000 nominal evaluation slots. Local results remain development evidence until reproduced from an immutable Waffle snapshot.
