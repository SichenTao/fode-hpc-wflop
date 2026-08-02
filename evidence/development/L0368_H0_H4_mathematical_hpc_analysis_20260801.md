# L0368 H0-H4 mathematical and HPC analysis

## H0: source and semantic audit

The controlling paper is Liu et al., DOI `10.1016/j.enconman.2021.114610`, PDF SHA-256 `a81ca2a398ec6654af0755ca0f40dd64f206b0dd8a57cf244efe09cbe1701a24`. Exact-title, DOI, author, institution and GitHub searches found no target layout implementation or original Nanao seabed/wind arrays. The paper cites a same-author GA study whose CC0 Mendeley dataset, DOI `10.17632/bvrdgykzwy.1`, contains MATLAB `gaoptimset` calls with 500 generations, crossover fraction 0.3 and population sizes 50/100. These settings complete omissions but the dataset is not represented as target source.

The target defines 20 optimization problems, `S1-S5 x W1-W4`, on a 2000 m square with up to 25 turbines, a Qian-Ishihara Gaussian velocity/turbulence model, an engineering initial-capital model and a capital-to-power objective. Sixteen additional Figure-11-style evaluations apply the four S1 layouts to S2-S5 without reoptimization.

The executable repairs four mathematical issues. Equation 1 is a square L-infinity exclusion despite claiming physical 5D distance; the primary model restores Euclidean 5D and preserves the literal equation as a sensitivity mode. Equation 7 omits turbine count from a per-MW transformer cost; full-farm capacity is restored. Equations 9-10 are implicit capital shares and are solved algebraically. Equation 11 is capital divided by instantaneous expected MW, so it is not described as lifecycle LCOE.

The published result table cannot close under its own capital equations. S1W1 and S1W3 both contain 18 turbines on zero-depth terrain, hence Eqs.3-10 require identical capital, but `COE x displayed power` implies approximately GBP139.8m and GBP244.3m. The same contradiction occurs for the two 23-turbine S1W2/S1W4 rows. The implementation retains every table anchor but refuses a nonphysical wind-dependent capital calibration.

## H1: mathematical work decomposition

For layout `L`, wind state `q` and terrain `S`, the evaluator computes ordered upstream Qian-Ishihara velocity and added-turbulence propagation, turbine power from the paper's cubic Eq.2, and expected farm power `sum_q p_q P_q(L)`. Terrain affects the support length and cost at every turbine. Direct capital is the sum of turbine, support, cable, transformer and port terms; total ICC is `direct/(1-0.043-0.174)`. The minimized scalar is `ICC/E[P]`.

One individual therefore costs `O(|W| N^2)`, whereas rank selection, count mutation, coordinate variation and repair cost at most `O(N^2)`. W1 has one state, W2 has 36, W3 has 72 and W4 has 180. Population evaluation is consequently the dominant independent work, especially for the real-wind proxy.

## H2: high-performance design

One optimization owns one persistent full-core executor. Initialization and every frozen generation write individuals into fixed indices in parallel. Every individual performs a complete serial physics evaluation so nested thread teams cannot oversubscribe Waffle. Selection, stable ranking and elite commits remain ordered. Counter-keyed random events make the generated population independent of worker scheduling. Wind states, coefficients and terrain normalizations are immutable.

This design accelerates both algorithm construction and evaluator work while preserving the exact one-worker scientific result. It also makes the same algorithm/problem interface usable for future wind and terrain contracts.

## H3: implementation and correctness gates

The C++20 target is compiled with `-O3 -march=native -ffp-contract=off -Wall -Wextra -Wpedantic -Werror`. Unit tests cover the 20-case order, probability normalization, slope endpoints, the Euclidean-versus-literal spacing distinction, feasibility, physical FES and one/four-worker hash identity. The independent Python H5 validator checks every paper anchor, receipt field and one/four-worker layout/evaluation identity. It also transcribes Eq.2 independently for all four wind models, verifies one-turbine no-wake power and 100% efficiency, and closes the implicit capital equation and capital-to-power objective without reusing C++ formulas.

The final local S5W4 fixed-work candidate used population 100 and 100 generations. One worker took 7.100331144 s end to end and 7.076422635 s in evaluation; 20 workers took 0.599982503 s and 0.583208634 s. End-to-end speedup was 11.834230346, evaluator speedup 12.133604015, all 20 workers participated, and layout, objective, physics, physical FES and scientific hash were identical. This is local candidate evidence, not immutable Waffle H6.

## H4: formal campaign

Immutable Waffle H6 repeats that S5W4 fixed work with one worker and every available Waffle core. Formal work then runs all 20 native cases once, matching the paper's one reported result per case, at population 100 and 500 generations on all cores. The runner records raw layouts, capital components, power, efficiency, spacing, FES, timing, worker receipt, hashes, paper anchors and all sixteen cross-terrain transfers. It is resumable per case and does not claim author numerical replay.

The final local full-campaign gate passed all 20 cases and 16 transfers. Each case consumed population 100 times the initial population plus 500 generations, namely 50,100 physical evaluations; the matrix consumed 1,002,000 evaluations. Summed in-binary end-to-end time was 26.929610175 s on 20 requested workers, with 0.052957613--4.365222901 s per case. These are runner/campaign readiness data only; the immutable Waffle snapshot remains the formal evidence source.
