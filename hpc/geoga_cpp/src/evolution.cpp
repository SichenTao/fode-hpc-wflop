/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared evolutionary execution
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Public asset/source: no author implementation or numerical Anholt data found; evidence dossier docs/source-dossiers/L0726.json
Missing information: author source, exact operator edge cases, and original Anholt numerical inputs
Reconstruction: geoga_declared_reconstruction_v1 freezes the declared operator and parallel-execution completions
Admitted method semantics reused unchanged: population 50, roulette selection proportional to nonnegative AEP, one-point crossover, unique-index repair, one geometric mutation uniformly among the five nearest free candidates, parent-plus-offspring deterministic best-50 survival, and initialization within exact physical FES
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: geoga_declared_reconstruction_v1
Execution profile ID: geoga_anholt_structured_p3_execution_v1
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Controlling contract: shared/contracts/geoga_anholt_structured_execution_contract.json
Parallel completion: persistent pure-CPU executor, counter-keyed schedule-independent RNG, parallel initialization/variation/evaluation, fixed-order reductions, all-visible default, and exact partial terminal batch
Physical FES: one complete layout evaluation across the selected case's twelve joint wind states; no cross-case denominator
Claim boundary: this extension does not alter the historical GGA-asset proxy path and does not reproduce the unavailable Anholt experiment
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "geoga/evolution.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace geoga {
namespace {

using Clock = std::chrono::steady_clock;
using Layout = std::vector<int>;

constexpr int kPopulationSize = 50;
constexpr int kNearestNeighbors = 5;

struct Individual {
    Layout layout;
    LayoutEvaluation evaluation;
};

StageReceipt capture_stage(
    double wall_seconds,
    const fode::PersistentExecutor& executor
) {
    const fode::ExecutorWorkReceipt source = executor.work_receipt();
    StageReceipt target;
    target.wall_seconds = wall_seconds;
    target.parallel_regions = source.parallel_regions;
    target.task_items = source.task_items;
    target.participant_activations = source.participant_activations;
    target.distinct_participants = source.distinct_participants;
    target.peak_region_participants = source.peak_region_participants;
    return target;
}

void add_stage(StageReceipt& total, const StageReceipt& addition) {
    total.wall_seconds += addition.wall_seconds;
    total.parallel_regions += addition.parallel_regions;
    total.task_items += addition.task_items;
    total.participant_activations += addition.participant_activations;
    total.distinct_participants =
        std::max(total.distinct_participants, addition.distinct_participants);
    total.peak_region_participants = std::max(
        total.peak_region_participants,
        addition.peak_region_participants
    );
}

template <typename Task>
StageReceipt timed_parallel(
    fode::PersistentExecutor& executor,
    int begin,
    int end,
    Task task
) {
    executor.reset_work_receipt();
    const auto start = Clock::now();
    executor.parallel_for(begin, end, task);
    const double wall_seconds = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    return capture_stage(wall_seconds, executor);
}

bool better(const Individual& left, const Individual& right) {
    if (left.evaluation.aep_kwh != right.evaluation.aep_kwh) {
        return left.evaluation.aep_kwh > right.evaluation.aep_kwh;
    }
    return left.layout < right.layout;
}

Layout random_layout(
    const Problem& problem,
    const fode::CounterRng& rng,
    int individual
) {
    std::vector<std::pair<double, int>> ranks;
    ranks.reserve(problem.candidates.size());
    for (int candidate = 0;
         candidate < static_cast<int>(problem.candidates.size());
         ++candidate) {
        ranks.emplace_back(
            rng.uniform(
                0, 3000, static_cast<std::uint64_t>(individual),
                static_cast<std::uint64_t>(candidate)
            ),
            candidate
        );
    }
    std::stable_sort(ranks.begin(), ranks.end());
    Layout result;
    result.reserve(static_cast<std::size_t>(problem.turbine_count));
    for (int position = 0; position < problem.turbine_count; ++position) {
        result.push_back(ranks[static_cast<std::size_t>(position)].second);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::uint64_t repair(
    Layout& layout,
    const Problem& problem,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    int child
) {
    std::vector<char> used(problem.candidates.size(), 0);
    Layout repaired;
    repaired.reserve(static_cast<std::size_t>(problem.turbine_count));
    repaired = layout;
    std::sort(repaired.begin(), repaired.end());
    repaired.erase(
        std::unique(repaired.begin(), repaired.end()),
        repaired.end()
    );
    repaired.erase(
        std::remove_if(
            repaired.begin(),
            repaired.end(),
            [&](int candidate) {
                return candidate < 0
                    || candidate
                        >= static_cast<int>(problem.candidates.size());
            }
        ),
        repaired.end()
    );
    for (const int candidate : repaired) {
        used[static_cast<std::size_t>(candidate)] = 1;
    }
    std::uint64_t additions = 0;
    std::uint64_t draw = 0;
    while (static_cast<int>(repaired.size()) < problem.turbine_count) {
        const int candidate = rng.integer(
            0,
            static_cast<int>(problem.candidates.size()),
            generation,
            3004,
            static_cast<std::uint64_t>(child),
            0,
            draw++
        );
        if (used[static_cast<std::size_t>(candidate)] == 0) {
            used[static_cast<std::size_t>(candidate)] = 1;
            repaired.push_back(candidate);
            ++additions;
        }
    }
    std::sort(repaired.begin(), repaired.end());
    layout = std::move(repaired);
    return additions;
}

int selected_parent(
    const std::vector<Individual>& population,
    double score_sum,
    double uniform_draw
) {
    if (!(score_sum > 0.0)) {
        int selected = static_cast<int>(
            uniform_draw * static_cast<double>(population.size())
        );
        if (selected >= static_cast<int>(population.size())) {
            selected = static_cast<int>(population.size()) - 1;
        }
        return selected;
    }
    const double threshold = uniform_draw * score_sum;
    double cumulative = 0.0;
    for (int row = 0; row < static_cast<int>(population.size()); ++row) {
        cumulative += std::max(
            0.0,
            population[static_cast<std::size_t>(row)].evaluation.aep_kwh
        );
        if (threshold <= cumulative) {
            return row;
        }
    }
    return static_cast<int>(population.size()) - 1;
}

std::string population_hash(const std::vector<Individual>& population) {
    Layout flattened;
    for (const Individual& individual : population) {
        flattened.insert(
            flattened.end(),
            individual.layout.begin(),
            individual.layout.end()
        );
        flattened.push_back(-1);
    }
    return layout_hash(flattened);
}

std::string stage_json(const StageReceipt& stage) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"wall_seconds\":" << stage.wall_seconds
           << ",\"parallel_regions\":" << stage.parallel_regions
           << ",\"task_items\":" << stage.task_items
           << ",\"participant_activations\":"
           << stage.participant_activations
           << ",\"distinct_participants\":" << stage.distinct_participants
           << ",\"peak_region_participants\":"
           << stage.peak_region_participants << '}';
    return output.str();
}

}  // namespace

namespace mechanism {

int nearest_free_replacement(
    const Problem& problem,
    const std::vector<int>& layout_0based,
    int mutation_position,
    double uniform_draw
) {
    if (mutation_position < 0
        || mutation_position >= static_cast<int>(layout_0based.size())) {
        throw std::runtime_error("GeoGA mutation position is invalid");
    }
    std::vector<char> used(problem.candidates.size(), 0);
    for (const int candidate : layout_0based) {
        used[static_cast<std::size_t>(candidate)] = 1;
    }
    const int current =
        layout_0based[static_cast<std::size_t>(mutation_position)];
    const Point& center =
        problem.candidates[static_cast<std::size_t>(current)];
    std::vector<std::pair<double, int>> nearest;
    for (int candidate = 0;
         candidate < static_cast<int>(problem.candidates.size());
         ++candidate) {
        if (used[static_cast<std::size_t>(candidate)] != 0) {
            continue;
        }
        const Point& point =
            problem.candidates[static_cast<std::size_t>(candidate)];
        const double dx = point.x_m - center.x_m;
        const double dy = point.y_m - center.y_m;
        nearest.emplace_back(dx * dx + dy * dy, candidate);
    }
    std::stable_sort(
        nearest.begin(), nearest.end(),
        [](const auto& left, const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            return left.second < right.second;
        }
    );
    const int choices = std::min(
        kNearestNeighbors, static_cast<int>(nearest.size())
    );
    if (choices == 0) {
        throw std::runtime_error("GeoGA mutation has no free candidate");
    }
    int selected = static_cast<int>(
        uniform_draw * static_cast<double>(choices)
    );
    if (selected >= choices) {
        selected = choices - 1;
    }
    return nearest[static_cast<std::size_t>(selected)].second;
}

}  // namespace mechanism

EvolutionResult run(const EvolutionConfig& config, const Problem& problem) {
    if (config.backend != "cpu") {
        throw std::runtime_error(
            "GeoGA Anholt declared profile supports only pure CPU; "
            "GPU/hybrid fail closed"
        );
    }
    if (config.physical_fes < kPopulationSize) {
        throw std::runtime_error(
            "GeoGA physical FES is below its 50-layout initialization"
        );
    }
    int resolved_workers = config.workers;
    if (resolved_workers == 0) {
        resolved_workers =
            static_cast<int>(std::thread::hardware_concurrency());
        resolved_workers = std::max(resolved_workers, 1);
    }
    if (resolved_workers < 1) {
        throw std::runtime_error("GeoGA workers must be nonnegative");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(resolved_workers);
    const fode::CounterRng rng(config.seed ^ 0x47454f4741ULL);
    std::vector<Individual> population(
        static_cast<std::size_t>(kPopulationSize)
    );

    const StageReceipt initialization = timed_parallel(
        executor, 0, kPopulationSize, [&](int individual) {
            population[static_cast<std::size_t>(individual)].layout =
                random_layout(problem, rng, individual);
        }
    );
    StageReceipt evaluator_total = timed_parallel(
        executor, 0, kPopulationSize, [&](int individual) {
            Individual& target =
                population[static_cast<std::size_t>(individual)];
            target.evaluation = evaluate_layout(problem, target.layout);
        }
    );
    WorkReceipt work;
    StageReceipt variation_total;
    StageReceipt selection_total;
    std::uint64_t physical_fes = kPopulationSize;
    std::uint64_t generation = 0;
    while (physical_fes < config.physical_fes) {
        ++generation;
        const int count = static_cast<int>(std::min<std::uint64_t>(
            kPopulationSize, config.physical_fes - physical_fes
        ));
        const auto selection_start = Clock::now();
        double score_sum = 0.0;
        for (const Individual& individual : population) {
            score_sum += std::max(0.0, individual.evaluation.aep_kwh);
        }
        selection_total.wall_seconds += std::chrono::duration<double>(
            Clock::now() - selection_start
        ).count();
        selection_total.task_items += population.size();

        std::vector<Individual> offspring(static_cast<std::size_t>(count));
        std::atomic<std::uint64_t> repair_additions{0};
        const StageReceipt variation = timed_parallel(
            executor, 0, count, [&](int child) {
                int first = selected_parent(
                    population,
                    score_sum,
                    rng.uniform(
                        generation, 3001,
                        static_cast<std::uint64_t>(child)
                    )
                );
                int second = selected_parent(
                    population,
                    score_sum,
                    rng.uniform(
                        generation, 3002,
                        static_cast<std::uint64_t>(child)
                    )
                );
                if (first == second) {
                    second = (second + 1) % kPopulationSize;
                }
                const int cut = rng.integer(
                    1,
                    problem.turbine_count,
                    generation,
                    3003,
                    static_cast<std::uint64_t>(child)
                );
                Layout layout;
                layout.reserve(
                    static_cast<std::size_t>(problem.turbine_count)
                );
                const Layout& first_parent =
                    population[static_cast<std::size_t>(first)].layout;
                const Layout& second_parent =
                    population[static_cast<std::size_t>(second)].layout;
                layout.insert(
                    layout.end(), first_parent.begin(),
                    first_parent.begin() + cut
                );
                layout.insert(
                    layout.end(), second_parent.begin() + cut,
                    second_parent.end()
                );
                repair_additions.fetch_add(
                    repair(layout, problem, rng, generation, child),
                    std::memory_order_relaxed
                );
                const int mutation_position = rng.integer(
                    0,
                    problem.turbine_count,
                    generation,
                    3006,
                    static_cast<std::uint64_t>(child)
                );
                layout[static_cast<std::size_t>(mutation_position)] =
                    mechanism::nearest_free_replacement(
                        problem,
                        layout,
                        mutation_position,
                        rng.uniform(
                            generation, 3007,
                            static_cast<std::uint64_t>(child)
                        )
                    );
                std::sort(layout.begin(), layout.end());
                offspring[static_cast<std::size_t>(child)].layout =
                    std::move(layout);
            }
        );
        add_stage(variation_total, variation);
        work.roulette_parent_draws +=
            2ULL * static_cast<std::uint64_t>(count);
        work.crossover_gene_copies +=
            static_cast<std::uint64_t>(count)
            * static_cast<std::uint64_t>(problem.turbine_count);
        work.duplicate_repairs +=
            repair_additions.load(std::memory_order_relaxed);
        work.geometry_distance_checks +=
            static_cast<std::uint64_t>(count)
            * static_cast<std::uint64_t>(
                problem.target_candidate_count - problem.turbine_count
            );
        work.geometry_mutations += static_cast<std::uint64_t>(count);

        const StageReceipt evaluator = timed_parallel(
            executor, 0, count, [&](int child) {
                Individual& target =
                    offspring[static_cast<std::size_t>(child)];
                target.evaluation = evaluate_layout(problem, target.layout);
            }
        );
        add_stage(evaluator_total, evaluator);
        physical_fes += static_cast<std::uint64_t>(count);

        const auto survival_start = Clock::now();
        std::vector<Individual> pool = population;
        pool.insert(
            pool.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end())
        );
        std::stable_sort(pool.begin(), pool.end(), better);
        pool.resize(kPopulationSize);
        population = std::move(pool);
        selection_total.wall_seconds += std::chrono::duration<double>(
            Clock::now() - survival_start
        ).count();
        selection_total.task_items +=
            static_cast<std::uint64_t>(kPopulationSize + count);
        work.survivor_candidates_ranked +=
            static_cast<std::uint64_t>(kPopulationSize + count);
    }

    const auto best_iterator = std::max_element(
        population.begin(),
        population.end(),
        [](const Individual& left, const Individual& right) {
            return left.evaluation.aep_kwh < right.evaluation.aep_kwh;
        }
    );
    const Individual& best = *best_iterator;
    EvolutionResult result;
    result.method_semantic_id = kMethodSemanticId;
    result.execution_profile_id = kExecutionProfileId;
    result.problem_semantic_id = kProblemSemanticId;
    result.problem_semantic_hash = problem_semantic_hash(problem);
    result.case_id = problem.case_id;
    result.seed = config.seed;
    result.physical_fes = physical_fes;
    result.generations = generation;
    result.requested_workers = config.workers;
    result.resolved_workers = resolved_workers;
    result.total_wall_seconds = std::chrono::duration<double>(
        Clock::now() - total_start
    ).count();
    result.initialization_stage = initialization;
    result.variation_repair_stage = variation_total;
    result.evaluator_stage = evaluator_total;
    result.selection_other_stage = selection_total;
    result.work = work;
    result.best_layout_0based = best.layout;
    result.best_evaluation = best.evaluation;
    result.best_layout_hash = layout_hash(best.layout);
    result.population_layout_hash = population_hash(population);
    return result;
}

std::string result_to_json(const EvolutionResult& result) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"method_semantic_id\":\"" << result.method_semantic_id
           << "\",\"execution_profile_id\":\""
           << result.execution_profile_id
           << "\",\"problem_semantic_id\":\""
           << result.problem_semantic_id
           << "\",\"problem_semantic_hash\":\""
           << result.problem_semantic_hash
           << "\",\"case_id\":\"" << result.case_id
           << "\",\"seed\":" << result.seed
           << ",\"physical_fes\":" << result.physical_fes
           << ",\"physical_fes_denominator\":\"one complete layout "
              "evaluation over this selected case's twelve joint wind "
              "states\""
           << ",\"generations\":" << result.generations
           << ",\"requested_workers\":" << result.requested_workers
           << ",\"resolved_workers\":" << result.resolved_workers
           << ",\"backend\":\"pure_cpp_cpu\""
           << ",\"total_wall_seconds\":" << result.total_wall_seconds
           << ",\"stages\":{\"initialization\":"
           << stage_json(result.initialization_stage)
           << ",\"variation_repair\":"
           << stage_json(result.variation_repair_stage)
           << ",\"evaluator\":" << stage_json(result.evaluator_stage)
           << ",\"selection_other\":"
           << stage_json(result.selection_other_stage) << '}'
           << ",\"work_receipt\":{\"roulette_parent_draws\":"
           << result.work.roulette_parent_draws
           << ",\"crossover_gene_copies\":"
           << result.work.crossover_gene_copies
           << ",\"duplicate_repairs\":" << result.work.duplicate_repairs
           << ",\"geometry_distance_checks\":"
           << result.work.geometry_distance_checks
           << ",\"geometry_mutations\":" << result.work.geometry_mutations
           << ",\"survivor_candidates_ranked\":"
           << result.work.survivor_candidates_ranked << '}'
           << ",\"best\":{\"aep_kwh\":" << result.best_evaluation.aep_kwh
           << ",\"no_wake_aep_kwh\":"
           << result.best_evaluation.no_wake_aep_kwh
           << ",\"capacity_factor\":"
           << result.best_evaluation.capacity_factor
           << ",\"layout_0based\":[";
    for (std::size_t index = 0;
         index < result.best_layout_0based.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << result.best_layout_0based[index];
    }
    output << "],\"layout_hash\":\"" << result.best_layout_hash << "\"}"
           << ",\"population_layout_hash\":\""
           << result.population_layout_hash << "\""
           << ",\"actual_layout_comparison\":{\"status\":\"blocked\","
              "\"reason\":\"original actual-layout coordinates and the "
              "paper evaluator arrays are unavailable; no numerical "
              "comparison is emitted\"}"
           << ",\"claim_boundary\":\"Anholt-structured P3 declared proxy; "
              "not the original Anholt experiment\"}";
    return output.str();
}

}  // namespace geoga
