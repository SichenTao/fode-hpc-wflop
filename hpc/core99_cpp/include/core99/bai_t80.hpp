/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T80 AGA-MCTS paper problem and algorithm
Paper: Fangyun Bai, Xinglong Ju, Shouyi Wang, Wenyong Zhou and Feng Liu,
Wind Farm Layout Optimization Using Adaptive Evolutionary Algorithm with
Monte Carlo Tree Search Reinforcement Learning, Energy Conversion and
Management 252 (2022) 115047, DOI 10.1016/j.enconman.2021.115047.
Primary PDF SHA-256:
89c73e7f261cbed357fa199f94e91d6c872b7c5227643296364b9608fa720f34.
Public source: no paper-linked author code or data was found in the frozen
source search; T74 predecessor source is used only for disclosed GA operator
lineage, not represented as T80 author code.
Paper-provided facts: Eqs. (1)-(17), Jensen wake with kw=0.1 and Ct=0.88,
overlap superposition, CoE and efficiency; SP-MCTS Algorithms 1-2; elite,
crossover, random and mutation rates 0.2/0.6/0.5/0.1; 200 outer iterations;
Case I has 60 GE1.5sle turbines on a 21x21 grid, four wind scenarios and
231/308/385 m cells; Case II has 99 Haliade-X 12 MW turbines on a 20x30
grid with 220 m cells and a four-speed/sixteen-direction figure wind rose;
ten independent repeats.
Missing: population size, number m of simultaneously relocated turbines,
exploitation rate rI, SP-MCTS simulation count, UCT constants C and D,
tree widening rule, exact GA operators/random states, New Jersey numeric
wind array and Haliade hub height were not reported; no author source,
initial populations or result arrays were found.
Reconstruction: population 100 follows the directly cited T74 GA lineage;
m=3, rI=0.5, 200 MCTS simulations, C=1, D=1e-6 and deterministic
square-root progressive widening are versioned completions; GA crossover
and mutation follow the T74 public lineage; the New Jersey direction totals
are transcribed from Fig. 12 and the unresolved stacked speed shares are a
declared normalized completion; official GE data supplies the 220 m rotor.
Method semantic ID: t80_aga_spmcts_declared_completion_v1.
Problem semantic IDs: t80_case1_grid60_12cases_v1;
t80_case2_new_jersey_figure_proxy_v1.
Protocol semantic ID: t80_13case_10repeat_pop100_outer200_mcts200_v1.
Production backend: pure C++ CPU; one persistent full-core team parallelizes
population wake evaluation, independent per-individual MCTS relocations,
crossover and mutation; each MCTS tree remains serial to preserve its
adaptive dependency, and formal tasks do not oversubscribe Waffle.
Claim boundary: academic paper/predecessor reconstruction; Case II is an
explicit figure-derived P3 proxy, not author wind data, code or numerical
replay.
Contract: shared/contracts/core99_t80_bai_aga_mcts_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#pragma once

#include "core99/discrete_grid_wake.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace core99::t80 {

struct RunConfig {
    std::uint64_t seed = 2026080000ULL;
    int workers = 20;
    int population = -1;
    int generations = -1;
    int mcts_simulations = -1;
};

struct RunResult {
    std::string case_id;
    std::string problem_semantic_id;
    std::string method_semantic_id;
    std::uint64_t seed = 0;
    int requested_workers = 0;
    int observed_workers = 0;
    int population = 0;
    int generations = 0;
    int mcts_simulations = 0;
    std::uint64_t physical_fes = 0;
    gridwake::Evaluation initial_best;
    gridwake::Evaluation best_evaluation;
    gridwake::Layout best_layout;
    std::vector<double> best_efficiency_history_percent;
    double population_evaluation_seconds = 0.0;
    double mcts_relocation_seconds = 0.0;
    double genetic_operator_seconds = 0.0;
    double end_to_end_seconds = 0.0;
    std::uint64_t scientific_hash = 0;
};

class Problem {
public:
    explicit Problem(std::string case_id);

    [[nodiscard]] const std::string& case_id() const noexcept;
    [[nodiscard]] const std::string& semantic_id() const noexcept;
    [[nodiscard]] const gridwake::Problem& evaluator() const noexcept;
    [[nodiscard]] int paper_population_completion() const noexcept;
    [[nodiscard]] int paper_generations() const noexcept;
    [[nodiscard]] int paper_mcts_simulations_completion() const noexcept;
    [[nodiscard]] int paper_repeats() const noexcept;
    [[nodiscard]] RunResult optimize(const RunConfig& config) const;

private:
    std::string case_id_;
    std::string semantic_id_;
    gridwake::Problem evaluator_;
};

[[nodiscard]] std::vector<std::string> paper_case_ids();

}  // namespace core99::t80
