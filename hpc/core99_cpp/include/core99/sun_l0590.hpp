/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0590 3-D ANN wake, layout/hub-height GA and paper cases
Paper/DOI: Sun and Yang, Wind farm layout and hub height optimization with
a novel wake model; 10.1016/j.apenergy.2023.121554. Primary PDF SHA-256:
db8eb9eaf64ba9fe6e3fb9934e7e2536ae57f08294185edeca3d10beff4cebe5.
Paper-provided facts: analytical 3-D Gaussian wake Eqs. (1)-(3), logarithmic
inflow Eq. (4), a 3-5-6-1 sigmoid ANN, 70/15/15 data partition, maximum
1000 epochs, MSE target 1e-6, D=77 m, hub-height reference 65 m, 12.7 m/s
at hub, 1513 kW turbine, 30 turbines, north wind, 2 km square, minimum
spacing 5D, height bounds [45,85] m and named cases E1-E4/C1-C4.
Cited model sources recovered legally through Elsevier API:
  * 10.1016/j.apenergy.2018.06.027, PDF SHA-256
    e83ed8170d94375cd82e509aa4cc2868c47fe7c868de639bed89b6422bf2d978;
  * 10.1016/j.renene.2019.08.122, PDF SHA-256
    e24faf6782fb69660f75b835fd85a2beb5b7c61aa0ceb30b5e6a80a7600a1b8c.
They specify the original model's flux conservation, power-law alternative,
rw=r0+k_wake*x, v0=(1-2a)u0, turbulence-dependent wake expansion and
multiple-wake RSS.
Public code/data: no paper-linked code, Shiren array, trained weights or
machine-readable NREL cost curve was located.
Missing/conflicts: target paper omits Shiren calibration constants, data
ranges/count, exact rotor integration, GA population/operators/seeds and
repeat count. It calls cost/power both total-power/total-cost and USD/kW;
USD/kW and minimization establish cost/power as the operative definition.
The cited cost curve begins at 60 m although the optimization permits 45 m.
Reconstruction: C=5.15 and onshore k0=0.075 are source-supported operating
completions; a deterministic 0.01D-lattice stratified teacher set replaces
the private array. Below-60-m cost is explicitly linearly extrapolated.
The real-coded GA uses population 64, paper-reported 838 energy and 1017
cost generations, binary tournament, arithmetic/uniform crossover,
bounded mutation, deterministic spacing repair and elitist survival.
Paper cases C1/C3 are registered aliases of E1/E3 as stated by the paper.
Problem semantic ID: l0590_shiren_3d_ann_layout_height_v1.
Method semantic ID: l0590_real_ga_completed_v1.
Training semantic ID: l0590_mlp_3_5_6_1_from_scratch_v1.
Production backend: pure C++ CPU. Teacher generation, full-batch gradient
chunks and population evaluation use the persistent full-core executor.
Counter-keyed random events make one/all-core scientific trajectories
identical. Formal scheduling may use independent case/seed parallelism
when aggregate throughput is greater.
Controlling contract:
shared/contracts/core99_l0590_sun_ann_height_2023.json.
Claim boundary: academic declared reproduction of every named paper case
and the paper/cited equations; not author code, private Shiren data,
author-trained weights, exact GA, exact cost curve or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0590 {

struct Turbine {
    double x_m = 0.0;
    double y_m = 0.0;
    double hub_height_m = 65.0;
};

struct Evaluation {
    std::vector<double> turbine_speed_mps;
    std::vector<double> turbine_power_kw;
    double total_power_kw = 0.0;
    double total_cost_usd = 0.0;
    double cost_of_power_usd_per_kw = 0.0;
    double objective = 0.0;
    double minimum_spacing_m = 0.0;
    bool feasible = false;
};

struct TrainingConfig {
    std::uint64_t seed = 2026059001;
    int workers = 20;
    int maximum_epochs = 1000;
    int sample_count = 32768;
    double target_mse = 1.0e-6;
};

struct TrainingResult {
    int epochs = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int train_count = 0;
    int validation_count = 0;
    int test_count = 0;
    double train_mse = 0.0;
    double validation_mse = 0.0;
    double test_mse = 0.0;
    double seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

struct RunConfig {
    std::uint64_t seed = 2026059000;
    int workers = 20;
    int generations = -1;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::string training_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t physical_fes = 0;
    int generations = 0;
    Evaluation initial_best;
    Evaluation best_evaluation;
    std::vector<Turbine> best_layout;
    std::vector<double> best_objective_history;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class WakeSurrogate {
public:
    WakeSurrogate();
    ~WakeSurrogate();
    WakeSurrogate(WakeSurrogate&&) noexcept;
    WakeSurrogate& operator=(WakeSurrogate&&) noexcept;
    WakeSurrogate(const WakeSurrogate&) = delete;
    WakeSurrogate& operator=(const WakeSurrogate&) = delete;

    [[nodiscard]] TrainingResult train(const TrainingConfig& config);
    [[nodiscard]] double teacher_deficit_ratio(
        double downstream_m,
        double crosswind_m,
        double vertical_offset_m
    ) const;
    [[nodiscard]] double predict_deficit_ratio(
        double downstream_m,
        double crosswind_m,
        double vertical_offset_m
    ) const;
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Problem {
public:
    explicit Problem(std::string case_id, const WakeSurrogate& surrogate);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] bool optimizes_layout() const noexcept;
    [[nodiscard]] bool optimizes_height() const noexcept;
    [[nodiscard]] bool minimizes_cost() const noexcept;
    [[nodiscard]] int paper_generation_limit() const noexcept;
    [[nodiscard]] Evaluation evaluate(
        const std::vector<Turbine>& layout
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] std::vector<Turbine> aligned_layout();

}  // namespace core99::l0590
