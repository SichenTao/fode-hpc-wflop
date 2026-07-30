# T48 paper-paired HPC analysis

Paper: *An Analytical Framework for Offshore Wind Farm Layout Optimization*  
DOI: `10.1260/030952407780811401`

## Mathematical work graph

For each two-turbine layout, the objective performs:

1. a 360-point periodic direction integration;
2. two turbine-specific shore-distance and wake transformations;
3. a 160-point Weibull-speed integration for every turbine-direction pair;
4. fixed-order aggregation into AEP, capital cost, O&M, and LCOE.

The reconstructed gradient iteration generates two bounded perturbations for
each of four coordinates. Its eight trial layouts are independent. Production
therefore flattens `candidate × turbine × direction` into one persistent task
space. Each task performs its own speed integral, after which fixed-order
reductions recover the exact deterministic candidate values. Candidate
acceptance is ordered and serial.

This exposes all available cores even though the paper demonstration has only
two turbines and eight trials per iteration. It also provides the same problem
interface for future algorithms without altering the LCOE equations.

## Evidence and reconstruction boundary

The paper does not publish its wind arrays, power curve, search rule, or
quadrature. Figure 1 was digitized into 16 periodic markers. The declared
piecewise-linear 1.5MW curve is calibrated to the paper's explicit 42% isolated
capacity-factor anchor. Search uses deterministic coordinate-gradient trials.
These are academic reconstruction decisions, never author-source claims.

H5 uses multiple independent anchors rather than one fitted endpoint:

- initial LCOE: paper 0.105 dollars/kWh;
- illustrative two-turbine capacity factor and wake loss: 40.5% and 4%;
- reported final LCOE, capital, and capacity factor: 0.051 dollars/kWh,
  approximately 4.5 million dollars, and 41.5%.

H6 runs the complete 190-iteration, 1,521-physical-evaluation reconstruction on
all currently available Waffle cores and records CPU utilization, timings,
worker participation, feasibility, and scientific hash.
