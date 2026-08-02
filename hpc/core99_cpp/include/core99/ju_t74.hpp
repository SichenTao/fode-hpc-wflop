/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T74 SIGA paper problem and algorithm
Paper: Xinglong Ju and Feng Liu, Wind Farm Layout Optimization Using
Self-Informed Genetic Algorithm with Information Guided Exploitation,
Applied Energy 248 (2019) 429-445, DOI 10.1016/j.apenergy.2019.04.084.
Primary PDF SHA-256:
62f93d5942ddbc2fd7e94eebf52544de9da3147b55729fd643034362f5004c6f.
Public source: https://github.com/JuXinglong/WFLOP_Python,
MIT, pinned commit b1fc0d602928ee3f1fed5f8dc0de0a4a37a06bff.
Source SHA-256: WindFarmGeneticToolbox.py
f29bf5ebc19f543a1f612ed84fe57cb35b87b4b38013d036cf1d93c87f224277;
MARS.py d642c36983a8b70a87c6cc3e0ce49e21f09cf9ec35459a6d329c3df008bf912e;
main.py e542126465681b71f0a81a058a73300c453fbc5449c25d2c0ca4dac6f09c468f.
Paper/source-provided facts: 21x21 grid, 60 GE-1.5sle turbines, cell widths
231/308/385 m, five explicit wind distributions, Jensen overlap wake and
source power curve, 10,000 Monte-Carlo layouts, MARS truncated-linear
surface, population 100, 200 iterations, 30 repeats, elite/crossover/random/
mutation rates 0.2/0.6/0.5/0.1, worst-turbine relocation and five MARS
candidates.
Conflict: the paper treats relocation/selection/mutation settings as
probabilities, while the public Python source compares standard-normal
draws against 0.5/0.1 and repeatedly reseeds from wall time. Both identities
are executable: paper_probability is primary; source_normal_threshold is
an explicit source-behavior replay without MT19937/time-bitstream identity.
Missing: author initial populations, Monte-Carlo layouts, trained MARS
objects and exact random states were not published. Reconstruction:
counter-keyed random events regenerate these artifacts; the MARS stage is a
clean-room truncated-linear forward-selection regression using paper grid
knots, maximum 100 bases, maximum interaction order four and 1e-3 stopping.
Method semantic IDs: t74_siga_paper_probability_v1;
t74_siga_source_normal_threshold_v1.
Problem semantic IDs: t74_grid60_case1_case5_small_medium_large_v1.
Protocol semantic ID: t74_15case_30repeat_10000mc_pop100_gen200_v1.
Production backend: pure C++ CPU with one persistent full-core executor;
Monte-Carlo truth evaluation, population truth evaluation, relocation,
crossover and mutation are parallelized without nested oversubscription.
Claim boundary: academic paper/source reconstruction, not author numerical
or random-bitstream replay.
Contract: shared/contracts/core99_t74_ju_siga_2019.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t74 {

using Layout = std::vector<int>;

struct Evaluation {
    double expected_power_kw = 0.0;
    double efficiency_percent = 0.0;
    std::vector<double> turbine_power_kw;
    bool feasible = false;
};

struct RunConfig {
    std::uint64_t seed = 2026074000ULL;
    int workers = 20;
    int monte_carlo_layouts = -1;
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
    explicit Problem(std::string case_id);

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] int wind_case() const noexcept;
    [[nodiscard]] double cell_width_m() const noexcept;
    [[nodiscard]] int wind_state_count() const noexcept;
    [[nodiscard]] int paper_population() const noexcept;
    [[nodiscard]] int paper_generations() const noexcept;
    [[nodiscard]] int paper_monte_carlo_layouts() const noexcept;
    [[nodiscard]] Evaluation evaluate(const Layout& layout) const;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    std::string case_id_;
    std::string semantic_id_;
    int wind_case_ = 1;
    double cell_width_m_ = 231.0;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();
[[nodiscard]] Layout regular_reference_layout();

}  // namespace core99::t74
