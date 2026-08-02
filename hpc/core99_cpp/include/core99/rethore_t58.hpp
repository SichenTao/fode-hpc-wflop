/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T58 TOPFARM multi-fidelity problem and optimizer package
Paper/DOI: Rethore et al., TOPFARM: Multi-Fidelity Optimization of Wind
Farms; 10.1002/we.1667.
Primary paper: local 20-page PDF SHA-256
779feb21b1943d300e5752e7613b5fda60eb679df00cceb1bf6b6b2940628065.
Cited primary report: Rethore et al., Risoe-R-1768(EN), 2011, official DTU
Orbit URL https://backend.orbit.dtu.dk/ws/portalfiles/portal/51814036/ris_r_1768.pdf,
retrieved SHA-256 c0ef46d784f6874a6596647b13ebc194b377c9a29a3e5ad0a01eda885ca6ab0f.
It supplies population 20, crossover 0.70, bit mutation 0.025, two elites,
fitness scaling 2, approximate 1000-cell SGA mapping, SLP move-limit ranges,
the Stags Holt/Coldham and Middelgrunden polygons, turbine data, wind-sector
Weibull parameters, direction probabilities, ambient turbulence and native
iteration counts. The paper supplies all objective equations, three target
cases, linear wake-deficit superposition, rotor Gauss integration, 1D minimum
spacing, cable clustering, cost tables and reported result anchors.
Same-lineage sources: Larsen, Risoe-R-1713(EN), official DTU Orbit URL
https://backend.orbit.dtu.dk/ws/portalfiles/portal/122941920/Simple_analytical_wake_model_final_10.pdf,
for the stationary semi-analytical wake equations; current TopFarm2 at
https://gitlab.windenergy.dtu.dk/TOPFARM/TopFarm2 is a post-2018 lineage
reference and is not represented as the 2014 HAWTOPT/MATLAB source.
Public source provenance: the two official DTU reports and the later TopFarm2
lineage are evidence oracles only; no executable author implementation is
redistributed or represented as the target 2014 source.
Missing assets: HAWTOPT source and solver state; S2MW curve CSVs; the 7500
HAWC2-DWM simulations and six-seed time series; exact 4D load database;
Middelgrunden/Stags bathymetry and baseline coordinate arrays; the fictitious
case numeric arrays; exact random state; and complete convergence histories.
Reconstruction: the two real polygons and wind tables are copied from the
cited report; Middelgrunden baseline coordinates use the public OpenWAKE
lineage already pinned by L0079; Stags coordinates and the fictitious case are
declared figure digitizations; missing power/CT curves use disclosed smooth
curves; the Larsen wake is implemented from Risoe-R-1713 with paper-required
linear superposition and rotor quadrature; the missing DWM load table is
replaced by a deterministic nearest-wake analytic load surrogate using the
paper component costs and Wohler exponents. Baseline wake scale is calibrated
once to the report's 83.9% Middelgrunden and 89.4% Stags energy efficiencies;
no optimized result is calibration input. HAWTOPT SLP is reconstructed as a
deterministic move-limited linearized refinement with paper iteration counts.
The full binary SGA lifecycle follows the report parameters.
Problem semantic ID: t58_topfarm_three_case_financial_declared_v1.
Method semantic IDs: t58_slp_declared_v1, t58_sga_declared_v1,
t58_sga_slp_multifidelity_declared_v1.
Protocol semantic ID: t58_native_five_role_single_run_v1.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json.
Production backend: pure C++20 CPU-HPC. Wind states, SGA populations and SLP
finite-difference trial points use one persistent all-core team. Fixed slots,
fixed-order reductions, frozen generations and counter-keyed random events
make one/all-core scientific results identical.
Claim boundary: source-backed flexible academic reproduction of the paper's
three problems, financial objective and complete SGA-to-SLP method lifecycle;
not author HAWTOPT, HAWC2-DWM database, site arrays, random stream or exact
numeric trajectory.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t58 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
    bool operator==(const Point&) const = default;
};

enum class CaseId { fictitious_2x3 = 1, stags_holt_coldham = 2,
                    middelgrunden = 3 };
enum class Method { slp_only, sga_only, sga_slp };
enum class Fidelity { level1_coarse, level2_fine };

struct EvaluationSettings {
    Fidelity fidelity = Fidelity::level2_fine;
    double fatigue_scale = 1.0;
    double cable_scale = 1.0;
};

struct Evaluation {
    double gross_aep_mwh_per_year = 0.0;
    double net_aep_mwh_per_year = 0.0;
    double energy_efficiency_percent = 0.0;
    double power_value_meur = 0.0;
    double foundation_cost_meur = 0.0;
    double cable_cost_meur = 0.0;
    double degradation_cost_meur = 0.0;
    double maintenance_cost_meur = 0.0;
    double financial_balance_meur = 0.0;
    double cable_length_m = 0.0;
    double minimum_spacing_m = 0.0;
    double maximum_constraint_violation_m = 0.0;
    int requested_workers = 0;
    int observed_workers = 0;
    bool feasible = false;
    double seconds = 0.0;
};

struct StageReceipt {
    std::string stage;
    int iterations = 0;
    std::uint64_t physical_fes = 0;
    double start_balance_meur = 0.0;
    double end_balance_meur = 0.0;
    double evaluator_seconds = 0.0;
    double seconds = 0.0;
};

struct RunConfig {
    Method method = Method::sga_slp;
    int workers = 20;
    std::uint64_t seed = 58001;
    bool smoke = false;
    int sga_generations_override = 0;
    int slp_iterations_override = 0;
};

struct RunResult {
    std::string problem_semantic_id =
        "t58_topfarm_three_case_financial_declared_v1";
    std::string method_semantic_id;
    std::string protocol_semantic_id = "t58_native_five_role_single_run_v1";
    CaseId case_id = CaseId::fictitious_2x3;
    Method method = Method::sga_slp;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int sga_population = 0;
    int sga_generations = 0;
    int slp_iterations = 0;
    std::uint64_t physical_fes = 0;
    Evaluation initial_evaluation;
    Evaluation final_evaluation;
    std::vector<StageReceipt> stages;
    std::vector<Point> final_layout;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct Data;

    explicit Problem(CaseId case_id);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] CaseId case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int fine_wind_state_count() const noexcept;
    [[nodiscard]] int candidate_count() const noexcept;
    [[nodiscard]] double rotor_diameter_m() const noexcept;
    [[nodiscard]] const std::vector<Point>& baseline_layout() const noexcept;
    [[nodiscard]] const std::vector<Point>& polygon() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const;

private:
    std::unique_ptr<Data> data_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] const char* case_name(CaseId value) noexcept;
[[nodiscard]] const char* method_name(Method value) noexcept;
[[nodiscard]] const char* fidelity_name(Fidelity value) noexcept;
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::t58
