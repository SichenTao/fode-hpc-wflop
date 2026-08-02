/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T64 energy-noise-land problem and static, dynamic, and
death-penalty NSGA-II API
Paper title: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization
Paper DOI: 10.1016/j.renene.2015.06.026
Public source: the paper states that an in-house serial C++ implementation
was used; exact-title, DOI, author, repository, and lab searches on
2026-07-31 located no target code, native polygon maps, receptor arrays, wind
matrix, random states, or result fronts. The author's public thesis confirms
the problem and method but does not provide an executable package.
Related public source: pinned MIT-licensed PyWake ISO 9613 implementation
and its hash are recorded in the same-lineage T72 contract and are reused
only as an independent acoustic-formula check.
Related public source URL:
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git at revision
5b07481ec9b3633a74844651648f266ba82a8b32.
Paper-provided facts: continuous 2N layout coordinates; 3 km square; Jensen
wake with 24 directions and 43 speeds; ISO-9613-2 maximum receptor noise;
225 similar-area convex land polygons; 70/80/90 percent land availability;
5/10/15 turbines; 385 m spacing; real-coded elitist NSGA-II; crossover 0.95;
mutation 0.05; populations 200/150/100; 80000 complete layout evaluations;
static penalties 1e4 and 4e4; dynamic penalty 1e4 with Cgen=ngen and
Cgen=ngen/2; death penalty; four 80-percent, 10-turbine uniformity cases.
Missing assets: author source; native polygon vertices, forbidden-cell
identities and receptors; raw 24-by-43 probabilities; octave spectra and
atmospheric/ground constants; crossover and mutation distribution indices;
offspring count; death-regeneration random lifecycle; four native
uniformity maps; seeds, trajectories, and machine-readable fronts.
Paper conflicts and ambiguity: AEP is a maximization objective although the
printed penalized AEP equations add a positive penalty; Eq. (4) prints a
distance comparison with dimensional ambiguity; the stated feasible initial
population and death replacement do not define a construction procedure.
Reconstruction and completion: inherit the independently H5-validated T72
same-lineage evaluator components without changing their default T72
semantics; select a T64-specific profile with the paper's 0.3u^3 kW power
curve and constant Lw=100 dB; use standard full Jensen deficit, the cited
direction-conditioned Weibull reconstruction, pinned-PyWake-checked ISO
constants, deterministic
similar-area jittered Voronoi maps, one receptor per forbidden cell,
Deb-standard eta_c=eta_m=20, offspring size equal to population, and
counter-keyed feasible sequential initialization. Minimize negative AEP plus
positive penalty and SPL plus positive penalty. Death replacement consumes
one additional physical layout evaluation and uses a newly generated
feasible layout. Four deterministic map variants complete the uniformity
study.
Method semantic ID: t64_nsga2_three_penalties_declared_reconstruction_v1
Problem semantic ID: t64_energy_noise_land13role_declared_reconstruction_v1
Protocol semantic ID: t64_80000fes_25seed_penalty_uniformity_v1
Production backend: pure C++20 CPU-HPC. One persistent team parallelizes
feasible initialization, physical population evaluation, offspring
variation, death replacement, and dominance construction; fixed-index work,
counter-keyed events, stable ranks, and ordered environmental selection
preserve one/all-worker scientific identity.
Claim boundary: academic flexible paper/lineage reconstruction of all target
penalty methods and paper problem roles; not author code, native maps, native
wind array, original random stream, or numerical replay.
Contract: shared/contracts/core99_t64_sorkhabi_2016.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "core99/sorkhabi_t72.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t64 {

enum class PenaltyMode {
    static_1e4,
    static_4e4,
    dynamic_cgen_ngen,
    dynamic_cgen_half_ngen,
    death,
};

struct RunConfig {
    std::uint64_t seed = 20260731;
    int workers = 20;
    std::uint64_t physical_fes = 80000;
    PenaltyMode penalty_mode = PenaltyMode::dynamic_cgen_ngen;
    bool enable_convergence = true;
};

struct RunResult {
    std::string problem_id;
    std::string problem_semantic_id =
        "t64_energy_noise_land13role_declared_reconstruction_v1";
    std::string method_semantic_id =
        "t64_nsga2_three_penalties_declared_reconstruction_v1";
    std::string protocol_semantic_id =
        "t64_80000fes_25seed_penalty_uniformity_v1";
    std::string penalty_mode;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_fes = 0;
    int generations = 0;
    int population_size = 0;
    bool converged = false;
    double measured_land_availability = 0.0;
    double uniformity_parameter = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<t72::FrontPoint> front;
};

class Problem {
public:
    Problem(
        int land_availability_percent,
        int turbine_count,
        int map_variant = 0
    );

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int land_availability_percent() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int map_variant() const noexcept;
    [[nodiscard]] int population_size() const noexcept;
    [[nodiscard]] double measured_land_availability() const noexcept;
    [[nodiscard]] double uniformity_parameter() const noexcept;
    [[nodiscard]] const std::vector<t72::Point>& receptors() const noexcept;
    [[nodiscard]] t72::Evaluation evaluate(
        const std::vector<t72::Point>& layout
    ) const;
    [[nodiscard]] const t72::Problem& shared_evaluator() const noexcept;

private:
    std::string id_;
    t72::Problem evaluator_;
    double uniformity_parameter_ = 0.0;
};

[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config = {}
);
[[nodiscard]] std::string penalty_mode_name(PenaltyMode mode);
[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::t64
