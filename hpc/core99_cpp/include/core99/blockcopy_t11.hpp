/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T11 BlockCopy operators and four paper-native WFLOPs
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Paper asset: the hashed eight-page primary PDF provides both BlockCopy
operators, the (1+1)-ES and (5,10)-ES experiments, four scenario identities,
fixed counts, block grids, economic objective, 2000 physical evaluations and
30 independent runs.
Public source: the paper links no source. The evaluator/scenario lineage is
available at https://github.com/d9w/WindFLO, MIT revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2. It supplies the original C++
Kusiak evaluator and exact 2014 competition_1.xml and competition_3.xml.
Same-author completion source: Chen Zheng's 2016 Waikato thesis,
https://hdl.handle.net/10289/10775, SHA-256
c0d81ff589347a19c8ee7816a9ec0b6829418a767ee7fa04f016898d09431c8e,
describes the implementation and repair lifecycle but provides no code.
Missing facts: author BlockCopy source and random states; exact random
initial-layout sampler; tie acceptance; (5,10)-ES survivor and parent pairing
details; and machine-readable Kusiak scenario files.
Conflicts and completion: paper authority wins where the thesis says an
invalid copied turbine is randomly relocated but the paper says it is simply
not placed. The official 2014 XML k ranges are opposite the ranges printed in
the paper table; scenario identity follows dimensions, turbine count and the
named official XML. The paper caption says 15 direction bins while its model,
thesis and released evaluator use 24 15-degree sectors. The thesis prints
interest r=0.3 while the paper and source use r=0.03. The paper's stated
algorithm target is fixed-count continuous WFLOP with 8 rotor-radius spacing.
Kusiak wind arrays use the audited tables of the cited paper. A conventional
comma-ES lifecycle initializes 5 parents, generates 10 offspring, and keeps
the best 5 offspring; parents are sampled with replacement and crossover
parents are distinct. Equal-cost offspring are accepted/selected stably.
Random starts and count repair use uniform feasible continuous placement.
Target methods: (1+1)-ES BlockCopy mutation; (1+1)-ES equal-probability
BlockCopy/random-10-turbine mutation; (5,10)-ES BlockCopy mutation; and
(5,10)-ES BlockCopy crossover. TDA and perturbation-only paper baselines are
not target contributions and are intentionally not implemented.
Target problems: Kusiak-Song 1 and 2 on 4 km squares with 100 turbines, plus
official 2014 Competition 1 (220 turbines) and 3 (710 turbines).
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
Production backend: pure C++20 CPU-HPC. Full evaluation is parallel over
direction/turbine tasks. BlockCopy evaluation preserves a parent's squared
wake state and updates only removed/added turbines in O(S*N*K), instead of
O(S*N*N), where K is the small changed set. Independent formal runs occupy
all allocated cores without nested oversubscription.
Claim boundary: source-backed flexible academic reproduction, not author
BlockCopy source, author random stream or exact-number replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "fode/executor.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace core99::t11 {

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct Evaluation {
    double energy_cost = 0.0;
    double energy_output_kw = 0.0;
    double wake_free_ratio = 0.0;
    double constraint_violation_m = 0.0;
    bool feasible = false;
};

struct RunConfig {
    std::string algorithm_id = "t11_1plus1_blockcopy_mutation";
    std::uint64_t seed = 20260731;
    std::uint64_t physical_fes = 2000;
    int workers = 20;
};

struct RunResult {
    std::string problem_id;
    std::string algorithm_id;
    std::string problem_semantic_id =
        "t11_kusiak_and_2014_competition_four_cases_v1";
    std::string method_semantic_id =
        "t11_blockcopy_four_es_methods_v1";
    std::uint64_t seed = 0;
    std::uint64_t physical_fes = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    std::uint64_t accepted_offspring = 0;
    double initial_energy_cost = 0.0;
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    Evaluation final_evaluation;
    std::vector<Point> final_layout;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    class State {
    public:
        std::vector<Point> layout;
        std::vector<double> squared_deficits;
        Evaluation evaluation;
    };
    struct Impl;

    explicit Problem(const std::string& problem_id);
    ~Problem();
    Problem(Problem&&) noexcept;
    Problem& operator=(Problem&&) noexcept;
    Problem(const Problem&) = delete;
    Problem& operator=(const Problem&) = delete;

    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] int turbine_count() const noexcept;
    [[nodiscard]] int direction_count() const noexcept;
    [[nodiscard]] int block_columns() const noexcept;
    [[nodiscard]] int block_rows() const noexcept;
    [[nodiscard]] double width_m() const noexcept;
    [[nodiscard]] double height_m() const noexcept;
    [[nodiscard]] double minimum_spacing_m() const noexcept;
    [[nodiscard]] bool valid_point(const Point& point) const noexcept;
    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate_full(
        const std::vector<Point>& layout
    ) const;
    [[nodiscard]] Evaluation evaluate_parallel(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::unique_ptr<State> make_state(
        const std::vector<Point>& layout,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] std::unique_ptr<State> update_state(
        const State& parent,
        const std::vector<Point>& child,
        fode::PersistentExecutor& executor
    ) const;
    [[nodiscard]] const std::vector<Point>& state_layout(
        const State& state
    ) const noexcept;
    [[nodiscard]] const Evaluation& state_evaluation(
        const State& state
    ) const noexcept;
    [[nodiscard]] std::vector<Point> random_feasible_layout(
        std::uint64_t seed,
        std::uint64_t event = 0
    ) const;

private:
    std::unique_ptr<Impl> impl_;
    friend RunResult run(const Problem&, const RunConfig&);
};

[[nodiscard]] std::vector<std::string> paper_problem_ids();
[[nodiscard]] std::vector<std::string> paper_algorithm_ids();
[[nodiscard]] RunResult run(
    const Problem& problem,
    const RunConfig& config
);

}  // namespace core99::t11
