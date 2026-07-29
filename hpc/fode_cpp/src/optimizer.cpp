/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: canonical FODE state machine
Paper title and DOI: A State-of-the-Art Fractional Order-Driven Differential
Evolution for Wind Farm Layout Optimization; 10.3390/math13020282
Paper/source basis: Eqs. 12-15, Algorithm 1, and archived FODE.m
Public asset: not publicly redistributed; immutable hash in paper ledger
Missing/conflicts: D=80 population crossing is disclosed and frozen
Reconstruction: generation-snapshot, counter-random, exact-FES C++ state machine
Method/problem semantic IDs: fode_e0_physical_fes;
fode_wflop_e0_legacy_v1
Controlling contract and claim boundary:
shared/contracts/paper_implementation_ledger.tsv; semantic reproduction without
MATLAB random-stream identity
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "fode/optimizer.hpp"

#include "fode/evaluator.hpp"
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

#include <omp.h>

namespace fode {
namespace {

constexpr double kFractionalA = 0.8;
constexpr double kPBestRate = 0.11;
constexpr double kArchiveRate = 1.4;
constexpr int kMemorySize = 5;

enum RandomPhase : std::uint64_t {
    kInitialKey = 10,
    kMemoryIndex = 20,
    kNormalCr = 21,
    kCauchySf = 22,
    kR1 = 23,
    kR2 = 24,
    kPBest = 25,
    kCross = 26,
    kJrand = 27,
    kRepairBound = 28,
    kRepairFill = 29,
    kArchiveTrim = 30,
    kLocalK = 40,
    kLocalMix = 41,
    kLocalMutationFlag = 42,
    kLocalMutationValue = 43,
};

std::size_t offset(int row, int col, int dimension) {
    return static_cast<std::size_t>(row * dimension + col);
}

template <typename Task>
void execute_fode_stage(
    PersistentExecutor& executor,
    int begin,
    int end,
    int minimum_parallel_items,
    Task&& task
) {
    if (end - begin < minimum_parallel_items) {
        for (int index = begin; index < end; ++index) {
            task(index);
        }
        return;
    }
    executor.parallel_for(begin, end, std::forward<Task>(task));
}

std::vector<char> unavailable_mask(const CaseData& data) {
    std::vector<char> mask(
        static_cast<std::size_t>(data.rows * data.cols),
        0
    );
    for (const int cell_1based : data.unavailable_cells_1based) {
        if (cell_1based < 1 || cell_1based > data.rows * data.cols) {
            throw std::runtime_error(
                "case has an out-of-range unavailable cell"
            );
        }
        mask[static_cast<std::size_t>(cell_1based - 1)] = 1;
    }
    return mask;
}

std::vector<double> initialize_population(
    int population_size,
    const CaseData& data,
    const CounterRng& rng,
    PersistentExecutor& executor
) {
    const int dimension = data.turbine_count;
    const int grid_dimension = data.rows * data.cols;
    const std::vector<char> blocked = unavailable_mask(data);
    std::vector<int> available;
    for (int cell = 0; cell < grid_dimension; ++cell) {
        if (blocked[static_cast<std::size_t>(cell)] == 0) {
            available.push_back(cell + 1);
        }
    }
    if (static_cast<int>(available.size()) < dimension) {
        throw std::runtime_error("not enough available cells");
    }

    std::vector<double> population(
        static_cast<std::size_t>(population_size * dimension)
    );
    execute_fode_stage(
        executor,
        0,
        population_size,
        8,
        [&](int individual) {
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(available.size());
        for (std::size_t cell = 0; cell < available.size(); ++cell) {
            keyed.emplace_back(
                rng.uniform(
                    0,
                    kInitialKey,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(cell)
                ),
                available[cell]
            );
        }
        std::sort(
            keyed.begin(),
            keyed.end(),
            [](const auto& lhs, const auto& rhs) {
                if (lhs.first != rhs.first) {
                    return lhs.first < rhs.first;
                }
                return lhs.second < rhs.second;
            }
        );
        std::vector<int> selected;
        selected.reserve(static_cast<std::size_t>(dimension));
        for (int d = 0; d < dimension; ++d) {
            selected.push_back(keyed[static_cast<std::size_t>(d)].second);
        }
        std::sort(selected.begin(), selected.end());
        for (int d = 0; d < dimension; ++d) {
            population[offset(individual, d, dimension)] =
                static_cast<double>(selected[static_cast<std::size_t>(d)]);
        }
        }
    );
    return population;
}

void repair_population(
    std::vector<double>& candidates,
    int population_size,
    const CaseData& data,
    const CounterRng& rng,
    std::uint64_t generation,
    PersistentExecutor& executor,
    std::uint64_t phase_bias = 0
) {
    const int dimension = data.turbine_count;
    const int grid_dimension = data.rows * data.cols;
    const std::vector<char> blocked = unavailable_mask(data);

    execute_fode_stage(
        executor,
        0,
        population_size,
        128,
        [&](int individual) {
        std::vector<char> occupied = blocked;
        for (int d = 0; d < dimension; ++d) {
            int cell = static_cast<int>(
                std::ceil(candidates[offset(individual, d, dimension)])
            );
            if (cell < 1 || cell > grid_dimension) {
                cell = rng.integer(
                    1,
                    grid_dimension + 1,
                    generation,
                    kRepairBound + phase_bias,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(d)
                );
            }
            if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
            }
        }

        int placed = static_cast<int>(std::count(
            occupied.begin(), occupied.end(), static_cast<char>(1)
        )) - static_cast<int>(std::count(
            blocked.begin(), blocked.end(), static_cast<char>(1)
        ));
        std::uint64_t draw = 0;
        while (placed < dimension) {
            const int cell = rng.integer(
                1,
                grid_dimension + 1,
                generation,
                kRepairFill + phase_bias,
                static_cast<std::uint64_t>(individual),
                0,
                draw++
            );
            const std::size_t index = static_cast<std::size_t>(cell - 1);
            if (occupied[index] == 0) {
                occupied[index] = 1;
                ++placed;
            }
            if (draw > static_cast<std::uint64_t>(10000 * grid_dimension)) {
                for (
                    std::size_t fallback = 0;
                    fallback < occupied.size();
                    ++fallback
                ) {
                    if (occupied[fallback] == 0) {
                        occupied[fallback] = 1;
                        ++placed;
                        break;
                    }
                }
            }
        }

        int out = 0;
        for (int cell = 0; cell < grid_dimension && out < dimension; ++cell) {
            if (occupied[static_cast<std::size_t>(cell)] != 0
                && blocked[static_cast<std::size_t>(cell)] == 0) {
                candidates[offset(individual, out, dimension)] =
                    static_cast<double>(cell + 1);
                ++out;
            }
        }
        }
    );
}

double fractional_value(
    double fractional_order,
    double current,
    const std::vector<double>& h1,
    const std::vector<double>& h2,
    const std::vector<double>& h3,
    const std::vector<double>& h4,
    std::size_t index,
    std::uint64_t iteration
) {
    if (h1.empty()) {
        return current;
    }
    const double a = fractional_order;
    double result = a * current;
    if (iteration >= 2) {
        result += 0.5 * a * (1.0 - a) * h1[index];
    }
    if (iteration >= 3 && !h2.empty()) {
        result += (1.0 / 6.0) * a * (1.0 - a)
            * (2.0 - a) * h2[index];
    }
    if (iteration >= 4 && !h3.empty()) {
        result += (1.0 / 24.0) * a * (1.0 - a)
            * (2.0 - a) * (3.0 - a) * h3[index];
    }
    if (iteration >= 5 && !h4.empty()) {
        result += (1.0 / 120.0) * a * (1.0 - a)
            * (2.0 - a) * (3.0 - a)
            * (4.0 - a) * h4[index];
    }
    return result;
}

bool equal_row(
    const std::vector<double>& matrix,
    int lhs,
    const std::vector<double>& row,
    int dimension
) {
    for (int d = 0; d < dimension; ++d) {
        if (matrix[offset(lhs, d, dimension)]
            != row[static_cast<std::size_t>(d)]) {
            return false;
        }
    }
    return true;
}

void update_archive(
    std::vector<double>& archive,
    const std::vector<double>& parents,
    const std::vector<char>& accepted,
    int population_size,
    int dimension,
    int capacity,
    const CounterRng& rng,
    std::uint64_t generation
) {
    if (capacity <= 0) {
        archive.clear();
        return;
    }
    std::vector<double> combined = archive;
    int rows = static_cast<int>(combined.size()) / dimension;
    for (int individual = 0; individual < population_size; ++individual) {
        if (accepted[static_cast<std::size_t>(individual)] == 0) {
            continue;
        }
        std::vector<double> row(static_cast<std::size_t>(dimension));
        for (int d = 0; d < dimension; ++d) {
            row[static_cast<std::size_t>(d)] =
                parents[offset(individual, d, dimension)];
        }
        bool duplicate = false;
        for (int existing = 0; existing < rows; ++existing) {
            if (equal_row(combined, existing, row, dimension)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            combined.insert(combined.end(), row.begin(), row.end());
            ++rows;
        }
    }
    if (rows <= capacity) {
        archive = std::move(combined);
        return;
    }
    std::vector<int> order(static_cast<std::size_t>(rows));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&rng, generation](int lhs, int rhs) {
        const double lkey = rng.uniform(
            generation, kArchiveTrim,
            static_cast<std::uint64_t>(lhs)
        );
        const double rkey = rng.uniform(
            generation, kArchiveTrim,
            static_cast<std::uint64_t>(rhs)
        );
        return lkey == rkey ? lhs < rhs : lkey < rkey;
    });
    archive.assign(
        static_cast<std::size_t>(capacity * dimension),
        0.0
    );
    for (int row = 0; row < capacity; ++row) {
        for (int d = 0; d < dimension; ++d) {
            archive[offset(row, d, dimension)] =
                combined[offset(order[static_cast<std::size_t>(row)], d, dimension)];
        }
    }
}

void retain_rows(
    std::vector<double>& matrix,
    const std::vector<int>& keep,
    int old_rows,
    int dimension
) {
    if (matrix.empty()) {
        return;
    }
    if (static_cast<int>(matrix.size()) != old_rows * dimension) {
        throw std::runtime_error("row-aligned FODE state has drifted");
    }
    std::vector<double> reduced(
        static_cast<std::size_t>(keep.size() * dimension)
    );
    for (std::size_t row = 0; row < keep.size(); ++row) {
        for (int d = 0; d < dimension; ++d) {
            reduced[offset(
                static_cast<int>(row), d, dimension
            )] = matrix[offset(keep[row], d, dimension)];
        }
    }
    matrix = std::move(reduced);
}

std::vector<int> survivor_rows(
    const std::vector<double>& fitness,
    int target
) {
    const int rows = static_cast<int>(fitness.size());
    std::vector<int> ranking(static_cast<std::size_t>(rows));
    std::iota(ranking.begin(), ranking.end(), 0);
    std::stable_sort(
        ranking.begin(),
        ranking.end(),
        [&fitness](int lhs, int rhs) {
            return fitness[static_cast<std::size_t>(lhs)]
                > fitness[static_cast<std::size_t>(rhs)];
        }
    );
    std::vector<char> retain(static_cast<std::size_t>(rows), 0);
    for (int i = 0; i < target; ++i) {
        retain[static_cast<std::size_t>(
            ranking[static_cast<std::size_t>(i)]
        )] = 1;
    }
    std::vector<int> keep;
    keep.reserve(static_cast<std::size_t>(target));
    for (int row = 0; row < rows; ++row) {
        if (retain[static_cast<std::size_t>(row)] != 0) {
            keep.push_back(row);
        }
    }
    return keep;
}

}  // namespace

RunResult optimize_fode_hpc_controlled(
    const CaseData& data,
    const RunConfig& config,
    FractionalOrderController& controller
) {
    if (config.physical_fes_budget == 0 || config.workers <= 0) {
        throw std::invalid_argument("FES budget and workers must be positive");
    }
    PersistentExecutor executor(config.workers);
    using Clock = std::chrono::steady_clock;
    auto seconds_since = [](Clock::time_point started) {
        return std::chrono::duration<double>(Clock::now() - started).count();
    };
    std::array<double, 17> phases{};
    Clock::time_point phase_started{};
    auto begin_phase = [&]() {
        if (config.profile_phases) {
            phase_started = Clock::now();
        }
    };
    auto end_phase = [&](std::size_t phase) {
        if (config.profile_phases) {
            phases[phase] += seconds_since(phase_started);
        }
    };
    const auto total_started = std::chrono::steady_clock::now();
    begin_phase();
    const CounterRng rng(config.seed);
    const int dimension = data.turbine_count;
    const int grid_dimension = data.rows * data.cols;
    int population_size = std::abs(77 - dimension);
    population_size = std::max(population_size, 3);
    const int initial_population_size = population_size;
    const int minimum_population = std::min(4, population_size);
    const std::uint64_t legacy_fes_budget = config.physical_fes_budget;

    std::vector<double> population = initialize_population(
        population_size, data, rng, executor
    );
    end_phase(0);
    begin_phase();
    Evaluation initial = evaluate_population_hpc(
        population,
        population_size,
        data,
        executor,
        EvaluationDetail::TotalOnly,
        EvaluationSchedule::GranularityAware
    );
    end_phase(1);
    std::uint64_t physical_fes =
        static_cast<std::uint64_t>(population_size);
    double evaluator_seconds = initial.elapsed_seconds;
    int observed_workers = initial.observed_workers;
    std::vector<double> fitness = initial.fitness;
    double controller_best_fitness = *std::max_element(
        fitness.begin(),
        fitness.end()
    );

    // Source identity: initial candidates do not enter the search-best state.
    double best_fitness = 0.0;
    const int initial_best = static_cast<int>(
        std::distance(
            fitness.begin(),
            std::max_element(fitness.begin(), fitness.end())
        )
    );
    std::vector<int> best_layout(static_cast<std::size_t>(dimension));
    for (int d = 0; d < dimension; ++d) {
        best_layout[static_cast<std::size_t>(d)] = static_cast<int>(
            std::llround(population[offset(initial_best, d, dimension)])
        );
    }

    std::vector<double> memory_sf(kMemorySize, 0.5);
    std::vector<double> memory_cr(kMemorySize, 0.5);
    int memory_position = 0;
    int archive_capacity = static_cast<int>(
        std::llround(kArchiveRate * static_cast<double>(population_size))
    );
    std::vector<double> archive;

    std::vector<double> dp_h1, dp_h2, dp_h3, dp_h4;
    std::vector<double> da_h1, da_h2, da_h3, da_h4;

    std::uint64_t legacy_fes = 0;
    std::uint64_t iteration = 1;
    std::uint64_t generation = 0;

    while (legacy_fes < legacy_fes_budget
           && physical_fes < config.physical_fes_budget
           && (
               config.maximum_generations == 0
               || generation < config.maximum_generations
           )) {
        begin_phase();
        ++generation;
        const double fractional_order = std::clamp(
            controller.begin_generation_with_fes(
                generation,
                controller_best_fitness,
                physical_fes
            ),
            0.0,
            1.0
        );
        const int old_population_size = population_size;
        const std::vector<double> parent = population;

        std::vector<int> ranking(
            static_cast<std::size_t>(old_population_size)
        );
        std::iota(ranking.begin(), ranking.end(), 0);
        std::stable_sort(
            ranking.begin(),
            ranking.end(),
            [&fitness](int lhs, int rhs) {
                return fitness[static_cast<std::size_t>(lhs)]
                    > fitness[static_cast<std::size_t>(rhs)];
            }
        );

        const int archive_rows =
            static_cast<int>(archive.size()) / dimension;
        std::vector<double> all = parent;
        all.insert(all.end(), archive.begin(), archive.end());
        const int all_rows = old_population_size + archive_rows;
        end_phase(2);

        begin_phase();
        std::vector<int> r1(static_cast<std::size_t>(old_population_size));
        std::vector<int> r2(static_cast<std::size_t>(old_population_size));
        std::vector<int> pbest_row(
            static_cast<std::size_t>(old_population_size)
        );
        std::vector<int> jrand(static_cast<std::size_t>(old_population_size));
        std::vector<double> cr(
            static_cast<std::size_t>(old_population_size)
        );
        std::vector<double> sf(
            static_cast<std::size_t>(old_population_size)
        );
        const int pbest_count = std::max(
            static_cast<int>(std::llround(
                kPBestRate * static_cast<double>(old_population_size)
            )),
            2
        );

        execute_fode_stage(
            executor,
            0,
            old_population_size,
            128,
            [&](int individual) {
            const int memory_index = rng.integer(
                0, kMemorySize, generation, kMemoryIndex,
                static_cast<std::uint64_t>(individual)
            );
            const double mu_sf =
                memory_sf[static_cast<std::size_t>(memory_index)];
            const double mu_cr =
                memory_cr[static_cast<std::size_t>(memory_index)];
            double sampled_cr = mu_cr + 0.1 * rng.normal(
                generation, kNormalCr,
                static_cast<std::uint64_t>(individual)
            );
            if (mu_cr == -1.0) {
                sampled_cr = 0.0;
            }
            cr[static_cast<std::size_t>(individual)] =
                std::clamp(sampled_cr, 0.0, 1.0);

            std::uint64_t draw = 0;
            double sampled_sf = 0.0;
            do {
                const double u = rng.uniform(
                    generation, kCauchySf,
                    static_cast<std::uint64_t>(individual), 0, draw++
                );
                sampled_sf = mu_sf
                    + 0.1 * std::tan(std::numbers::pi * (u - 0.5));
            } while (sampled_sf <= 0.0);
            sf[static_cast<std::size_t>(individual)] =
                std::min(sampled_sf, 1.0);

            draw = 0;
            int choice = individual;
            while (choice == individual) {
                choice = rng.integer(
                    0, old_population_size, generation, kR1,
                    static_cast<std::uint64_t>(individual), 0, draw++
                );
            }
            r1[static_cast<std::size_t>(individual)] = choice;

            draw = 0;
            int second = individual;
            while (second == individual || second == choice) {
                second = rng.integer(
                    0, all_rows, generation, kR2,
                    static_cast<std::uint64_t>(individual), 0, draw++
                );
            }
            r2[static_cast<std::size_t>(individual)] = second;

            const int pbest_rank = rng.integer(
                0, pbest_count, generation, kPBest,
                static_cast<std::uint64_t>(individual)
            );
            pbest_row[static_cast<std::size_t>(individual)] =
                ranking[static_cast<std::size_t>(pbest_rank)];
            jrand[static_cast<std::size_t>(individual)] = rng.integer(
                0, dimension, generation, kJrand,
                static_cast<std::uint64_t>(individual)
            );
            }
        );
        end_phase(3);

        begin_phase();
        std::vector<double> dp_current(
            static_cast<std::size_t>(old_population_size * dimension)
        );
        std::vector<double> da_current(
            static_cast<std::size_t>(old_population_size * dimension)
        );
        std::vector<double> candidate(
            static_cast<std::size_t>(old_population_size * dimension)
        );

        execute_fode_stage(
            executor,
            0,
            old_population_size * dimension,
            1024,
            [&](int task) {
                const int individual = task / dimension;
                const int d = task - individual * dimension;
                const std::size_t index = offset(individual, d, dimension);
                const double dp =
                    parent[offset(
                        pbest_row[static_cast<std::size_t>(individual)],
                        d,
                        dimension
                    )] - parent[index];
                const double da =
                    parent[offset(
                        r1[static_cast<std::size_t>(individual)],
                        d,
                        dimension
                    )] - all[offset(
                        r2[static_cast<std::size_t>(individual)],
                        d,
                        dimension
                    )];
                dp_current[index] = dp;
                da_current[index] = da;
                const double fo_dp = fractional_value(
                    fractional_order,
                    dp,
                    dp_h1,
                    dp_h2,
                    dp_h3,
                    dp_h4,
                    index,
                    iteration
                );
                const double fo_da = fractional_value(
                    fractional_order,
                    da,
                    da_h1,
                    da_h2,
                    da_h3,
                    da_h4,
                    index,
                    iteration
                );
                double value = parent[index]
                    + sf[static_cast<std::size_t>(individual)]
                        * (fo_dp + fo_da);
                const bool copy_parent =
                    rng.uniform(
                        generation,
                        kCross,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(d)
                    ) > cr[static_cast<std::size_t>(individual)]
                    && d != jrand[static_cast<std::size_t>(individual)];
                if (copy_parent) {
                    value = parent[index];
                }
                candidate[index] = value;
            }
        );
        end_phase(4);
        begin_phase();
        repair_population(
            candidate,
            old_population_size,
            data,
            rng,
            generation,
            executor
        );
        end_phase(5);

        begin_phase();
        const std::uint64_t remaining =
            config.physical_fes_budget - physical_fes;
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(old_population_size),
            remaining
        ));
        std::vector<double> evaluated_candidate(
            candidate.begin(),
            candidate.begin()
                + static_cast<std::ptrdiff_t>(completed * dimension)
        );
        Evaluation children = evaluate_population_hpc(
            evaluated_candidate,
            completed,
            data,
            executor,
            EvaluationDetail::TotalOnly,
            EvaluationSchedule::GranularityAware
        );
        end_phase(6);
        evaluator_seconds += children.elapsed_seconds;
        observed_workers = std::max(
            observed_workers, children.observed_workers
        );
        physical_fes += static_cast<std::uint64_t>(completed);

        begin_phase();
        const auto child_best_it = std::max_element(
            children.fitness.begin(), children.fitness.end()
        );
        const double child_best = *child_best_it;
        double pfit = -std::numeric_limits<double>::infinity();
        if (best_fitness != 0.0) {
            pfit = (best_fitness - child_best) / best_fitness;
        }
        for (int individual = 0; individual < completed; ++individual) {
            const double value =
                children.fitness[static_cast<std::size_t>(individual)];
            if (value > best_fitness) {
                best_fitness = value;
                for (int d = 0; d < dimension; ++d) {
                    best_layout[static_cast<std::size_t>(d)] = static_cast<int>(
                        std::llround(candidate[offset(individual, d, dimension)])
                    );
                }
            }
            controller_best_fitness = std::max(
                controller_best_fitness,
                value
            );
        }
        end_phase(7);
        if (completed < old_population_size) {
            break;
        }

        begin_phase();
        std::vector<char> accepted(
            static_cast<std::size_t>(old_population_size),
            0
        );
        std::vector<double> difference(
            static_cast<std::size_t>(old_population_size),
            0.0
        );
        execute_fode_stage(
            executor,
            0,
            old_population_size,
            128,
            [&](int individual) {
            const double child =
                children.fitness[static_cast<std::size_t>(individual)];
            const double previous =
                fitness[static_cast<std::size_t>(individual)];
            difference[static_cast<std::size_t>(individual)] =
                std::abs(previous - child);
            if (child > previous) {
                accepted[static_cast<std::size_t>(individual)] = 1;
            }
            }
        );
        end_phase(8);
        begin_phase();
        update_archive(
            archive,
            parent,
            accepted,
            old_population_size,
            dimension,
            archive_capacity,
            rng,
            generation
        );
        end_phase(9);

        begin_phase();
        execute_fode_stage(
            executor,
            0,
            old_population_size,
            128,
            [&](int individual) {
            if (accepted[static_cast<std::size_t>(individual)] != 0) {
                fitness[static_cast<std::size_t>(individual)] =
                    children.fitness[static_cast<std::size_t>(individual)];
                for (int d = 0; d < dimension; ++d) {
                    population[offset(individual, d, dimension)] =
                        candidate[offset(individual, d, dimension)];
                }
            }
            }
        );

        double difference_sum = 0.0;
        double weighted_f = 0.0;
        double weighted_f2 = 0.0;
        double weighted_cr = 0.0;
        double weighted_cr2 = 0.0;
        double max_good_cr = 0.0;
        for (int individual = 0; individual < old_population_size; ++individual) {
            if (accepted[static_cast<std::size_t>(individual)] == 0) {
                continue;
            }
            const double weight =
                difference[static_cast<std::size_t>(individual)];
            const double f_value = sf[static_cast<std::size_t>(individual)];
            const double cr_value = cr[static_cast<std::size_t>(individual)];
            difference_sum += weight;
            weighted_f += weight * f_value;
            weighted_f2 += weight * f_value * f_value;
            weighted_cr += weight * cr_value;
            weighted_cr2 += weight * cr_value * cr_value;
            max_good_cr = std::max(max_good_cr, cr_value);
        }
        if (difference_sum > 0.0 && weighted_f > 0.0) {
            memory_sf[static_cast<std::size_t>(memory_position)] =
                weighted_f2 / weighted_f;
            if (max_good_cr == 0.0
                || memory_cr[static_cast<std::size_t>(memory_position)]
                    == -1.0) {
                memory_cr[static_cast<std::size_t>(memory_position)] = -1.0;
            } else if (weighted_cr > 0.0) {
                memory_cr[static_cast<std::size_t>(memory_position)] =
                    weighted_cr2 / weighted_cr;
            }
            memory_position = (memory_position + 1) % kMemorySize;
        }
        end_phase(10);

        begin_phase();
        dp_h4 = std::move(dp_h3);
        dp_h3 = std::move(dp_h2);
        dp_h2 = std::move(dp_h1);
        dp_h1 = std::move(dp_current);
        da_h4 = std::move(da_h3);
        da_h3 = std::move(da_h2);
        da_h2 = std::move(da_h1);
        da_h1 = std::move(da_current);
        end_phase(11);

        begin_phase();
        int planned_population = static_cast<int>(std::llround(
            static_cast<double>(initial_population_size)
            + (
                static_cast<double>(
                    minimum_population - initial_population_size
                ) / static_cast<double>(legacy_fes_budget)
            ) * static_cast<double>(legacy_fes)
        ));
        planned_population = std::max(
            minimum_population,
            planned_population
        );
        if (population_size > planned_population) {
            const std::vector<int> keep =
                survivor_rows(fitness, planned_population);
            retain_rows(
                population, keep, old_population_size, dimension
            );
            retain_rows(dp_h1, keep, old_population_size, dimension);
            retain_rows(dp_h2, keep, old_population_size, dimension);
            retain_rows(dp_h3, keep, old_population_size, dimension);
            retain_rows(dp_h4, keep, old_population_size, dimension);
            retain_rows(da_h1, keep, old_population_size, dimension);
            retain_rows(da_h2, keep, old_population_size, dimension);
            retain_rows(da_h3, keep, old_population_size, dimension);
            retain_rows(da_h4, keep, old_population_size, dimension);
            std::vector<double> reduced_fitness;
            reduced_fitness.reserve(keep.size());
            for (const int row : keep) {
                reduced_fitness.push_back(
                    fitness[static_cast<std::size_t>(row)]
                );
            }
            fitness = std::move(reduced_fitness);
            population_size = planned_population;
            archive_capacity = static_cast<int>(std::llround(
                kArchiveRate * static_cast<double>(population_size)
            ));
            if (static_cast<int>(archive.size()) / dimension
                > archive_capacity) {
                std::vector<char> none(
                    static_cast<std::size_t>(population_size), 0
                );
                std::vector<double> empty_parent(
                    static_cast<std::size_t>(population_size * dimension)
                );
                update_archive(
                    archive, empty_parent, none, population_size, dimension,
                    archive_capacity, rng, generation + 1000000
                );
            }
        }
        end_phase(12);

        begin_phase();
        const std::uint64_t legacy_before_population = legacy_fes;
        legacy_fes += static_cast<std::uint64_t>(population_size);
        iteration += legacy_fes / 120 - legacy_before_population / 120;
        if (physical_fes >= config.physical_fes_budget) {
            end_phase(13);
            break;
        }
        end_phase(13);

        if (pfit < 0.05) {
            begin_phase();
            std::vector<double> local(static_cast<std::size_t>(dimension));
            // MATLAB reuses loop variable i in the post-reduction survival
            // loop, so pbest(i,:) refers to the current population-size row.
            const int source_last = population_size - 1;
            execute_fode_stage(
                executor,
                0,
                dimension,
                256,
                [&](int d) {
                const int k = rng.integer(
                    0,
                    population_size,
                    generation,
                    kLocalK,
                    0,
                    static_cast<std::uint64_t>(d)
                );
                double value = 0.0;
                if (fitness[0] > fitness[static_cast<std::size_t>(k)]) {
                    const double mix = rng.uniform(
                        generation,
                        kLocalMix,
                        0,
                        static_cast<std::uint64_t>(d)
                    );
                    value = mix * parent[offset(
                        pbest_row[static_cast<std::size_t>(source_last)],
                        d,
                        dimension
                    )] + (1.0 - mix)
                        * static_cast<double>(
                            best_layout[static_cast<std::size_t>(d)]
                        );
                } else {
                    value = parent[offset(
                        pbest_row[static_cast<std::size_t>(k)],
                        d,
                        dimension
                    )];
                }
                if (rng.uniform(
                        generation,
                        kLocalMutationFlag,
                        0,
                        static_cast<std::uint64_t>(d)
                    ) < 0.01) {
                    value = static_cast<double>(grid_dimension)
                        + rng.uniform(
                            generation,
                            kLocalMutationValue,
                            0,
                            static_cast<std::uint64_t>(d)
                        ) * static_cast<double>(1 - grid_dimension);
                }
                local[static_cast<std::size_t>(d)] = value;
                }
            );
            repair_population(
                local,
                1,
                data,
                rng,
                generation,
                executor,
                100
            );
            end_phase(14);
            begin_phase();
            Evaluation local_result = evaluate_population_hpc(
                local,
                1,
                data,
                executor,
                EvaluationDetail::TotalOnly,
                EvaluationSchedule::GranularityAware
            );
            end_phase(15);
            evaluator_seconds += local_result.elapsed_seconds;
            observed_workers = std::max(
                observed_workers, local_result.observed_workers
            );
            ++physical_fes;
            if (local_result.fitness[0] > best_fitness) {
                best_fitness = local_result.fitness[0];
                for (int d = 0; d < dimension; ++d) {
                    best_layout[static_cast<std::size_t>(d)] =
                        static_cast<int>(std::llround(
                            local[static_cast<std::size_t>(d)]
                        ));
                }
            }
            controller_best_fitness = std::max(
                controller_best_fitness,
                local_result.fitness[0]
            );
            const std::uint64_t legacy_before_local = legacy_fes;
            ++legacy_fes;
            iteration += legacy_fes / 120 - legacy_before_local / 120;
        }
        controller.end_generation(generation, controller_best_fitness);
    }
    controller.finish(controller_best_fitness);

    begin_phase();
    RunResult result;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = physical_fes;
    result.generations = generation;
    result.initial_population = initial_population_size;
    result.final_population = population_size;
    result.requested_workers = config.workers;
    result.observed_workers = observed_workers;
    result.best_expected_power_kw = best_fitness;
    result.best_layout_1based = std::move(best_layout);
    result.evaluator_seconds = evaluator_seconds;
    end_phase(16);
    result.profiling_enabled = config.profile_phases;
    result.phase_seconds = phases;
    result.total_seconds = std::chrono::duration<double>(
        Clock::now() - total_started
    ).count();
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - evaluator_seconds);
    return result;
}

namespace {

class FixedFractionalOrderController final
    : public FractionalOrderController {
public:
    double begin_generation(std::uint64_t, double) override {
        return kFractionalA;
    }

    void finish(double) override {
    }
};

}  // namespace

RunResult optimize_fode_hpc(const CaseData& data, const RunConfig& config) {
    FixedFractionalOrderController controller;
    return optimize_fode_hpc_controlled(data, config, controller);
}

}  // namespace fode
