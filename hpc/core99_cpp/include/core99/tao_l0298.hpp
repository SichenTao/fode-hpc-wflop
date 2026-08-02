/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0298 offshore micrositing/cabling problem and
NSGA-III--BPSO--QP API
Paper/DOI: Joint Optimization of Wind Turbine Micrositing and Cabling in an
Offshore Wind Farm; 10.1109/TSG.2020.3022378. Target PDF SHA-256:
99057161f4efdb2cf0c8a72b82890bba6c7a4ff67fc3c2d71509129e5b8609e6.
Paper-provided method: outer three-objective NSGA-III; inner radial cable
design by BPSO; inner 24-hour conventional-generator dispatch by quadratic
programming. Table V fixes NSGA-III pc=0.5, pm=0.5, mu=0.02,
population=120 and maximum iterations=250, and BPSO omega=0.7298,
c1=c2=1.4961, population=100 and maximum iterations=250.
Paper-provided problem: 6 km by 6 km offshore site, 12 by 12 candidate grid,
central offshore substation, 180--240 MW capacity, three 3 MW turbine types,
five 33 kV cable types, Greater-Gabbard winter/summer profiles, IEEE RTS-24
load/generation system, buses 3/5/7/16/21/23, Models 1--3, and 29 reported
paper roles in Tables VI--IX and Figs. 7--9.
Public source designated by the paper: Yarpiz YPEA126 NSGA-III,
https://github.com/smkalami/ypea126-nsga3, BSD-2-Clause, commit
a6e206086cdf1e29c0ae29c2699bef85df728181. The paper cites Yarpiz for BPSO,
but exact-site, repository and GitHub searches on 2026-08-01 found no
paper-matching Yarpiz BPSO package or target project. The public continuous
YPEA102 PSO is not represented as the missing binary cable code.
Cited/public problem data: IEEE RTS-24 is Ref. 39. The auditable MATPOWER
case24_ieee_rts transcription is pinned at commit
5f1b70611a573f5455de7a2e5786aed12adfbaf8 and used only to complete the
published generator/load table lineage; it is not target author source.
Missing assets: target MATLAB project, exact BPSO code/encoding, raw
2005--2012 Greater-Gabbard wind records, machine-readable Figs. 3--5,
author RTS modifications, emissions coefficients, price-correlation data,
cable installation/loss constants, random states, raw fronts/layouts and
repeat count.
Paper conflicts: Eq. 37 prints sqrt(10*rotor_radius) separation, while the
case study explicitly sets 500 m, the grid-cell length; production uses the
experiment's 500 m. Model 2 fixes 210 MW (70 three-MW turbines), while Fig. 7
labels its best-profit layout Nwt=75; production follows the mathematical
capacity statement and Table VI. Eqs. 37 and 40 suggest AC network and dense
directed cable variables, while Eqs. 42--43 and the stated quadprog solve
retain only active-power balance, reserve and generator bounds; production
implements that reproducible convex dispatch and does not claim AC-OPF.
Reconstruction: digitized deterministic 24-hour winter/summer wind and load
profiles; paper Gaussian centreline wake with declared thrust/entrainment
completion; stable real-key 12x12 grid decoding; YPEA126 blend crossover,
Gaussian mutation and 14-division/120-direction NSGA-III selection; binary
PSO over a compact parent-edge bit encoding whose decoder guarantees a
radial tree and chooses the least daily-cost feasible cable type; analytic
convex RTS-24 economic dispatch; all absent economic constants are versioned
in the controlling contract rather than calibrated to the paper results.
Method semantic ID: l0298_yarpiz_nsga3_bpso_qp_declared_v1.
Problem semantic ID: l0298_offshore_grid_cable_rts24_declared_v1.
Protocol semantic ID: l0298_models_turbines_seasons_buses_29roles_v1.
Production backend: pure C++20 CPU-HPC. One persistent full-core executor
parallelizes complete outer candidates, each owning a serial inner BPSO and
24-hour evaluator to avoid nested oversubscription. Dominance rows and
reference association use the same persistent team. Counter-keyed random
events, fixed-index writes and ordered survivor commits preserve one/all-core
scientific identity.
Controlling contract: shared/contracts/core99_l0298_tao_2020.json.
Claim boundary: source-backed flexible academic reconstruction of the target
joint problem, target NSGA-III/BPSO/QP method and every native paper role;
not author source, original wind/grid/economic arrays, MATLAB trajectory or
numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0298 {

enum class ProfileId {
    model_comparison,
    turbine_e115,
    turbine_ltw101,
    summer,
    bus5,
    bus7,
    bus16,
    bus21,
    bus23,
};

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct CableEdge {
    int from_turbine = 0;
    int to_node = 0;
    int cable_type = 0;
    double flow_mw = 0.0;
    double length_m = 0.0;
};

struct Evaluation {
    bool feasible = false;
    int turbine_count = 0;
    double installed_capacity_mw = 0.0;
    double profit_rate_percent = 0.0;
    double capacity_factor_percent = 0.0;
    double variability_percent = 0.0;
    double cable_daily_cost_eur = 0.0;
    double cable_length_m = 0.0;
    double grid_benefit_eur = 0.0;
    double wind_daily_energy_mwh = 0.0;
    double constraint_violation = 0.0;
};

struct RoleResult {
    std::string role;
    std::string model;
    Evaluation evaluation;
    std::vector<int> active_cells;
    std::vector<CableEdge> cable_edges;
};

struct RunConfig {
    std::uint64_t seed = 29801;
    int workers = 20;
    int outer_population = 120;
    int outer_iterations = 250;
    int inner_population = 100;
    int inner_iterations = 250;
};

struct RunResult {
    std::string profile_id;
    std::string method_semantic_id =
        "l0298_yarpiz_nsga3_bpso_qp_declared_v1";
    std::string problem_semantic_id =
        "l0298_offshore_grid_cable_rts24_declared_v1";
    std::string protocol_semantic_id =
        "l0298_models_turbines_seasons_buses_29roles_v1";
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t parallel_regions = 0;
    int outer_population = 0;
    int outer_iterations = 0;
    int inner_population = 0;
    int inner_iterations = 0;
    std::uint64_t complete_outer_evaluations = 0;
    std::uint64_t cable_particle_evaluations = 0;
    std::uint64_t hourly_wake_evaluations = 0;
    double wake_and_coupled_evaluator_seconds = 0.0;
    double evolutionary_orchestration_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
    std::vector<RoleResult> roles;
};

class Problem {
public:
    struct Impl;

    explicit Problem(ProfileId profile);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] ProfileId profile() const noexcept;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int expected_role_count() const noexcept;
    [[nodiscard]] Evaluation evaluate_cells(
        const std::vector<int>& active_cells,
        const std::string& model,
        std::uint64_t cable_seed,
        int inner_population,
        int inner_iterations,
        std::vector<CableEdge>* cable_edges = nullptr,
        std::uint64_t* cable_evaluations = nullptr
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);
[[nodiscard]] std::vector<ProfileId> paper_profiles();
[[nodiscard]] std::string to_string(ProfileId value);

}  // namespace core99::l0298
