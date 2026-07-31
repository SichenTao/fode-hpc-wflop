/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T18 WindFLO framework, SOHO relay optimizer and AWEC
terrain-layout/turbine co-design API
Paper/DOI: Sohail R. Reddy, Wind Farm Layout Optimization (WindFLO): An
Advanced Framework for Fast Wind Farm Analysis and Optimization;
10.1016/j.apenergy.2020.115090. Target PDF SHA-256:
466c24c441328fe6e5c41ef42b7c883997adc61fa755ab11f905b4f3e536384a.
Paper-provided method: WindFLO with six analytical wake models, four wake
superposition schemes, terrain-following wakes, rotor-height-dependent cost,
convex-hull land area and master-slave optimization; SOHO relays among the
single-objective forms of NSGA-III, NSDE-R and MOEA-DD after stagnation.
Paper-provided problem: 25 Vestas V90-3 MW turbines on a 2 km by 2 km scaled
Alta Wind Energy Center terrain; the 2019 wind rose; Frandsen and BP wake
models with quadratic superposition; Case 1 optimizes 50 x-y variables and
Case 2 optimizes 100 x-y-diameter-height variables; maximize AEP subject to
pair clearance and tower/rotor feasibility; Tables 2--4 are native roles.
Public source: https://github.com/sohailreddy/WindFLO, repository revision
97dd43784bffb1c0c8a4388d8e7929b337d496a5 (current canonical owner
https://github.com/sohailreddy/WindFLO), Apache-2.0 LICENSE SHA-256
a845ffd6cd92809f188af7b175b5d3b3417f40d3e9cffdba4de5615195c0c804.
The public repository supplies the WindFLO C++/Fortran implementation, the
v1.0.0 Cal et al. validation inputs, the 489-point AWEC terrain, V90 Cp data,
PSO/GA examples and example outputs. It does not supply the target SOHO source,
the numeric 2019 cli-MATE wind-rose table, target optimization states, budget,
stagnation setting or the four optimized layouts from Figure 11.
SOHO supplement: Reddy 2019 FIU dissertation, DOI
10.25148/etd.FIDC008890, archived official PDF SHA-256
9bd7ee57e124aa0ad6ff49953b94318ad840e7be92179c2df51aaab3913241b1,
states the constitutive operators and parameters. The target paper narrows
SOHO to NSGA-III, NSDE-R and MOEA-DD and randomly switches to one of the other
two algorithms at stagnation.
Source/paper conflicts and modeling corrections: the paper uses local RBF
terrain interpolation while public Example 4 overrides it with IDW; both are
executable and paper-local-RBF is primary. Public disk sampling uses r=R*u,
which is not uniform in disk area; production uses r=R*sqrt(u), while a source-
radius profile is retained. Public DWM accumulation repeatedly multiplies the
previous deficit by overlap; production uses the intended maximum weighted
deficit and records the source expression. The paper prints 26,280 MWh as the
all-turbine normalization although 25 times 3 MW times 8760 h is 657,000 MWh;
production uses the dimensionally correct farm value, consistent with Table 4.
Tables 2--3 use the exact public two-dimensional Sobol sequence with 1000
uniform-radius samples; the six optimization roles use 64 corrected equal-area
samples. This keeps source validation and corrected production semantics
separate in one receipt.
Declared completions: a versioned digitization of Figure 10(c) supplies the
missing 16-direction by seven-speed-bin wind quadrature; a local-neighbor
multiquadric RBF grid supplies the paper profile; population 100, 200
generations and a 20-generation stagnation window instantiate the missing
SOHO budget. These values are not calibrated to Table 4.
Method semantic ID: t18_soho_three_kernel_relay_declared_v1.
Problem semantic ID: t18_windflo_awec25_two_case_two_wake_v1.
Protocol semantic ID: t18_tables2_3_4_54roles_25seed_v1.
Production backend: pure C++20 CPU-HPC. One persistent all-core executor
parallelizes complete offspring generation/evaluation, the independent
validation matrix and fixed-index source/correct-area quadrature. Immutable
terrain/scenario tables, counter-keyed random events, fixed-index writes and
ordered commits preserve one/all-core scientific identity.
Controlling contract: shared/contracts/core99_t18_reddy_2020.json.
Claim boundary: source-backed flexible academic reconstruction of the target
framework, target SOHO relay, the two target optimization cases and all native
Tables 2--4 roles; not the unavailable author SOHO source, cli-MATE table,
random states, layouts or a numerical replay of Table 4. The paper already
declares master-slave parallel optimization and the public PSO exposes an
external parallel-evaluation hook; this implementation does not claim first
parallelization. It supplies a fully auditable persistent-team realization,
deterministic one/all-core identity, physical-work receipts and measured
scaling for the reconstructed SOHO/WindFLO protocol.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t18 {

enum class WakeModel { jensen, frandsen, larsen, ishihara, bp, xa };
enum class MergeScheme { linear, quadratic, energy, dwm };
enum class TerrainProfile { paper_local_rbf, source_example_idw };
enum class DiskSampling { paper_area_correct, source_uniform_radius };
enum class DesignCase { case1_layout, case2_layout_turbine };

struct Turbine {
    double x_m = 0.0;
    double y_m = 0.0;
    double diameter_m = 90.0;
    double height_m = 105.0;
};

struct Evaluation {
    bool feasible = false;
    double constraint_violation = 0.0;
    double annual_energy_mwh = 0.0;
    double normalized_aep = 0.0;
    double farm_power_mw = 0.0;
    double farm_efficiency = 0.0;
    double land_used_km2 = 0.0;
    double farm_cost_usd = 0.0;
    double coe_usd_kwh = 0.0;
};

struct ValidationRecord {
    std::string role;
    WakeModel wake = WakeModel::jensen;
    MergeScheme merge = MergeScheme::linear;
    double predicted_velocity_mps = 0.0;
    double experimental_velocity_mps = 0.0;
    double relative_error_percent = 0.0;
};

struct RoleResult {
    std::string role;
    WakeModel wake = WakeModel::frandsen;
    DesignCase design_case = DesignCase::case1_layout;
    bool reference = false;
    Evaluation evaluation;
    std::vector<Turbine> layout;
    int starting_kernel = 0;
    std::vector<int> kernel_sequence;
    std::vector<int> switch_generations;
};

struct RunConfig {
    std::uint64_t seed = 18001;
    int workers = 20;
    int population = 100;
    int generations = 200;
    int stagnation_generations = 20;
    int disk_quadrature_points = 64;
    int validation_disk_quadrature_points = 1000;
    TerrainProfile terrain_profile = TerrainProfile::paper_local_rbf;
    DiskSampling disk_sampling = DiskSampling::paper_area_correct;
    DiskSampling validation_disk_sampling = DiskSampling::source_uniform_radius;
};

struct RunResult {
    std::string case_id = "awec25";
    std::string method_semantic_id =
        "t18_soho_three_kernel_relay_declared_v1";
    std::string problem_semantic_id =
        "t18_windflo_awec25_two_case_two_wake_v1";
    std::string protocol_semantic_id =
        "t18_tables2_3_4_54roles_25seed_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int population = 0;
    int generations = 0;
    int stagnation_generations = 0;
    int disk_quadrature_points = 0;
    int validation_disk_quadrature_points = 0;
    std::uint64_t objective_evaluations = 0;
    std::uint64_t wind_scenario_layout_evaluations = 0;
    std::uint64_t wake_pair_checks = 0;
    std::uint64_t disk_quadrature_samples = 0;
    double terrain_precompute_seconds = 0.0;
    double validation_seconds = 0.0;
    // Sum of complete-candidate evaluator wall times; overlapping parallel
    // evaluations make this a work receipt, not an evaluator wall clock.
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    TerrainProfile terrain_profile = TerrainProfile::paper_local_rbf;
    DiskSampling disk_sampling = DiskSampling::paper_area_correct;
    DiskSampling validation_disk_sampling = DiskSampling::source_uniform_radius;
    std::vector<ValidationRecord> validation;
    std::vector<RoleResult> roles;
};

class Problem {
public:
    struct Impl;

    Problem();
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] Evaluation evaluate(
        const std::vector<Turbine>& layout,
        WakeModel wake,
        TerrainProfile terrain_profile = TerrainProfile::paper_local_rbf,
        DiskSampling disk_sampling = DiskSampling::paper_area_correct,
        int disk_quadrature_points = 64
    ) const;
    [[nodiscard]] std::vector<Turbine> reference_layout() const;
    [[nodiscard]] std::vector<ValidationRecord> validate_wind_tunnel(
        DiskSampling disk_sampling = DiskSampling::paper_area_correct,
        int disk_quadrature_points = 1000
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::string to_string(WakeModel value);
[[nodiscard]] std::string to_string(MergeScheme value);
[[nodiscard]] std::string to_string(TerrainProfile value);
[[nodiscard]] std::string to_string(DiskSampling value);
[[nodiscard]] std::string to_string(DesignCase value);

}  // namespace core99::t18
