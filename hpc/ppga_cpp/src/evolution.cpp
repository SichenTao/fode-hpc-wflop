/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared 3D evolutionary state machine
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Paper-preserved fields: population 30, threshold zero, crossover 0.8, mutation 0.1, power-law exponent 2.5, second-generation perturbation, elites, and complete-layout evaluations
Declared M3 completions: elite count three, min-max normalized fitness, occupied-site distance, row-aligned stagnation, uniform crossover, one-cell mutation, truncated discrete power law, deterministic repair, exact partial terminal batch, stable ties, and counter-keyed RNG
Method evidence tier: M3_DECLARED_COMPLETION
Method semantic ID: ppga_nantong_structured_3d_declared_reconstruction_v1
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_reconstruction_contract.json
Claim boundary: bounded declared reconstruction only; original PPGA transitions and Nantong paper results remain blocked
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "ppga/evolution.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ppga {
namespace {

using Clock = std::chrono::steady_clock;
using Layout = std::vector<int>;

constexpr int kPopulationSize = 30;
constexpr int kEliteCount = 3;
constexpr double kCrossoverProbability = 0.8;
constexpr double kMutationProbability = 0.1;
constexpr double kPowerLawExponent = 2.5;

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
    const double wall = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    return capture_stage(wall, executor);
}

bool better(const Individual& left, const Individual& right) {
    if (left.evaluation.conversion_efficiency
        != right.evaluation.conversion_efficiency) {
        return left.evaluation.conversion_efficiency
            > right.evaluation.conversion_efficiency;
    }
    return left.layout < right.layout;
}

Layout initial_layout(
    const Problem& problem,
    const fode::CounterRng& rng,
    int individual
) {
    const int dimension = problem.rows * problem.cols;
    Layout pool(static_cast<std::size_t>(dimension));
    std::iota(pool.begin(), pool.end(), 1);
    for (int position = 0; position < problem.turbine_count; ++position) {
        const int selected = rng.integer(
            position, dimension, 0, 10,
            static_cast<std::uint64_t>(individual),
            static_cast<std::uint64_t>(position)
        );
        std::swap(
            pool[static_cast<std::size_t>(position)],
            pool[static_cast<std::size_t>(selected)]
        );
    }
    pool.resize(static_cast<std::size_t>(problem.turbine_count));
    std::sort(pool.begin(), pool.end());
    return pool;
}

std::uint64_t repair_layout(
    Layout& layout,
    int dimension,
    int fill_start
) {
    std::vector<unsigned char> occupied(
        static_cast<std::size_t>(dimension + 1), 0
    );
    Layout unique;
    unique.reserve(layout.size());
    for (int cell : layout) {
        const int normalized =
            1 + ((cell - 1) % dimension + dimension) % dimension;
        if (occupied[static_cast<std::size_t>(normalized)] == 0U) {
            occupied[static_cast<std::size_t>(normalized)] = 1U;
            unique.push_back(normalized);
        }
    }
    const std::uint64_t duplicates =
        static_cast<std::uint64_t>(layout.size() - unique.size());
    int candidate = 1 + ((fill_start - 1) % dimension + dimension) % dimension;
    while (unique.size() < layout.size()) {
        if (occupied[static_cast<std::size_t>(candidate)] == 0U) {
            occupied[static_cast<std::size_t>(candidate)] = 1U;
            unique.push_back(candidate);
        }
        candidate = candidate == dimension ? 1 : candidate + 1;
    }
    std::sort(unique.begin(), unique.end());
    layout = std::move(unique);
    return duplicates;
}

int tournament(
    const std::vector<Individual>& population,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    int child,
    std::uint64_t draw
) {
    const int a = rng.integer(
        0, static_cast<int>(population.size()), generation, 20,
        static_cast<std::uint64_t>(child), 0, draw
    );
    const int b = rng.integer(
        0, static_cast<int>(population.size()), generation, 20,
        static_cast<std::uint64_t>(child), 1, draw
    );
    return better(
        population[static_cast<std::size_t>(a)],
        population[static_cast<std::size_t>(b)]
    ) ? a : b;
}

int power_law_step(
    const fode::CounterRng& rng,
    std::uint64_t generation,
    int individual,
    int dimension
) {
    const double u = std::min(
        rng.uniform(
            generation, 24, static_cast<std::uint64_t>(individual), 0, 0
        ),
        std::nextafter(1.0, 0.0)
    );
    const double raw =
        std::pow(1.0 - u, -1.0 / (kPowerLawExponent - 1.0));
    return std::clamp(static_cast<int>(std::floor(raw)), 1, dimension - 1);
}

std::uint64_t fnv_layouts(const std::vector<Individual>& population) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Individual& individual : population) {
        for (int cell : individual.layout) {
            const auto* bytes =
                reinterpret_cast<const unsigned char*>(&cell);
            for (std::size_t i = 0; i < sizeof(cell); ++i) {
                hash ^= static_cast<std::uint64_t>(bytes[i]);
                hash *= 1099511628211ULL;
            }
        }
    }
    return hash;
}

std::string hex_hash(std::uint64_t hash) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

std::string hash_layout(const Layout& layout) {
    std::vector<Individual> wrapper(1);
    wrapper[0].layout = layout;
    return hex_hash(fnv_layouts(wrapper));
}

void evaluate_batch(
    std::vector<Individual>& population,
    int count,
    const Problem& problem,
    fode::PersistentExecutor& executor,
    StageReceipt& evaluator_stage
) {
    add_stage(
        evaluator_stage,
        timed_parallel(executor, 0, count, [&](int index) {
            population[static_cast<std::size_t>(index)].evaluation =
                evaluate_layout(
                    problem,
                    population[static_cast<std::size_t>(index)].layout
                );
        })
    );
}

void update_best(
    Individual& best,
    bool& has_best,
    const std::vector<Individual>& candidates,
    int count
) {
    for (int i = 0; i < count; ++i) {
        const Individual& candidate = candidates[static_cast<std::size_t>(i)];
        if (!has_best || better(candidate, best)) {
            best = candidate;
            has_best = true;
        }
    }
}

std::string json_escape(const std::string& value) {
    std::string result;
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

void write_stage(
    std::ostringstream& output,
    const std::string& name,
    const StageReceipt& stage,
    bool comma
) {
    output << "    \"" << name << "\": {"
           << "\"wall_seconds\": " << stage.wall_seconds
           << ", \"parallel_regions\": " << stage.parallel_regions
           << ", \"task_items\": " << stage.task_items
           << ", \"participant_activations\": "
           << stage.participant_activations
           << ", \"distinct_participants\": "
           << stage.distinct_participants
           << ", \"peak_region_participants\": "
           << stage.peak_region_participants << "}";
    if (comma) {
        output << ',';
    }
    output << '\n';
}

}  // namespace

EvolutionResult run(
    const EvolutionConfig& config,
    const Problem& problem
) {
    if (config.backend != "cpu") {
        throw std::invalid_argument(
            "PPGA declared reconstruction supports backend=cpu only"
        );
    }
    if (config.physical_fes < static_cast<std::uint64_t>(kPopulationSize)) {
        throw std::invalid_argument("PPGA physical FES must be at least 30");
    }
    int resolved_workers = config.workers;
    if (resolved_workers == 0) {
        resolved_workers = static_cast<int>(std::thread::hardware_concurrency());
        resolved_workers = std::max(resolved_workers, 1);
    }
    if (resolved_workers < 1) {
        throw std::invalid_argument("PPGA workers must be zero or positive");
    }

    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(resolved_workers);
    const fode::CounterRng rng(config.seed);
    const int dimension = problem.rows * problem.cols;
    std::vector<Individual> population(
        static_cast<std::size_t>(kPopulationSize)
    );
    EvolutionResult result;
    result.method_semantic_id = kMethodSemanticId;
    result.problem_semantic_id = kProblemSemanticId;
    result.problem_semantic_hash = problem_semantic_hash(problem);
    result.case_id = problem.case_id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.resolved_workers = resolved_workers;

    result.initialization_stage = timed_parallel(
        executor, 0, kPopulationSize, [&](int index) {
            population[static_cast<std::size_t>(index)].layout =
                initial_layout(problem, rng, index);
        }
    );
    evaluate_batch(
        population, kPopulationSize, problem, executor, result.evaluator_stage
    );
    std::uint64_t fes = static_cast<std::uint64_t>(kPopulationSize);
    Individual best;
    bool has_best = false;
    update_best(best, has_best, population, kPopulationSize);
    std::vector<double> previous_scores(static_cast<std::size_t>(kPopulationSize));
    for (int i = 0; i < kPopulationSize; ++i) {
        previous_scores[static_cast<std::size_t>(i)] =
            population[static_cast<std::size_t>(i)]
                .evaluation.conversion_efficiency;
    }

    std::uint64_t generation = 0;
    while (fes < config.physical_fes) {
        ++generation;
        std::vector<double> row_distance(
            static_cast<std::size_t>(kPopulationSize), 0.0
        );
        add_stage(
            result.diversity_adaptation_stage,
            timed_parallel(executor, 0, kPopulationSize, [&](int left) {
                double local = 0.0;
                for (int right = left + 1; right < kPopulationSize; ++right) {
                    const Layout& a =
                        population[static_cast<std::size_t>(left)].layout;
                    const Layout& b =
                        population[static_cast<std::size_t>(right)].layout;
                    std::size_t i = 0;
                    std::size_t j = 0;
                    int intersection = 0;
                    while (i < a.size() && j < b.size()) {
                        if (a[i] == b[j]) {
                            ++intersection;
                            ++i;
                            ++j;
                        } else if (a[i] < b[j]) {
                            ++i;
                        } else {
                            ++j;
                        }
                    }
                    local += 1.0
                        - static_cast<double>(intersection)
                            / static_cast<double>(problem.turbine_count);
                }
                row_distance[static_cast<std::size_t>(left)] = local;
            })
        );
        result.work.pairwise_layout_distances +=
            static_cast<std::uint64_t>(
                kPopulationSize * (kPopulationSize - 1) / 2
            );
        const double diversity =
            2.0 * std::accumulate(
                row_distance.begin(), row_distance.end(), 0.0
            ) / static_cast<double>(
                kPopulationSize * (kPopulationSize - 1)
            );
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        int stagnant_count = 0;
        for (int i = 0; i < kPopulationSize; ++i) {
            const double score = population[static_cast<std::size_t>(i)]
                                     .evaluation.conversion_efficiency;
            minimum = std::min(minimum, score);
            maximum = std::max(maximum, score);
            if (score <= previous_scores[static_cast<std::size_t>(i)]) {
                ++stagnant_count;
            }
        }
        const double stagnation =
            static_cast<double>(stagnant_count)
            / static_cast<double>(kPopulationSize);
        std::vector<unsigned char> perturb(
            static_cast<std::size_t>(kPopulationSize), 0U
        );
        add_stage(
            result.diversity_adaptation_stage,
            timed_parallel(executor, 0, kPopulationSize, [&](int index) {
                const double score =
                    population[static_cast<std::size_t>(index)]
                        .evaluation.conversion_efficiency;
                const double normalized = maximum > minimum
                    ? (score - minimum) / (maximum - minimum)
                    : 1.0;
                const double theta =
                    normalized * std::sqrt(diversity) - stagnation;
                perturb[static_cast<std::size_t>(index)] =
                    (generation >= 2U && theta <= 0.0) ? 1U : 0U;
            })
        );

        std::vector<Individual> offspring(
            static_cast<std::size_t>(kPopulationSize)
        );
        std::vector<WorkReceipt> local_work(
            static_cast<std::size_t>(kPopulationSize)
        );
        add_stage(
            result.variation_repair_stage,
            timed_parallel(executor, 0, kPopulationSize, [&](int child) {
                const int parent_a =
                    tournament(population, rng, generation, child, 0);
                const int parent_b =
                    tournament(population, rng, generation, child, 1);
                Layout layout =
                    population[static_cast<std::size_t>(parent_a)].layout;
                WorkReceipt& work = local_work[static_cast<std::size_t>(child)];
                const bool crossover = rng.uniform(
                    generation, 21, static_cast<std::uint64_t>(child)
                ) < kCrossoverProbability;
                if (crossover) {
                    const Layout& other =
                        population[static_cast<std::size_t>(parent_b)].layout;
                    for (int gene = 0; gene < problem.turbine_count; ++gene) {
                        if (rng.uniform(
                            generation, 22,
                            static_cast<std::uint64_t>(child),
                            static_cast<std::uint64_t>(gene)
                        ) < 0.5) {
                            layout[static_cast<std::size_t>(gene)] =
                                other[static_cast<std::size_t>(gene)];
                        }
                        ++work.crossover_gene_choices;
                    }
                }
                ++work.mutation_gene_trials;
                if (rng.uniform(
                    generation, 23, static_cast<std::uint64_t>(child)
                ) < kMutationProbability) {
                    const int gene = rng.integer(
                        0, problem.turbine_count, generation, 23,
                        static_cast<std::uint64_t>(child), 1
                    );
                    layout[static_cast<std::size_t>(gene)] = rng.integer(
                        1, dimension + 1, generation, 23,
                        static_cast<std::uint64_t>(child), 2
                    );
                    ++work.mutation_events;
                }
                if (perturb[static_cast<std::size_t>(child)] != 0U) {
                    const int gene = rng.integer(
                        0, problem.turbine_count, generation, 24,
                        static_cast<std::uint64_t>(child), 1
                    );
                    const int step =
                        power_law_step(rng, generation, child, dimension);
                    const int sign = rng.uniform(
                        generation, 24, static_cast<std::uint64_t>(child), 2
                    ) < 0.5 ? -1 : 1;
                    layout[static_cast<std::size_t>(gene)] += sign * step;
                    ++work.perturbed_individuals;
                    ++work.power_law_gene_steps;
                }
                const int fill_start = rng.integer(
                    1, dimension + 1, generation, 25,
                    static_cast<std::uint64_t>(child)
                );
                work.duplicate_repairs +=
                    repair_layout(layout, dimension, fill_start);
                offspring[static_cast<std::size_t>(child)].layout =
                    std::move(layout);
            })
        );
        for (const WorkReceipt& work : local_work) {
            result.work.crossover_gene_choices += work.crossover_gene_choices;
            result.work.mutation_gene_trials += work.mutation_gene_trials;
            result.work.mutation_events += work.mutation_events;
            result.work.perturbed_individuals += work.perturbed_individuals;
            result.work.power_law_gene_steps += work.power_law_gene_steps;
            result.work.duplicate_repairs += work.duplicate_repairs;
        }

        const std::uint64_t remaining = config.physical_fes - fes;
        const int batch = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(kPopulationSize), remaining
        ));
        evaluate_batch(
            offspring, batch, problem, executor, result.evaluator_stage
        );
        fes += static_cast<std::uint64_t>(batch);
        update_best(best, has_best, offspring, batch);
        if (batch < kPopulationSize) {
            break;
        }

        const auto selection_start = Clock::now();
        std::vector<int> parent_order(
            static_cast<std::size_t>(kPopulationSize)
        );
        std::iota(parent_order.begin(), parent_order.end(), 0);
        std::stable_sort(
            parent_order.begin(), parent_order.end(), [&](int left, int right) {
                return better(
                    population[static_cast<std::size_t>(left)],
                    population[static_cast<std::size_t>(right)]
                );
            }
        );
        std::vector<int> child_order(
            static_cast<std::size_t>(kPopulationSize)
        );
        std::iota(child_order.begin(), child_order.end(), 0);
        std::stable_sort(
            child_order.begin(), child_order.end(), [&](int left, int right) {
                return better(
                    offspring[static_cast<std::size_t>(left)],
                    offspring[static_cast<std::size_t>(right)]
                );
            }
        );
        std::vector<Individual> next;
        next.reserve(static_cast<std::size_t>(kPopulationSize));
        for (int elite = 0; elite < kEliteCount; ++elite) {
            next.push_back(
                population[static_cast<std::size_t>(
                    parent_order[static_cast<std::size_t>(elite)]
                )]
            );
        }
        for (int rank = 0; rank < kPopulationSize - kEliteCount; ++rank) {
            next.push_back(
                offspring[static_cast<std::size_t>(
                    child_order[static_cast<std::size_t>(rank)]
                )]
            );
        }
        result.selection_other_stage.wall_seconds +=
            std::chrono::duration<double>(
                Clock::now() - selection_start
            ).count();
        result.selection_other_stage.task_items +=
            static_cast<std::uint64_t>(2 * kPopulationSize);
        for (int i = 0; i < kPopulationSize; ++i) {
            previous_scores[static_cast<std::size_t>(i)] =
                population[static_cast<std::size_t>(i)]
                    .evaluation.conversion_efficiency;
        }
        population = std::move(next);
    }

    result.physical_fes = fes;
    result.generations = generation;
    result.best_layout_1based = best.layout;
    result.best_evaluation = best.evaluation;
    result.population_layout_hash = hex_hash(fnv_layouts(population));
    result.best_layout_hash = hash_layout(best.layout);
    result.total_wall_seconds =
        std::chrono::duration<double>(Clock::now() - total_start).count();
    return result;
}

std::string result_to_json(const EvolutionResult& result) {
    std::ostringstream output;
    output << std::setprecision(17);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"method_semantic_id\": \""
           << json_escape(result.method_semantic_id) << "\",\n"
           << "  \"problem_semantic_id\": \""
           << json_escape(result.problem_semantic_id) << "\",\n"
           << "  \"problem_semantic_hash\": \""
           << result.problem_semantic_hash << "\",\n"
           << "  \"case_id\": \"" << json_escape(result.case_id) << "\",\n"
           << "  \"seed\": " << result.seed << ",\n"
           << "  \"physical_fes\": " << result.physical_fes << ",\n"
           << "  \"generations\": " << result.generations << ",\n"
           << "  \"backend\": \"cpu\",\n"
           << "  \"requested_workers\": " << result.requested_workers << ",\n"
           << "  \"resolved_workers\": " << result.resolved_workers << ",\n"
           << "  \"total_wall_seconds\": " << result.total_wall_seconds
           << ",\n"
           << "  \"best\": {\n"
           << "    \"layout_1based\": [";
    for (std::size_t i = 0; i < result.best_layout_1based.size(); ++i) {
        if (i != 0) {
            output << ", ";
        }
        output << result.best_layout_1based[i];
    }
    output << "],\n"
           << "    \"expected_power_kw\": "
           << result.best_evaluation.expected_power_kw << ",\n"
           << "    \"ideal_expected_power_kw\": "
           << result.best_evaluation.ideal_expected_power_kw << ",\n"
           << "    \"conversion_efficiency\": "
           << result.best_evaluation.conversion_efficiency << ",\n"
           << "    \"cost_per_expected_power\": "
           << result.best_evaluation.cost_per_expected_power << ",\n"
           << "    \"layout_hash\": \"" << result.best_layout_hash << "\"\n"
           << "  },\n"
           << "  \"population_layout_hash\": \""
           << result.population_layout_hash << "\",\n"
           << "  \"stage_receipts\": {\n";
    write_stage(output, "initialization", result.initialization_stage, true);
    write_stage(
        output, "diversity_adaptation",
        result.diversity_adaptation_stage, true
    );
    write_stage(
        output, "variation_repair", result.variation_repair_stage, true
    );
    write_stage(output, "evaluator", result.evaluator_stage, true);
    write_stage(
        output, "selection_other", result.selection_other_stage, false
    );
    output << "  },\n"
           << "  \"work_receipt\": {\n"
           << "    \"pairwise_layout_distances\": "
           << result.work.pairwise_layout_distances << ",\n"
           << "    \"crossover_gene_choices\": "
           << result.work.crossover_gene_choices << ",\n"
           << "    \"mutation_gene_trials\": "
           << result.work.mutation_gene_trials << ",\n"
           << "    \"mutation_events\": " << result.work.mutation_events
           << ",\n"
           << "    \"perturbed_individuals\": "
           << result.work.perturbed_individuals << ",\n"
           << "    \"power_law_gene_steps\": "
           << result.work.power_law_gene_steps << ",\n"
           << "    \"duplicate_repairs\": "
           << result.work.duplicate_repairs << "\n"
           << "  }\n"
           << "}";
    return output.str();
}

}  // namespace ppga
