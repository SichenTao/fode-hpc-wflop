/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T80 pure-C++ AGA-SP-MCTS and paper case backend
Paper/source/missing/reconstruction/semantic IDs:
hpc/core99_cpp/include/core99/bai_t80.hpp.
Public source: no T80 author source; T74 public GA lineage is disclosed.
Claim boundary: academic reconstruction with a figure-derived NJ proxy.
Contract: shared/contracts/core99_t80_bai_aga_mcts_2022.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/bai_t80.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t80 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int completed_population = 100;
constexpr int outer_generations = 200;
constexpr int completed_mcts_simulations = 200;
constexpr int relocated_turbines = 3;
constexpr int paper_repeat_count = 10;
constexpr double exploitation_rate = 0.5;
constexpr double elite_rate = 0.2;
constexpr double crossover_rate = 0.6;
constexpr double mutation_rate = 0.1;
constexpr double uct_exploration = 1.0;
constexpr double uct_variance_floor = 1.0e-6;

struct TreeNode {
    gridwake::Layout partial;
    int parent = -1;
    int action = -1;
    int depth = 0;
    int visits = 0;
    double reward_sum = 0.0;
    double squared_reward_sum = 0.0;
    std::vector<int> children;
};

struct MctsResult {
    gridwake::Layout layout;
    std::uint64_t physical_fes = 0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<gridwake::WindState> case1_wind(const int scenario) {
    if (scenario == 1) return {{45.0, 12.0, 1.0}};
    if (scenario == 2) {
        return {
            {0.0, 12.0, 0.25},
            {90.0, 12.0, 0.25},
            {180.0, 12.0, 0.25},
            {270.0, 12.0, 0.25},
        };
    }
    if (scenario == 3) {
        return {
            {0.0, 12.0, 0.20},
            {30.0, 12.0, 0.30},
            {60.0, 12.0, 0.20},
            {90.0, 12.0, 0.10},
            {120.0, 12.0, 0.10},
            {150.0, 12.0, 0.10},
        };
    }
    std::vector<gridwake::WindState> result;
    for (int direction = 0; direction < 12; ++direction) {
        const double angle = 30.0 * direction;
        result.push_back({angle, 8.0, 0.10 / 12.0});
        result.push_back({angle, 10.0, 0.20 / 12.0});
        result.push_back({angle, 12.0, 0.70 / 12.0});
    }
    return result;
}

std::vector<gridwake::WindState> new_jersey_wind() {
    constexpr double raw_total = 1.44;
    constexpr double direction_frequency[16]{
        0.09, 0.08, 0.085, 0.075,
        0.06, 0.055, 0.05, 0.06,
        0.10, 0.17, 0.16, 0.12,
        0.10, 0.09, 0.08, 0.065,
    };
    constexpr double speed[4]{2.0, 6.0, 10.0, 18.0};
    // The paper exposes only the stacked wind-rose image. Direction totals
    // above are a figure transcription; normalized speed shares below are a
    // declared completion of the unresolved color-stack heights.
    constexpr double speed_share[4]{0.05, 0.25, 0.40, 0.30};
    std::vector<gridwake::WindState> result;
    for (int direction = 0; direction < 16; ++direction) {
        for (int speed_index = 0; speed_index < 4; ++speed_index) {
            result.push_back({
                22.5 * direction,
                speed[speed_index],
                direction_frequency[direction] / raw_total
                    * speed_share[speed_index],
            });
        }
    }
    return result;
}

gridwake::Configuration configuration_for(const std::string& case_id) {
    if (case_id == "t80_case2_new_jersey") {
        return {
            .rows = 20,
            .columns = 30,
            .turbine_count = 99,
            .cell_width_m = 220.0,
            .turbine = {
                .name = "GE Haliade-X 12 MW",
                .rotor_diameter_m = 220.0,
                .hub_height_m = 138.0,
                .thrust_coefficient = 0.88,
                .wake_expansion = 0.1,
                .cut_in_mps = 4.0,
                .rated_mps = 11.0,
                .cut_out_mps = 25.0,
                .rated_power_kw = 12000.0,
            },
            .wind_states = new_jersey_wind(),
        };
    }
    if (!case_id.starts_with("t80_case1_s")) {
        throw std::invalid_argument("invalid T80 case id " + case_id);
    }
    const auto separator = case_id.find('_', std::string("t80_case1_s1").size());
    const int scenario = std::stoi(case_id.substr(11, 1));
    const std::string size = case_id.substr(separator + 1);
    double cell_width = 0.0;
    if (size == "small") cell_width = 231.0;
    else if (size == "medium") cell_width = 308.0;
    else if (size == "large") cell_width = 385.0;
    else throw std::invalid_argument("invalid T80 Case I size");
    if (scenario < 1 || scenario > 4) {
        throw std::invalid_argument("invalid T80 scenario");
    }
    return {
        .rows = 21,
        .columns = 21,
        .turbine_count = 60,
        .cell_width_m = cell_width,
        .turbine = {
            .name = "GE1.5sle",
            .rotor_diameter_m = 77.0,
            .hub_height_m = 80.0,
            .thrust_coefficient = 0.88,
            .wake_expansion = 0.1,
            .cut_in_mps = 2.0,
            .rated_mps = 12.8,
            .cut_out_mps = 18.0,
            .rated_power_kw = 629.1,
        },
        .wind_states = case1_wind(scenario),
    };
}

std::string semantic_id_for(const std::string& case_id) {
    if (case_id == "t80_case2_new_jersey") {
        return "t80_case2_new_jersey_figure_proxy_v1";
    }
    return case_id + "_v1";
}

gridwake::Layout random_layout(
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t phase
) {
    std::vector<std::pair<double, int>> order;
    order.reserve(static_cast<std::size_t>(problem.candidate_count()));
    for (int node = 0; node < problem.candidate_count(); ++node) {
        order.emplace_back(
            random.uniform(generation, phase, individual, node),
            node
        );
    }
    const int count = problem.configuration().turbine_count;
    std::nth_element(order.begin(), order.begin() + count, order.end());
    gridwake::Layout layout;
    layout.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        layout.push_back(order[static_cast<std::size_t>(index)].second);
    }
    std::sort(layout.begin(), layout.end());
    return layout;
}

int random_empty(
    const gridwake::Layout& layout,
    const int candidate_count,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    for (int attempt = 0; attempt < 2 * candidate_count; ++attempt) {
        const int node = random.integer(
            0,
            candidate_count,
            generation,
            phase,
            individual,
            attempt,
            draw
        );
        if (!std::binary_search(layout.begin(), layout.end(), node)) {
            return node;
        }
    }
    for (int node = 0; node < candidate_count; ++node) {
        if (!std::binary_search(layout.begin(), layout.end(), node)) {
            return node;
        }
    }
    throw std::runtime_error("T80 no empty grid cell");
}

gridwake::Layout insert_node(gridwake::Layout layout, const int node) {
    layout.insert(std::lower_bound(layout.begin(), layout.end(), node), node);
    return layout;
}

gridwake::Layout remove_worst(
    const gridwake::Layout& layout,
    const gridwake::Evaluation& evaluation
) {
    std::vector<int> order(layout.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](const int first, const int second) {
            return evaluation.turbine_power_kw[
                static_cast<std::size_t>(first)
            ] < evaluation.turbine_power_kw[
                static_cast<std::size_t>(second)
            ];
        }
    );
    gridwake::Layout result;
    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (
            std::find(
                order.begin(),
                order.begin() + relocated_turbines,
                static_cast<int>(index)
            ) == order.begin() + relocated_turbines
        ) {
            result.push_back(layout[index]);
        }
    }
    return result;
}

int expandable_child_limit(const TreeNode& node) {
    return std::max(
        1,
        static_cast<int>(std::ceil(std::sqrt(node.visits + 1.0)))
    );
}

double uct_score(const TreeNode& child, const int parent_visits) {
    if (child.visits == 0) return std::numeric_limits<double>::infinity();
    const double mean = child.reward_sum / child.visits;
    const double variance = std::max(
        0.0,
        child.squared_reward_sum / child.visits - mean * mean
    );
    return mean
        + uct_exploration
            * std::sqrt(
                std::log(std::max(2, parent_visits))
                / static_cast<double>(child.visits)
            )
        + std::sqrt(variance + uct_variance_floor);
}

int choose_new_action(
    const TreeNode& node,
    const std::vector<TreeNode>& tree,
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t simulation
) {
    for (int attempt = 0; attempt < 2 * problem.candidate_count(); ++attempt) {
        const int candidate = random.integer(
            0,
            problem.candidate_count(),
            generation,
            100 + static_cast<std::uint64_t>(node.depth),
            individual,
            simulation,
            attempt
        );
        if (
            std::binary_search(
                node.partial.begin(), node.partial.end(), candidate
            )
        ) {
            continue;
        }
        const bool already_child = std::any_of(
            node.children.begin(),
            node.children.end(),
            [&](const int child) {
                return tree[static_cast<std::size_t>(child)].action
                    == candidate;
            }
        );
        if (!already_child) return candidate;
    }
    for (int candidate = 0; candidate < problem.candidate_count(); ++candidate) {
        if (
            !std::binary_search(
                node.partial.begin(), node.partial.end(), candidate
            )
            && std::none_of(
                node.children.begin(),
                node.children.end(),
                [&](const int child) {
                    return tree[static_cast<std::size_t>(child)].action
                        == candidate;
                }
            )
        ) {
            return candidate;
        }
    }
    return -1;
}

MctsResult relocate_mcts(
    const gridwake::Layout& layout,
    const gridwake::Evaluation& evaluation,
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const int simulations
) {
    std::vector<TreeNode> tree;
    tree.push_back({
        .partial = remove_worst(layout, evaluation),
        .parent = -1,
        .action = -1,
        .depth = 0,
    });
    gridwake::Layout best_layout = layout;
    double best_reward = evaluation.conversion_efficiency_percent;
    for (int simulation = 0; simulation < simulations; ++simulation) {
        int current = 0;
        while (tree[static_cast<std::size_t>(current)].depth
               < relocated_turbines) {
            auto& node = tree[static_cast<std::size_t>(current)];
            const int child_limit = std::min(
                problem.candidate_count()
                    - static_cast<int>(node.partial.size()),
                expandable_child_limit(node)
            );
            if (static_cast<int>(node.children.size()) < child_limit) {
                const int action = choose_new_action(
                    node,
                    tree,
                    problem,
                    random,
                    generation,
                    individual,
                    simulation
                );
                if (action < 0) break;
                TreeNode child;
                child.partial = insert_node(node.partial, action);
                child.parent = current;
                child.action = action;
                child.depth = node.depth + 1;
                tree.push_back(std::move(child));
                const int child_index = static_cast<int>(tree.size()) - 1;
                tree[static_cast<std::size_t>(current)]
                    .children.push_back(child_index);
                current = child_index;
                break;
            }
            current = *std::max_element(
                node.children.begin(),
                node.children.end(),
                [&](const int first, const int second) {
                    return uct_score(
                        tree[static_cast<std::size_t>(first)],
                        node.visits
                    ) < uct_score(
                        tree[static_cast<std::size_t>(second)],
                        node.visits
                    );
                }
            );
        }
        gridwake::Layout terminal =
            tree[static_cast<std::size_t>(current)].partial;
        while (
            static_cast<int>(terminal.size())
            < problem.configuration().turbine_count
        ) {
            const int inserted = random_empty(
                terminal,
                problem.candidate_count(),
                random,
                generation,
                150 + terminal.size(),
                individual,
                simulation
            );
            terminal = insert_node(std::move(terminal), inserted);
        }
        const auto terminal_evaluation = problem.evaluate(terminal);
        const double reward =
            terminal_evaluation.conversion_efficiency_percent;
        if (reward > best_reward) {
            best_reward = reward;
            best_layout = terminal;
        }
        int node = current;
        while (node >= 0) {
            auto& item = tree[static_cast<std::size_t>(node)];
            ++item.visits;
            item.reward_sum += reward;
            item.squared_reward_sum += reward * reward;
            node = item.parent;
        }
    }
    return {std::move(best_layout), static_cast<std::uint64_t>(simulations)};
}

gridwake::Layout relocate_random(
    const gridwake::Layout& layout,
    const gridwake::Evaluation& evaluation,
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    auto result = remove_worst(layout, evaluation);
    while (
        static_cast<int>(result.size())
        < problem.configuration().turbine_count
    ) {
        const int inserted = random_empty(
            result,
            problem.candidate_count(),
            random,
            generation,
            200 + result.size(),
            individual,
            result.size()
        );
        result = insert_node(std::move(result), inserted);
    }
    return result;
}

gridwake::Layout crossover(
    const gridwake::Layout& first,
    const gridwake::Layout& second,
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    const int count = problem.configuration().turbine_count;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const int cut = random.integer(
            1, count, generation, 300, individual, attempt
        );
        if (first[static_cast<std::size_t>(cut - 1)]
            >= second[static_cast<std::size_t>(cut)]) {
            continue;
        }
        gridwake::Layout child;
        child.insert(child.end(), first.begin(), first.begin() + cut);
        child.insert(child.end(), second.begin() + cut, second.end());
        if (problem.feasible(child)) return child;
    }
    return random_layout(problem, random, generation, individual, 301);
}

gridwake::Layout mutate(
    gridwake::Layout layout,
    const gridwake::Problem& problem,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    const int removed = random.integer(
        0,
        static_cast<int>(layout.size()),
        generation,
        310,
        individual
    );
    layout.erase(layout.begin() + removed);
    const int inserted = random_empty(
        layout,
        problem.candidate_count(),
        random,
        generation,
        311,
        individual,
        0
    );
    return insert_node(std::move(layout), inserted);
}

std::uint64_t scientific_hash(
    const gridwake::Layout& layout,
    const std::vector<double>& history,
    const std::uint64_t physical_fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_hash(hash, physical_fes);
    for (const int node : layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(node));
    }
    for (const double value : history) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hash;
}

}  // namespace

Problem::Problem(std::string case_id)
    : case_id_(std::move(case_id)),
      semantic_id_(semantic_id_for(case_id_)),
      evaluator_(configuration_for(case_id_)) {}

const std::string& Problem::case_id() const noexcept { return case_id_; }
const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}
const gridwake::Problem& Problem::evaluator() const noexcept {
    return evaluator_;
}
int Problem::paper_population_completion() const noexcept {
    return completed_population;
}
int Problem::paper_generations() const noexcept { return outer_generations; }
int Problem::paper_mcts_simulations_completion() const noexcept {
    return completed_mcts_simulations;
}
int Problem::paper_repeats() const noexcept { return paper_repeat_count; }

RunResult Problem::optimize(const RunConfig& config) const {
    const int population_count =
        config.population < 0 ? completed_population : config.population;
    const int generations =
        config.generations < 0 ? outer_generations : config.generations;
    const int simulations =
        config.mcts_simulations < 0
        ? completed_mcts_simulations
        : config.mcts_simulations;
    if (
        config.workers < 1
        || population_count < 2
        || generations < 1
        || simulations < 1
    ) {
        throw std::invalid_argument("invalid T80 execution configuration");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng random(config.seed);
    std::vector<gridwake::Layout> population(
        static_cast<std::size_t>(population_count)
    );
    executor.parallel_for(0, population_count, [&](const int individual) {
        population[static_cast<std::size_t>(individual)] = random_layout(
            evaluator_, random, 0, individual, 10
        );
    });
    std::vector<gridwake::Evaluation> evaluations(
        static_cast<std::size_t>(population_count)
    );
    std::vector<double> history;
    history.reserve(static_cast<std::size_t>(generations));
    gridwake::Evaluation initial_best;
    gridwake::Evaluation best;
    gridwake::Layout best_layout;
    std::uint64_t physical_fes = 0;
    double evaluation_seconds = 0.0;
    double relocation_seconds = 0.0;
    double operator_seconds = 0.0;

    for (int generation = 0; generation < generations; ++generation) {
        const auto evaluation_start = Clock::now();
        executor.parallel_for(0, population_count, [&](const int individual) {
            evaluations[static_cast<std::size_t>(individual)] =
                evaluator_.evaluate(
                    population[static_cast<std::size_t>(individual)]
                );
        });
        evaluation_seconds += elapsed_seconds(evaluation_start);
        physical_fes += static_cast<std::uint64_t>(population_count);
        std::vector<int> order(static_cast<std::size_t>(population_count));
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int first, const int second) {
                return evaluations[static_cast<std::size_t>(first)]
                    .expected_power_kw
                    > evaluations[static_cast<std::size_t>(second)]
                    .expected_power_kw;
            }
        );
        if (generation == 0) {
            initial_best = evaluations[
                static_cast<std::size_t>(order.front())
            ];
        }
        const auto& generation_best =
            evaluations[static_cast<std::size_t>(order.front())];
        if (
            !best.feasible
            || generation_best.expected_power_kw > best.expected_power_kw
        ) {
            best = generation_best;
            best_layout =
                population[static_cast<std::size_t>(order.front())];
        }
        history.push_back(best.conversion_efficiency_percent);

        std::vector<std::uint64_t> relocation_fes(
            static_cast<std::size_t>(population_count),
            0
        );
        const auto relocation_start = Clock::now();
        executor.parallel_for(0, population_count, [&](const int individual) {
            const bool exploit = random.uniform(
                generation, 20, individual
            ) < exploitation_rate;
            if (exploit) {
                auto result = relocate_mcts(
                    population[static_cast<std::size_t>(individual)],
                    evaluations[static_cast<std::size_t>(individual)],
                    evaluator_,
                    random,
                    generation,
                    individual,
                    simulations
                );
                population[static_cast<std::size_t>(individual)] =
                    std::move(result.layout);
                relocation_fes[static_cast<std::size_t>(individual)] =
                    result.physical_fes;
            } else {
                population[static_cast<std::size_t>(individual)] =
                    relocate_random(
                        population[static_cast<std::size_t>(individual)],
                        evaluations[static_cast<std::size_t>(individual)],
                        evaluator_,
                        random,
                        generation,
                        individual
                    );
            }
        });
        relocation_seconds += elapsed_seconds(relocation_start);
        physical_fes += std::accumulate(
            relocation_fes.begin(),
            relocation_fes.end(),
            std::uint64_t{0}
        );

        const auto operator_start = Clock::now();
        const int elites = std::max(
            2,
            static_cast<int>(elite_rate * population_count)
        );
        std::vector<gridwake::Layout> mating_pool(
            static_cast<std::size_t>(population_count)
        );
        for (int index = 0; index < elites; ++index) {
            mating_pool[static_cast<std::size_t>(index)] =
                population[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(index)]
                )];
        }
        executor.parallel_for(
            elites,
            population_count,
            [&](const int individual) {
                mating_pool[static_cast<std::size_t>(individual)] =
                    random_layout(
                        evaluator_,
                        random,
                        generation,
                        individual,
                        30
                    );
            }
        );
        std::vector<gridwake::Layout> next_population(
            static_cast<std::size_t>(population_count)
        );
        executor.parallel_for(0, population_count, [&](const int individual) {
            const int first = random.integer(
                0,
                population_count,
                generation,
                31,
                individual
            );
            int second = random.integer(
                0,
                population_count,
                generation,
                32,
                individual
            );
            if (second == first) second = (second + 1) % population_count;
            gridwake::Layout child =
                random.uniform(generation, 33, individual) < crossover_rate
                ? crossover(
                    mating_pool[static_cast<std::size_t>(first)],
                    mating_pool[static_cast<std::size_t>(second)],
                    evaluator_,
                    random,
                    generation,
                    individual
                )
                : mating_pool[static_cast<std::size_t>(first)];
            if (random.uniform(generation, 34, individual) < mutation_rate) {
                child = mutate(
                    std::move(child),
                    evaluator_,
                    random,
                    generation,
                    individual
                );
            }
            next_population[static_cast<std::size_t>(individual)] =
                std::move(child);
        });
        population = std::move(next_population);
        operator_seconds += elapsed_seconds(operator_start);
    }

    const auto receipt = executor.work_receipt();
    RunResult result;
    result.case_id = case_id_;
    result.problem_semantic_id = semantic_id_;
    result.method_semantic_id =
        "t80_aga_spmcts_declared_completion_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.population = population_count;
    result.generations = generations;
    result.mcts_simulations = simulations;
    result.physical_fes = physical_fes;
    result.initial_best = std::move(initial_best);
    result.best_evaluation = std::move(best);
    result.best_layout = std::move(best_layout);
    result.best_efficiency_history_percent = std::move(history);
    result.population_evaluation_seconds = evaluation_seconds;
    result.mcts_relocation_seconds = relocation_seconds;
    result.genetic_operator_seconds = operator_seconds;
    result.end_to_end_seconds = elapsed_seconds(total_start);
    result.scientific_hash = scientific_hash(
        result.best_layout,
        result.best_efficiency_history_percent,
        result.physical_fes
    );
    return result;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (int scenario = 1; scenario <= 4; ++scenario) {
        for (const std::string size : {"small", "medium", "large"}) {
            result.push_back(
                "t80_case1_s" + std::to_string(scenario) + "_" + size
            );
        }
    }
    result.push_back("t80_case2_new_jersey");
    return result;
}

}  // namespace core99::t80
