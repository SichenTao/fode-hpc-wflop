/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T16 Nantucket 38-turbine BP/NP evaluator and
gradient-based wake-expansion-continuation layout optimizer
Paper title/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Primary paper assets: Eqs. (1)-(21), Algorithm 1, Tables 1-2, 38 NREL 5-MW
turbines, 12-direction Nantucket rose at 8 m/s, 2-km circular physical farm,
2D optimization spacing, 5D concentric baseline, one baseline plus 199
pseudo-random starts, BP-2016 near-wake interpolation, NP linear upstream
velocity superposition, k*=0.3837I+0.003678, smooth maximum s=700, one rotor
sample during optimization and 100 for assessment, WEC factors 3.00 to 1.00
in 0.25 steps, final smooth-local-TI optimization, and final hard-TI
assessment. SOWFA Table 2 reports the paper's high-fidelity validation.
Public source: the 2019 paper links no source archive. Later same-author
lineage repository https://github.com/byuflowlab/thomas2021-wec at commit
8ff27d66079591f25619abeedbfc970d70e2b520 contains matching Nantucket,
NREL-5MW, concentric-layout, multistart, OpenMDAO/SNOPT and WEC assets but no
explicit repository license. FLOWFarm.jl commit
ec5270203786d5bcf065fff8c80bd7710906a40e (MIT) independently confirms the
same BP/NP, local-TI, smooth-max and 38-turbine WEC semantics.
Open replacement dependencies: NLopt v2.11.0 commit
88c424d4f458412787df96fcc95218acbca224fd (LGPL) supplies C++ SLSQP.
The objective derivative uses a project-native fixed-width forward-mode
automatic-differentiation scalar, avoiding dynamic expression graphs and
computing all 76 exact derivatives in one evaluator traversal.
Missing/conflicts: proprietary SNOPT and Tapenade build, original 2019
source snapshot, exact pseudo-random states, optimized coordinates, SOWFA
case directories/precursor/time histories, and per-start histories are not
published. Eq. (21) prints a 2-km hub-centre radius, while the later public
driver uses 1936.8 m so the 126.4 m rotor remains inside the 2-km physical
circle. The public twelve-bin probabilities sum to 0.962 rather than one.
The public NREL5MWCPCT_dict.txt header swaps the CP and CT labels; the
repository's own readandwritedict.py and source pickle establish the actual
wind-speed, CT, CP order, which the data-preparation step corrects. The paper
says random starts meet the 2D constraint; a later generator uses
1D rejection spacing. We follow the paper: feasible 2D reconstructed starts,
1936.8 m physical-edge-aware hub radius, and the unrenormalized 0.962 array.
The public same-lineage driver applies the NREL 5-MW generator efficiency
0.944; retaining it is independently corroborated by the paper's approximately
481 GWh BP baseline, whereas omitting it overpredicts the reported AEP.
Reconstruction: independent pure-C++ BP/NP evaluator; fixed-order
upstream solve; paper near-wake continuation; WEC-D Gaussian spreading;
fixed-width forward-mode exact objective gradient; analytic sparse spacing/boundary
Jacobians; open SLSQP in place of SNOPT; deterministic feasible multistarts;
Table-2 SOWFA observations retained as validation data, never simulated
surrogate output.
Problem semantic ID: t16_nantucket38_author_lineage_reconstructed_v1
Method semantic ID: t16_wec_slsqp_autodiff_reconstruction_v1
Production backend: pure C++ CPU. A persistent team parallelizes the twelve
wind directions inside each objective/gradient call. The paper's 200
independent starts are scheduled concurrently when outer throughput consumes
Waffle more efficiently; nested oversubscription is forbidden.
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Claim boundary: academic declared reproduction of the published equations,
problem and optimization lifecycle with same-lineage public numeric evidence
and open SQP/AD replacements; not author SNOPT/Tapenade replay, original
random-state replay, or rerun of the unavailable 25-million-cell SOWFA cases
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t16 {

constexpr int turbine_count = 38;
constexpr int wind_state_count = 12;

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

enum class TurbulenceMode {
    ambient_only,
    smooth_local,
    hard_local,
};

struct EvaluationSettings {
    double wec_factor = 1.0;
    TurbulenceMode turbulence_mode = TurbulenceMode::ambient_only;
    int rotor_sample_points = 1;
    bool calculate_gradient = false;
};

struct Evaluation {
    double aep_gwh = 0.0;
    double maximum_constraint_violation_m = 0.0;
    std::array<double, wind_state_count> directional_power_mw{};
    std::vector<double> gradient_gwh_per_m;
    int requested_workers = 0;
    int observed_workers = 0;
    double seconds = 0.0;
};

struct StageReceipt {
    double wec_factor = 1.0;
    TurbulenceMode turbulence_mode = TurbulenceMode::ambient_only;
    int objective_calls = 0;
    int gradient_calls = 0;
    int constraint_calls = 0;
    int optimizer_status = 0;
    std::string optimizer_status_name;
    double start_aep_gwh = 0.0;
    double end_aep_gwh = 0.0;
    double seconds = 0.0;
};

struct RunConfig {
    int workers = 20;
    int start_index = 0;
    std::uint64_t seed = 20260731;
    int maximum_evaluations_per_stage = 220;
    double relative_x_tolerance = 1.0e-6;
    double maximum_stage_seconds = 0.0;
    bool run_full_wec_lifecycle = true;
};

struct RunResult {
    std::string problem_semantic_id;
    std::string method_semantic_id;
    int start_index = 0;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    Evaluation initial_optimization_evaluation;
    Evaluation final_optimization_evaluation;
    Evaluation final_paper_assessment;
    std::vector<StageReceipt> stages;
    std::vector<Point> final_layout;
    double evaluator_seconds = 0.0;
    double optimizer_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(const std::string& data_path);
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] const std::vector<Point>& baseline_layout() const noexcept;
    [[nodiscard]] std::vector<Point> reconstructed_start(
        int start_index,
        std::uint64_t seed
    ) const;
    [[nodiscard]] double maximum_constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Point>& layout,
        const EvaluationSettings& settings,
        fode::PersistentExecutor& executor
    ) const;

private:
    struct Data;
    std::shared_ptr<const Data> data_;
    std::string semantic_id_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] double smooth_max(double left, double right, double smoothing);
[[nodiscard]] RunResult run(const Problem& problem, const RunConfig& config);

}  // namespace core99::t16
