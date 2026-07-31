/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0259 SUGGA algorithm and landowner-participation WFLOPs
Paper/DOI: Wind farm layout optimization based on support vector regression
guided genetic algorithm with consideration of participation among
landowners; 10.1016/j.enconman.2019.06.082.
Primary PDF SHA-256:
5b6605a967143073a4da442c6aa50c873562e6c0f5037b822f38a1862a68782f.
Public source: https://github.com/JuXinglong/WFLOP_SUGGA_Python,
MIT, pinned commit 27ae25a40be7fe2258894a741013aa46e9d1e4b3.
Consumed source SHA-256: main.py
7dfa2014199ac6622d73d59c100f16095efb2dc27716b9a71eff165a72a0e4c6;
WindFarmGenetic.py
cb79c40ef0c5071a8600508e793be6190a855be01c31e60d0f1c97d943438d11.
SVR backend: official LIBSVM C++ revision
6b907139084abf2da4d6d3cb10dc3b7eaffa2fbb, BSD-3-Clause; this is the
native backend used by the paper source's sklearn.svm.SVR.
Paper/source facts: 12x12 grid, 13 land masks L0-L12, D1/D2/D3 wind
profiles at 13 m/s, N=15/20/25 GE-1.5sle turbines, 10,000 Monte-Carlo
layouts per problem, RBF epsilon-SVR (C=2000, gamma=0.3, epsilon=0.1),
five relocation candidates, 200 generations and 100 independent repeats.
Conflicts: the paper specifies 154 m cells and 88 m hub height, whereas
the source main/class use 231 m and 80 m. The paper treats selection,
relocation and mutation rates as probabilities, whereas the source uses
standard-normal thresholds. The source ignores its declared 0.6 crossover
rate and repeatedly reseeds from wall time. The paper-primary identity uses
154 m, 88 m and uniform probability events; source_normal_threshold uses
231 m, 80 m and normal-threshold events. Both retain the otherwise complete
public-source lifecycle, including all-offspring crossover.
Missing facts: author initial populations, Monte-Carlo layouts, trained SVR
objects and random states were not published. Reconstruction completion:
counter-keyed random events regenerate them; one LIBSVM epsilon-SVR is
trained from scratch per paper problem and reused across its 100 repeats.
Method semantic IDs: l0259_sugga_paper_probability_v1;
l0259_sugga_source_normal_threshold_v1.
Problem semantic ID: l0259_landowner12x12_d1d3_n15n25_v1.
Protocol semantic ID: l0259_117case_100repeat_10000mc_pop120_gen200_v1.
Production backend: pure C++20 CPU-HPC. Direction/node wake interactions
are precomputed; Monte-Carlo truth layouts, population truth evaluations,
relocation, crossover and mutation run on one persistent full-core team.
Claim boundary: academic paper/source flexible reproduction, not author
numerical, trained-model or random-bitstream replay.
Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::l0259 {

using Layout = std::vector<int>;

struct Evaluation {
    double expected_power_kw = 0.0;
    double efficiency_percent = 0.0;
    std::vector<double> turbine_power_kw;
    bool feasible = false;
};

struct SurrogateSnapshot {
    std::vector<double> training_targets_kw;
    std::vector<double> predictions_kw;
    int observed_workers = 0;
    double monte_carlo_truth_seconds = 0.0;
    double training_seconds = 0.0;
};

struct RunConfig {
    std::uint64_t seed = 2026075900ULL;
    int workers = 20;
    int monte_carlo_layouts = -1;
    int population = -1;
    int generations = -1;
    std::string variant = "paper_probability";
    bool reuse_surrogate = false;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int monte_carlo_layouts = 0;
    int population = 0;
    int generations = 0;
    bool surrogate_reused = false;
    std::uint64_t physical_fes = 0;
    Evaluation initial_best;
    Evaluation best_evaluation;
    Layout best_layout;
    std::vector<double> best_efficiency_history_percent;
    double monte_carlo_truth_seconds = 0.0;
    double surrogate_training_seconds = 0.0;
    double population_truth_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    struct State;

    explicit Problem(std::string case_id, std::string variant = "paper_probability");
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int wind_profile() const noexcept;
    [[nodiscard]] int landscape() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int available_count() const noexcept;
    [[nodiscard]] double cell_width_m() const noexcept;
    [[nodiscard]] double hub_height_m() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] int paper_population() const noexcept;
    [[nodiscard]] int paper_generations() const noexcept;
    [[nodiscard]] int paper_monte_carlo_layouts() const noexcept;
    [[nodiscard]] int paper_repeats() const noexcept;
    [[nodiscard]] Evaluation evaluate(const Layout& layout) const;
    [[nodiscard]] SurrogateSnapshot train_surrogate(
        int monte_carlo_layouts,
        std::uint64_t seed,
        int workers
    ) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    std::unique_ptr<State> state_;
    std::string case_id_;
    std::string semantic_id_;
    std::string variant_;
    int wind_profile_ = 1;
    int landscape_ = 0;
    int turbine_count_ = 15;
    double cell_width_m_ = 154.0;
    double hub_height_m_ = 88.0;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] Layout regular_reference_layout(
    const Problem& problem
);

}  // namespace core99::l0259
