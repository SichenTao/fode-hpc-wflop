/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: legacy multipaper scalar algorithm module pending
per-method physical split
Paper title and DOI: multipaper; exact mapping is authoritative in
shared/contracts/paper_implementation_ledger.tsv
Paper/source basis: target PDFs, archived MATLAB assets, and explicit
paper-derived completions
Public asset: per-method URLs/revisions/licenses are in docs/source-dossiers
Missing/conflicts: CEDE, MS-SHADE, AGPSO, CGPSO, and HGPSO variants remain
separate registered identities; no source/paper result pooling
Reconstruction: pure C++ scalar methods over canonical evaluators
Method/problem semantic IDs: registry_defined; registry_defined
Controlling contract and claim boundary:
shared/contracts/paper_implementation_ledger.tsv; each result is bounded by its
registered provenance and semantic ID
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "wflop/algorithms.hpp"
#include "wflop/rlfode_reconstruction.hpp"

#include "fode/evaluator.hpp"
#include "fode/executor.hpp"
#include "fode/optimizer.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wflop {
namespace {

using Matrix = std::vector<double>;
using Clock = std::chrono::steady_clock;

std::size_t at(int row, int col, int dimension) {
    return static_cast<std::size_t>(row * dimension + col);
}

std::uint64_t algorithm_salt(const std::string& algorithm) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const unsigned char byte : algorithm) {
        value ^= static_cast<std::uint64_t>(byte);
        value *= 1099511628211ULL;
    }
    return value;
}

std::vector<char> blocked_mask(const fode::CaseData& data) {
    std::vector<char> blocked(
        static_cast<std::size_t>(data.rows * data.cols),
        0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    return blocked;
}

std::vector<int> available_cells(const fode::CaseData& data) {
    const auto blocked = blocked_mask(data);
    std::vector<int> available;
    for (int cell = 1; cell <= data.rows * data.cols; ++cell) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            available.push_back(cell);
        }
    }
    return available;
}

Matrix initialize_population(
    int population_size,
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    fode::PersistentExecutor& executor,
    std::uint64_t phase
) {
    const int dimension = data.turbine_count;
    const auto available = available_cells(data);
    Matrix population(
        static_cast<std::size_t>(population_size * dimension),
        0.0
    );
    executor.parallel_for(0, population_size, [&](int individual) {
        std::vector<std::pair<double, int>> keys;
        keys.reserve(available.size());
        for (std::size_t index = 0; index < available.size(); ++index) {
            keys.emplace_back(
                rng.uniform(
                    0,
                    phase,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(index)
                ),
                available[index]
            );
        }
        std::stable_sort(keys.begin(), keys.end());
        std::vector<int> row;
        row.reserve(static_cast<std::size_t>(dimension));
        for (int d = 0; d < dimension; ++d) {
            row.push_back(keys[static_cast<std::size_t>(d)].second);
        }
        std::sort(row.begin(), row.end());
        for (int d = 0; d < dimension; ++d) {
            population[at(individual, d, dimension)] =
                static_cast<double>(row[static_cast<std::size_t>(d)]);
        }
    });
    return population;
}

void repair_population(
    Matrix& population,
    int rows,
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    fode::PersistentExecutor& executor,
    std::uint64_t generation,
    std::uint64_t phase
) {
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    executor.parallel_for(0, rows, [&](int row) {
        std::vector<char> used = blocked;
        for (int d = 0; d < dimension; ++d) {
            int cell = static_cast<int>(
                std::ceil(population[at(row, d, dimension)])
            );
            if (cell < 1 || cell > grid) {
                cell = rng.integer(
                    1,
                    grid + 1,
                    generation,
                    phase,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
            }
            if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
                used[static_cast<std::size_t>(cell - 1)] = 1;
            }
        }
        int placed = 0;
        for (int cell = 0; cell < grid; ++cell) {
            if (used[static_cast<std::size_t>(cell)] != 0
                && blocked[static_cast<std::size_t>(cell)] == 0) {
                ++placed;
            }
        }
        std::uint64_t draw = 0;
        while (placed < dimension) {
            const int cell = rng.integer(
                1,
                grid + 1,
                generation,
                phase + 1,
                static_cast<std::uint64_t>(row),
                0,
                draw++
            );
            const std::size_t index = static_cast<std::size_t>(cell - 1);
            if (used[index] == 0) {
                used[index] = 1;
                ++placed;
            }
        }
        int output = 0;
        for (int cell = 0; cell < grid && output < dimension; ++cell) {
            if (used[static_cast<std::size_t>(cell)] != 0
                && blocked[static_cast<std::size_t>(cell)] == 0) {
                population[at(row, output++, dimension)] =
                    static_cast<double>(cell + 1);
            }
        }
    });
}

void copy_row(
    Matrix& target,
    int target_row,
    const Matrix& source,
    int source_row,
    int dimension
) {
    std::copy_n(
        source.begin() + static_cast<std::ptrdiff_t>(source_row * dimension),
        dimension,
        target.begin() + static_cast<std::ptrdiff_t>(target_row * dimension)
    );
}

std::vector<int> stable_rank_descending(const std::vector<double>& fitness) {
    std::vector<int> order(fitness.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
        return fitness[static_cast<std::size_t>(lhs)]
            > fitness[static_cast<std::size_t>(rhs)];
    });
    return order;
}

struct Runtime {
    explicit Runtime(const RunConfig& config)
        : executor(config.workers),
          rng(config.seed ^ algorithm_salt(config.algorithm_id)),
          budget(config.physical_fes_budget) {}

    fode::PersistentExecutor executor;
    fode::CounterRng rng;
    std::uint64_t budget = 0;
    std::uint64_t fes = 0;
    std::uint64_t generations = 0;
    double evaluator_seconds = 0.0;
    double best = -std::numeric_limits<double>::infinity();
    std::vector<int> best_layout;

    fode::Evaluation evaluate(
        const Matrix& population,
        int rows,
        const fode::CaseData& data,
        fode::EvaluationDetail detail,
        bool track_best = true
    ) {
        if (rows <= 0 || fes >= budget) {
            throw std::runtime_error("invalid or exhausted evaluation batch");
        }
        const int dimension = data.turbine_count;
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(rows),
            budget - fes
        ));
        Matrix prefix(
            population.begin(),
            population.begin()
                + static_cast<std::ptrdiff_t>(completed * dimension)
        );
        fode::Evaluation result = fode::evaluate_population_hpc(
            prefix,
            completed,
            data,
            executor,
            detail
        );
        evaluator_seconds += result.elapsed_seconds;
        if (track_best) {
            for (int row = 0; row < completed; ++row) {
                const double value =
                    result.fitness[static_cast<std::size_t>(row)];
                if (value > best) {
                    best = value;
                    best_layout.resize(static_cast<std::size_t>(dimension));
                    for (int d = 0; d < dimension; ++d) {
                        best_layout[static_cast<std::size_t>(d)] =
                            static_cast<int>(std::llround(
                                population[at(row, d, dimension)]
                            ));
                    }
                }
            }
        }
        fes += static_cast<std::uint64_t>(completed);
        return result;
    }
};

RunResult finish_result(
    const fode::CaseData& data,
    const RunConfig& config,
    const Runtime& runtime,
    int initial_population,
    int final_population,
    Clock::time_point started,
    const std::string& pso_semantics = {}
) {
    RunResult result;
    result.algorithm_id = config.algorithm_id;
    std::string upper = config.algorithm_id;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](char value) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    });
    result.method_id = upper + "_CPP_HPC_FULL";
    const auto& identity = algorithm_descriptor(config.algorithm_id);
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_provenance = identity.provenance;
    result.effective_semantics_id = identity.semantics_id;
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = runtime.fes;
    result.inference_physical_fes = runtime.fes;
    result.generations = runtime.generations;
    result.initial_population = initial_population;
    result.final_population = final_population;
    result.requested_workers = config.workers;
    result.observed_workers = runtime.executor.thread_count();
    result.best_expected_power_kw = runtime.best;
    result.best_layout_1based = runtime.best_layout;
    result.total_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    result.evaluator_seconds = runtime.evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - result.evaluator_seconds);
    result.pso_update_semantics = pso_semantics;
    return result;
}

RunResult optimize_ise(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int population_size = static_cast<int>(
        std::min<std::uint64_t>(5, runtime.budget)
    );
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 100
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    const int reduced_dimension = std::max(
        1,
        static_cast<int>(std::llround(static_cast<double>(dimension) / 3.0))
    );
    while (runtime.fes < runtime.budget && population_size >= 4) {
        ++runtime.generations;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        Matrix offspring(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, offspring_count, [&](int individual) {
            std::vector<int> pool;
            for (int candidate = 0; candidate < population_size; ++candidate) {
                if (candidate != individual) {
                    pool.push_back(candidate);
                }
            }
            std::stable_sort(pool.begin(), pool.end(), [&](int lhs, int rhs) {
                return runtime.rng.uniform(
                    runtime.generations, 110,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(lhs)
                ) < runtime.rng.uniform(
                    runtime.generations, 110,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(rhs)
                );
            });
            std::vector<int> coordinates(static_cast<std::size_t>(dimension));
            std::iota(coordinates.begin(), coordinates.end(), 0);
            std::stable_sort(
                coordinates.begin(),
                coordinates.end(),
                [&](int lhs, int rhs) {
                    return runtime.rng.uniform(
                        runtime.generations, 111,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(lhs)
                    ) < runtime.rng.uniform(
                        runtime.generations, 111,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(rhs)
                    );
                }
            );
            copy_row(offspring, individual, population, individual, dimension);
            double radius_squared = 0.0;
            for (int r = 0; r < reduced_dimension; ++r) {
                const int d = coordinates[static_cast<std::size_t>(r)];
                const double difference =
                    population[at(pool[1], d, dimension)]
                    - population[at(pool[2], d, dimension)];
                radius_squared += difference * difference;
            }
            const double radius = std::sqrt(radius_squared);
            std::vector<double> angles(
                static_cast<std::size_t>(std::max(0, reduced_dimension - 1))
            );
            for (std::size_t a = 0; a < angles.size(); ++a) {
                angles[a] = 2.0 * std::numbers::pi * runtime.rng.uniform(
                    runtime.generations, 112,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(a)
                );
            }
            std::vector<double> direction(
                static_cast<std::size_t>(reduced_dimension),
                1.0
            );
            if (reduced_dimension > 1) {
                direction[0] = 1.0;
                for (const double angle : angles) {
                    direction[0] *= std::sin(angle);
                }
                for (int coordinate = 1;
                     coordinate < reduced_dimension - 1;
                     ++coordinate) {
                    double value = std::cos(
                        angles[static_cast<std::size_t>(coordinate - 1)]
                    );
                    for (int a = coordinate;
                         a < reduced_dimension - 1;
                         ++a) {
                        value *= std::sin(
                            angles[static_cast<std::size_t>(a)]
                        );
                    }
                    direction[static_cast<std::size_t>(coordinate)] = value;
                }
                direction.back() = std::cos(angles.back());
            }
            for (int r = 0; r < reduced_dimension; ++r) {
                const int d = coordinates[static_cast<std::size_t>(r)];
                offspring[at(individual, d, dimension)] =
                    population[at(pool[0], d, dimension)]
                    + 10.0 * radius * direction[static_cast<std::size_t>(r)];
            }
        });
        repair_population(
            offspring,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            113
        );
        auto evaluated = runtime.evaluate(
            offspring,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        for (int row = 0; row < offspring_count; ++row) {
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                > fitness[static_cast<std::size_t>(row)]) {
                fitness[static_cast<std::size_t>(row)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
                copy_row(population, row, offspring, row, dimension);
            }
        }
    }
    return finish_result(
        data, config, runtime, population_size, population_size, started
    );
}

struct SvrModel {
    double gamma = 0.0;
    double intercept = 0.0;
    std::vector<std::array<double, 2>> support;
    std::vector<double> coefficient;

    double predict(double x, double y) const {
        double value = intercept;
        for (std::size_t index = 0; index < support.size(); ++index) {
            const double dx = x - support[index][0];
            const double dy = y - support[index][1];
            value += coefficient[index]
                * std::exp(-gamma * (dx * dx + dy * dy));
        }
        return value;
    }
};

SvrModel load_svr(const RunConfig& config, const fode::CaseData& data) {
    const std::filesystem::path path =
        std::filesystem::path(config.sugga_model_root)
        / (data.case_id + ".svr.tsv");
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("missing frozen C++ SUGGA model: " + path.string());
    }
    std::size_t count = 0;
    SvrModel model;
    stream >> count >> model.gamma >> model.intercept;
    model.support.resize(count);
    model.coefficient.resize(count);
    for (std::size_t index = 0; index < count; ++index) {
        stream >> model.support[index][0]
               >> model.support[index][1]
               >> model.coefficient[index];
    }
    if (!stream || count == 0) {
        throw std::runtime_error("invalid frozen C++ SUGGA model: " + path.string());
    }
    return model;
}

void replace_cell(
    Matrix& population,
    int row,
    int dimension,
    int removed,
    int inserted
) {
    for (int d = 0; d < dimension; ++d) {
        if (static_cast<int>(std::llround(population[at(row, d, dimension)]))
            == removed) {
            population[at(row, d, dimension)] = static_cast<double>(inserted);
            break;
        }
    }
    std::sort(
        population.begin() + static_cast<std::ptrdiff_t>(row * dimension),
        population.begin() + static_cast<std::ptrdiff_t>((row + 1) * dimension)
    );
}

RunResult optimize_ga(
    const fode::CaseData& data,
    const RunConfig& config,
    bool guided
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = guided ? 120 : 50;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    const SvrModel model = guided ? load_svr(config, data) : SvrModel{};
    Matrix population = initialize_population(
        population_size,
        data,
        runtime.rng,
        runtime.executor,
        guided ? 200 : 150
    );
    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            runtime.budget - runtime.fes
        ));
        auto evaluated = runtime.evaluate(
            population,
            completed,
            data,
            fode::EvaluationDetail::TotalAndPerTurbine
        );
        if (completed < population_size) {
            break;
        }
        std::vector<double> fitness = evaluated.fitness;
        const auto order = stable_rank_descending(fitness);
        Matrix ranked(population.size());
        std::vector<int> worst_cell(static_cast<std::size_t>(population_size));
        for (int row = 0; row < population_size; ++row) {
            copy_row(ranked, row, population, order[static_cast<std::size_t>(row)], dimension);
            worst_cell[static_cast<std::size_t>(row)] =
                evaluated.turbine_position_order_1based[
                    at(order[static_cast<std::size_t>(row)], 0, dimension)
                ];
        }
        population = std::move(ranked);
        runtime.executor.parallel_for(0, population_size, [&](int row) {
            std::vector<char> occupied = blocked;
            for (int d = 0; d < dimension; ++d) {
                const int cell = static_cast<int>(std::llround(
                    population[at(row, d, dimension)]
                ));
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
            }
            int inserted = 0;
            if (guided && runtime.rng.uniform(
                    runtime.generations, 210,
                    static_cast<std::uint64_t>(row)
                ) >= 0.5) {
                double best_prediction = -std::numeric_limits<double>::infinity();
                std::uint64_t draw = 0;
                for (int candidate = 0; candidate < 5; ++candidate) {
                    int cell = 0;
                    do {
                        cell = runtime.rng.integer(
                            1, grid + 1, runtime.generations, 211,
                            static_cast<std::uint64_t>(row), 0, draw++
                        );
                    } while (occupied[static_cast<std::size_t>(cell - 1)] != 0);
                    const int grid_row = cell / data.cols;
                    const int grid_col = cell - grid_row * data.cols;
                    const double prediction = model.predict(
                        static_cast<double>(grid_col),
                        static_cast<double>(grid_row)
                    );
                    if (prediction > best_prediction) {
                        best_prediction = prediction;
                        inserted = cell;
                    }
                }
            } else {
                std::uint64_t draw = 0;
                do {
                    inserted = runtime.rng.integer(
                        1, grid + 1, runtime.generations, 212,
                        static_cast<std::uint64_t>(row), 0, draw++
                    );
                } while (occupied[static_cast<std::size_t>(inserted - 1)] != 0);
            }
            replace_cell(
                population,
                row,
                dimension,
                worst_cell[static_cast<std::size_t>(row)],
                inserted
            );
        });

        std::vector<int> parents;
        const int elite_count = static_cast<int>(
            std::floor(0.2 * static_cast<double>(population_size))
        );
        for (int row = 0; row < elite_count; ++row) {
            parents.push_back(row);
        }
        for (int row = std::max(0, elite_count - 1);
             row < population_size;
             ++row) {
            if (runtime.rng.uniform(
                    runtime.generations, 213,
                    static_cast<std::uint64_t>(row)
                ) > 0.5) {
                parents.push_back(row);
            }
        }
        if (parents.size() < 2) {
            parents = {0, 1};
        }
        Matrix offspring = population;
        runtime.executor.parallel_for(0, population_size, [&](int row) {
            const int male = runtime.rng.integer(
                0, static_cast<int>(parents.size()),
                runtime.generations, 214,
                static_cast<std::uint64_t>(row), 0
            );
            const int female = runtime.rng.integer(
                0, static_cast<int>(parents.size()),
                runtime.generations, 214,
                static_cast<std::uint64_t>(row), 1
            );
            if (male != female) {
                const int cross_point = runtime.rng.integer(
                    2, dimension + 1,
                    runtime.generations, 215,
                    static_cast<std::uint64_t>(row)
                );
                const int male_row = parents[static_cast<std::size_t>(male)];
                const int female_row = parents[static_cast<std::size_t>(female)];
                const int left = static_cast<int>(std::llround(
                    population[at(male_row, cross_point - 2, dimension)]
                ));
                const int right = static_cast<int>(std::llround(
                    population[at(female_row, cross_point - 1, dimension)]
                ));
                if (left < right) {
                    for (int d = 0; d < cross_point - 1; ++d) {
                        offspring[at(row, d, dimension)] =
                            population[at(male_row, d, dimension)];
                    }
                    for (int d = cross_point - 1; d < dimension; ++d) {
                        offspring[at(row, d, dimension)] =
                            population[at(female_row, d, dimension)];
                    }
                }
            }
            if (runtime.rng.uniform(
                    runtime.generations, 216,
                    static_cast<std::uint64_t>(row)
                ) > 0.1) {
                const int removed_dimension = runtime.rng.integer(
                    0, dimension, runtime.generations, 217,
                    static_cast<std::uint64_t>(row)
                );
                std::vector<char> occupied = blocked;
                for (int d = 0; d < dimension; ++d) {
                    const int cell = static_cast<int>(std::llround(
                        offspring[at(row, d, dimension)]
                    ));
                    occupied[static_cast<std::size_t>(cell - 1)] = 1;
                }
                int inserted = 0;
                std::uint64_t draw = 0;
                do {
                    inserted = runtime.rng.integer(
                        1, grid + 1, runtime.generations, 218,
                        static_cast<std::uint64_t>(row), 0, draw++
                    );
                } while (occupied[static_cast<std::size_t>(inserted - 1)] != 0);
                offspring[at(row, removed_dimension, dimension)] =
                    static_cast<double>(inserted);
                std::sort(
                    offspring.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension),
                    offspring.begin()
                        + static_cast<std::ptrdiff_t>((row + 1) * dimension)
                );
            }
        });
        population = std::move(offspring);
    }
    return finish_result(
        data, config, runtime, population_size, population_size, started
    );
}

RunResult optimize_aiga(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 60;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error("budget is below AIGA initialization");
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1050
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalAndPerTurbine
    );
    std::vector<double> fitness = initial.fitness;
    std::vector<int> turbine_order =
        initial.turbine_position_order_1based;
    std::vector<double> cell_power(static_cast<std::size_t>(grid), 0.0);
    auto accumulate = [&](const fode::Evaluation& evaluated, int rows) {
        for (int row = 0; row < rows; ++row) {
            const double total =
                evaluated.fitness[static_cast<std::size_t>(row)];
            if (total <= 0.0) {
                continue;
            }
            for (int rank = 0; rank < dimension; ++rank) {
                const int cell =
                    evaluated.turbine_position_order_1based[
                        at(row, rank, dimension)
                    ];
                cell_power[static_cast<std::size_t>(cell - 1)] +=
                    evaluated.accumulated_turbine_power_kw[
                        at(row, rank, dimension)
                    ] / total;
            }
        }
    };
    accumulate(initial, population_size);

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const auto ranking = stable_rank_descending(fitness);
        const int parent_count = 36;
        Matrix adapted = population;
        runtime.executor.parallel_for(0, parent_count, [&](int rank) {
            const int row = ranking[static_cast<std::size_t>(rank)];
            std::vector<char> occupied = blocked;
            for (int d = 0; d < dimension; ++d) {
                const int cell = static_cast<int>(std::llround(
                    adapted[at(row, d, dimension)]
                ));
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
            }
            std::vector<int> candidates;
            candidates.reserve(static_cast<std::size_t>(grid - dimension));
            for (int cell = 1; cell <= grid; ++cell) {
                if (occupied[static_cast<std::size_t>(cell - 1)] == 0) {
                    candidates.push_back(cell);
                }
            }
            std::stable_sort(
                candidates.begin(),
                candidates.end(),
                [&](int lhs, int rhs) {
                    return cell_power[static_cast<std::size_t>(lhs - 1)]
                        > cell_power[static_cast<std::size_t>(rhs - 1)];
                }
            );
            const int top_count = std::max(
                1,
                static_cast<int>(std::ceil(
                    0.15 * static_cast<double>(candidates.size())
                ))
            );
            double total_weight = 0.0;
            for (int index = 0; index < top_count; ++index) {
                total_weight += std::max(
                    0.0,
                    cell_power[static_cast<std::size_t>(
                        candidates[static_cast<std::size_t>(index)] - 1
                    )]
                );
            }
            int inserted = candidates.front();
            if (total_weight > 0.0) {
                const double threshold = runtime.rng.uniform(
                    runtime.generations,
                    1051,
                    static_cast<std::uint64_t>(row)
                ) * total_weight;
                double cumulative = 0.0;
                for (int index = 0; index < top_count; ++index) {
                    const int cell =
                        candidates[static_cast<std::size_t>(index)];
                    cumulative += std::max(
                        0.0,
                        cell_power[static_cast<std::size_t>(cell - 1)]
                    );
                    if (threshold <= cumulative) {
                        inserted = cell;
                        break;
                    }
                }
            } else {
                inserted = candidates[static_cast<std::size_t>(
                    runtime.rng.integer(
                        0,
                        top_count,
                        runtime.generations,
                        1052,
                        static_cast<std::uint64_t>(row)
                    )
                )];
            }
            replace_cell(
                adapted,
                row,
                dimension,
                turbine_order[at(row, 0, dimension)],
                inserted
            );
        });

        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        Matrix offspring(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, offspring_count, [&](int row) {
            const int first =
                ranking[static_cast<std::size_t>(runtime.rng.integer(
                    0,
                    parent_count,
                    runtime.generations,
                    1053,
                    static_cast<std::uint64_t>(row)
                ))];
            const int second =
                ranking[static_cast<std::size_t>(runtime.rng.integer(
                    0,
                    parent_count,
                    runtime.generations,
                    1054,
                    static_cast<std::uint64_t>(row)
                ))];
            copy_row(offspring, row, adapted, first, dimension);
            if (dimension > 1
                && runtime.rng.uniform(
                    runtime.generations,
                    1055,
                    static_cast<std::uint64_t>(row)
                ) < 0.6) {
                std::vector<int> valid_cuts;
                for (int cut = 1; cut < dimension; ++cut) {
                    if (adapted[at(first, cut - 1, dimension)]
                        < adapted[at(second, cut, dimension)]) {
                        valid_cuts.push_back(cut);
                    }
                }
                if (!valid_cuts.empty()) {
                    const int cut = valid_cuts[static_cast<std::size_t>(
                        runtime.rng.integer(
                            0,
                            static_cast<int>(valid_cuts.size()),
                            runtime.generations,
                            1056,
                            static_cast<std::uint64_t>(row)
                        )
                    )];
                    for (int d = cut; d < dimension; ++d) {
                        offspring[at(row, d, dimension)] =
                            adapted[at(second, d, dimension)];
                    }
                }
            }
            if (runtime.rng.uniform(
                    runtime.generations,
                    1057,
                    static_cast<std::uint64_t>(row)
                ) < 0.5) {
                const int d = runtime.rng.integer(
                    0,
                    dimension,
                    runtime.generations,
                    1058,
                    static_cast<std::uint64_t>(row)
                );
                offspring[at(row, d, dimension)] =
                    static_cast<double>(runtime.rng.integer(
                        1,
                        grid + 1,
                        runtime.generations,
                        1059,
                        static_cast<std::uint64_t>(row)
                    ));
            }
        });
        repair_population(
            offspring,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1060
        );
        auto evaluated = runtime.evaluate(
            offspring,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalAndPerTurbine
        );
        accumulate(evaluated, offspring_count);
        if (offspring_count < population_size) {
            break;
        }

        std::vector<double> combined_fitness = fitness;
        combined_fitness.insert(
            combined_fitness.end(),
            evaluated.fitness.begin(),
            evaluated.fitness.end()
        );
        const auto combined_ranking =
            stable_rank_descending(combined_fitness);
        Matrix next_population(population.size(), 0.0);
        std::vector<double> next_fitness(
            static_cast<std::size_t>(population_size),
            0.0
        );
        std::vector<int> next_order(
            static_cast<std::size_t>(population_size * dimension),
            0
        );
        for (int row = 0; row < population_size; ++row) {
            const int source =
                combined_ranking[static_cast<std::size_t>(row)];
            next_fitness[static_cast<std::size_t>(row)] =
                combined_fitness[static_cast<std::size_t>(source)];
            if (source < population_size) {
                copy_row(
                    next_population,
                    row,
                    population,
                    source,
                    dimension
                );
                for (int rank = 0; rank < dimension; ++rank) {
                    next_order[at(row, rank, dimension)] =
                        turbine_order[at(source, rank, dimension)];
                }
            } else {
                const int child = source - population_size;
                copy_row(
                    next_population,
                    row,
                    offspring,
                    child,
                    dimension
                );
                for (int rank = 0; rank < dimension; ++rank) {
                    next_order[at(row, rank, dimension)] =
                        evaluated.turbine_position_order_1based[
                            at(child, rank, dimension)
                        ];
                }
            }
        }
        population = std::move(next_population);
        fitness = std::move(next_fitness);
        turbine_order = std::move(next_order);
    }

    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

RunResult optimize_ciga(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 60;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error("budget is below CIGA initialization");
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1100
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    double chaos = 0.6180339887498949;

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const auto ranking = stable_rank_descending(fitness);
        const int parent_count = 36;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        std::vector<double> chaos_values(
            static_cast<std::size_t>(offspring_count),
            0.0
        );
        for (int row = 0; row < offspring_count; ++row) {
            chaos = 3.9 * chaos * (1.0 - chaos);
            chaos_values[static_cast<std::size_t>(row)] = chaos;
        }
        const double shrink = std::max(
            0.0,
            1.0 - static_cast<double>(runtime.fes)
                / static_cast<double>(runtime.budget)
        );
        Matrix offspring(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, offspring_count, [&](int row) {
            const int first =
                ranking[static_cast<std::size_t>(runtime.rng.integer(
                    0,
                    parent_count,
                    runtime.generations,
                    1101,
                    static_cast<std::uint64_t>(row)
                ))];
            const int second =
                ranking[static_cast<std::size_t>(runtime.rng.integer(
                    0,
                    parent_count,
                    runtime.generations,
                    1102,
                    static_cast<std::uint64_t>(row)
                ))];
            copy_row(offspring, row, population, first, dimension);
            if (dimension > 1
                && runtime.rng.uniform(
                    runtime.generations,
                    1103,
                    static_cast<std::uint64_t>(row)
                ) < 0.6) {
                const int cut = runtime.rng.integer(
                    1,
                    dimension,
                    runtime.generations,
                    1104,
                    static_cast<std::uint64_t>(row)
                );
                for (int d = cut; d < dimension; ++d) {
                    offspring[at(row, d, dimension)] =
                        population[at(second, d, dimension)];
                }
            }
            if (runtime.rng.uniform(
                    runtime.generations,
                    1105,
                    static_cast<std::uint64_t>(row)
                ) < 0.5) {
                const int d = runtime.rng.integer(
                    0,
                    dimension,
                    runtime.generations,
                    1106,
                    static_cast<std::uint64_t>(row)
                );
                offspring[at(row, d, dimension)] +=
                    shrink * static_cast<double>(grid - 1)
                    * (
                        chaos_values[static_cast<std::size_t>(row)]
                        - 0.5
                    );
            }
        });
        repair_population(
            offspring,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1107
        );
        auto evaluated = runtime.evaluate(
            offspring,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        if (offspring_count < population_size) {
            break;
        }
        std::vector<double> combined_fitness = fitness;
        combined_fitness.insert(
            combined_fitness.end(),
            evaluated.fitness.begin(),
            evaluated.fitness.end()
        );
        const auto combined_ranking =
            stable_rank_descending(combined_fitness);
        Matrix next_population(population.size(), 0.0);
        std::vector<double> next_fitness(
            static_cast<std::size_t>(population_size),
            0.0
        );
        for (int row = 0; row < population_size; ++row) {
            const int source =
                combined_ranking[static_cast<std::size_t>(row)];
            next_fitness[static_cast<std::size_t>(row)] =
                combined_fitness[static_cast<std::size_t>(source)];
            if (source < population_size) {
                copy_row(
                    next_population,
                    row,
                    population,
                    source,
                    dimension
                );
            } else {
                copy_row(
                    next_population,
                    row,
                    offspring,
                    source - population_size,
                    dimension
                );
            }
        }
        population = std::move(next_population);
        fitness = std::move(next_fitness);
    }
    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

double positive_cauchy(
    const fode::CounterRng& rng,
    double location,
    std::uint64_t generation,
    std::uint64_t phase,
    std::uint64_t individual
) {
    std::uint64_t draw = 0;
    double value = 0.0;
    do {
        value = location + 0.1 * std::tan(
            std::numbers::pi
            * (
                rng.uniform(generation, phase, individual, 0, draw++)
                - 0.5
            )
        );
    } while (value <= 0.0);
    return std::min(value, 1.0);
}

void deduplicate_archive(Matrix& archive, int dimension) {
    const int rows = static_cast<int>(archive.size()) / dimension;
    std::set<std::vector<int>> seen;
    Matrix unique;
    unique.reserve(archive.size());
    for (int row = 0; row < rows; ++row) {
        std::vector<int> key(static_cast<std::size_t>(dimension));
        for (int d = 0; d < dimension; ++d) {
            key[static_cast<std::size_t>(d)] = static_cast<int>(
                std::llround(archive[at(row, d, dimension)])
            );
        }
        if (seen.insert(key).second) {
            for (int d = 0; d < dimension; ++d) {
                unique.push_back(archive[at(row, d, dimension)]);
            }
        }
    }
    archive = std::move(unique);
}

void trim_archive(
    Matrix& archive,
    int capacity,
    int dimension,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t phase
) {
    const int rows = static_cast<int>(archive.size()) / dimension;
    if (rows <= capacity) {
        return;
    }
    std::vector<std::pair<double, int>> keyed;
    keyed.reserve(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        keyed.emplace_back(
            rng.uniform(
                generation,
                phase,
                static_cast<std::uint64_t>(row)
            ),
            row
        );
    }
    std::stable_sort(keyed.begin(), keyed.end());
    Matrix trimmed(static_cast<std::size_t>(capacity * dimension));
    for (int row = 0; row < capacity; ++row) {
        copy_row(
            trimmed,
            row,
            archive,
            keyed[static_cast<std::size_t>(row)].second,
            dimension
        );
    }
    archive = std::move(trimmed);
}

std::array<double, 12> advance_chaos(std::array<double, 12> state) {
    state[0] = 4.0 * state[0] * (1.0 - state[0]);
    state[1] = state[1] < 0.7 ? state[1] / 0.7 : (1.0 - state[1]) / 0.3;
    const double x = state[2];
    state[2] = 1.073 * (
        7.86 * x - 23.31 * x * x + 28.75 * x * x * x
        - 13.302875 * x * x * x * x
    );
    state[3] = std::sin(std::numbers::pi * state[3]);
    state[4] = state[4] == 0.0
        ? 0.0
        : std::fmod(1.0 / state[4], 1.0);
    state[5] = state[5] <= 0.4
        ? state[5] / 0.4
        : (1.0 - state[5]) / 0.6;
    state[6] = state[6] <= 0.6
        ? state[6] / 0.6
        : (state[6] - 0.6) / 0.4;
    state[7] = std::abs(std::cos(5.0 * std::acos(state[7])));
    state[8] = std::fmod(
        state[8] + 0.5
            - 2.2 / (2.0 * std::numbers::pi)
                * std::sin(2.0 * std::numbers::pi * state[8]),
        1.0
    );
    state[9] = 2.59 * state[9] * (1.0 - state[9] * state[9]);
    state[10] = 2.3 * state[10] * state[10]
        * std::sin(std::numbers::pi * state[10]);
    state[11] = std::abs(std::sin(70.0 / state[11]));
    return state;
}

std::array<double, 12> advance_cgpso_chaos(
    std::array<double, 12> state
) {
    state[0] = 4.0 * state[0] - 4.0 * state[0] * state[0];
    if (state[1] > 0.0 && state[1] < 0.7) {
        state[1] /= 0.7;
    } else if (state[1] >= 0.7 && state[1] < 1.0) {
        state[1] = (1.0 - state[1]) / 0.3;
    }
    const double x = state[2];
    state[2] = 1.073 * (
        7.86 * x - 23.31 * x * x + 28.75 * x * x * x
        - 13.302875 * x * x * x * x
    );
    state[3] = std::sin(std::numbers::pi * state[3]);
    state[4] = state[4] == 0.0
        ? 0.0
        : 1.0 / state[4] - std::floor(1.0 / state[4]);
    if (state[5] > 0.0 && state[5] <= 0.4) {
        state[5] /= 0.4;
    } else if (state[5] > 0.4 && state[5] <= 1.0) {
        state[5] = (1.0 - state[5]) / 0.6;
    }
    if (state[6] > 0.0 && state[6] <= 0.6) {
        state[6] /= 0.6;
    } else if (state[6] > 0.6 && state[6] < 1.0) {
        state[6] = (state[6] - 0.6) / 0.4;
    }
    state[7] = std::abs(std::cos(5.0 / std::cos(state[7])));
    const double circle =
        2.2 / (2.0 * std::numbers::pi)
        * std::sin(2.0 * std::numbers::pi * state[8]);
    state[8] = state[8] + 0.5 - (circle - std::floor(circle));
    state[9] = 2.59 * state[9] * (1.0 - state[9] * state[9]);
    state[10] = 2.3 * state[10] * state[10]
        * std::sin(std::numbers::pi * state[10]);
    state[11] = std::abs(std::sin(70.0 / state[11]));
    return state;
}

RunResult optimize_lshade(
    const fode::CaseData& data,
    const RunConfig& config,
    bool chaotic,
    bool adaptive_guidance = false
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int initial_size = adaptive_guidance ? 120 : 18 * dimension;
    const int minimum_size = 4;
    int population_size = initial_size;
    if (runtime.budget < static_cast<std::uint64_t>(initial_size)) {
        throw std::runtime_error("budget is below LSHADE initialization");
    }
    Matrix population = initialize_population(
        population_size,
        data,
        runtime.rng,
        runtime.executor,
        chaotic ? 400 : 300
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix archive;
    std::array<double, 5> memory_f{0.5, 0.5, 0.5, 0.5, 0.5};
    std::array<double, 5> memory_cr{0.5, 0.5, 0.5, 0.5, 0.5};
    int memory_position = 0;
    std::array<double, 12> chaos{
        0.152, 0.152, 0.002, 0.152, 0.152, 0.152,
        0.152, 0.152, 0.152, 0.242, 0.74, 0.152
    };
    std::array<double, 12> selection_probability{};
    selection_probability.fill(1.0 / 12.0);
    Matrix chaos_success(50 * 12, 0.0);
    Matrix chaos_failure(50 * 12, 0.0);
    int chaos_memory_row = 0;
    double radius = 0.01;
    double shrink = 0.988;
    std::vector<double> best_scale_history;

    while (runtime.fes < runtime.budget && population_size >= minimum_size) {
        ++runtime.generations;
        const double previous_generation_best = chaotic
            ? *std::max_element(fitness.begin(), fitness.end())
            : 0.0;
        const int offspring_count = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            runtime.budget - runtime.fes
        ));
        const auto ranking = stable_rank_descending(fitness);
        const int generation_best = ranking.front();
        const double progress = std::clamp(
            static_cast<double>(runtime.fes)
                / static_cast<double>(runtime.budget),
            0.0,
            1.0
        );
        const double best_scale_mean = best_scale_history.empty()
            ? 1.0
            : std::accumulate(
                best_scale_history.begin(),
                best_scale_history.end(),
                0.0
            ) / static_cast<double>(best_scale_history.size());
        const int archive_rows =
            static_cast<int>(archive.size()) / dimension;
        Matrix combined = population;
        combined.insert(combined.end(), archive.begin(), archive.end());
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        std::vector<double> sampled_f(
            static_cast<std::size_t>(offspring_count)
        );
        std::vector<double> sampled_cr(
            static_cast<std::size_t>(offspring_count)
        );
        runtime.executor.parallel_for(0, offspring_count, [&](int individual) {
            const int memory_index = runtime.rng.integer(
                0, 5, runtime.generations, 310,
                static_cast<std::uint64_t>(individual)
            );
            sampled_f[static_cast<std::size_t>(individual)] = positive_cauchy(
                runtime.rng,
                memory_f[static_cast<std::size_t>(memory_index)],
                runtime.generations,
                311,
                static_cast<std::uint64_t>(individual)
            );
            if (adaptive_guidance && individual == generation_best) {
                sampled_f[static_cast<std::size_t>(individual)] = std::clamp(
                    sampled_f[static_cast<std::size_t>(individual)]
                        * progress * best_scale_mean,
                    std::numeric_limits<double>::epsilon(),
                    1.0
                );
            }
            sampled_cr[static_cast<std::size_t>(individual)] = std::clamp(
                memory_cr[static_cast<std::size_t>(memory_index)] == -1.0
                    ? 0.0
                    : memory_cr[static_cast<std::size_t>(memory_index)]
                        + 0.1 * runtime.rng.normal(
                            runtime.generations, 312,
                            static_cast<std::uint64_t>(individual)
                        ),
                0.0,
                1.0
            );
            int r1 = individual;
            std::uint64_t draw = 0;
            while (r1 == individual) {
                r1 = runtime.rng.integer(
                    0, population_size, runtime.generations, 313,
                    static_cast<std::uint64_t>(individual), 0, draw++
                );
            }
            int r2 = individual;
            draw = 0;
            while (r2 == individual || r2 == r1) {
                r2 = runtime.rng.integer(
                    0, population_size + archive_rows,
                    runtime.generations, 314,
                    static_cast<std::uint64_t>(individual), 0, draw++
                );
            }
            const int p_count = std::min(
                population_size,
                std::max(
                    2,
                    static_cast<int>(std::llround(
                        0.11 * static_cast<double>(population_size)
                    ))
                )
            );
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0, p_count, runtime.generations, 315,
                    static_cast<std::uint64_t>(individual)
                )
            )];
            const int jrand = runtime.rng.integer(
                0, dimension, runtime.generations, 316,
                static_cast<std::uint64_t>(individual)
            );
            for (int d = 0; d < dimension; ++d) {
                const double mutant =
                    population[at(individual, d, dimension)]
                    + sampled_f[static_cast<std::size_t>(individual)]
                        * (
                            population[at(pbest, d, dimension)]
                            - population[at(individual, d, dimension)]
                            + population[at(r1, d, dimension)]
                            - combined[at(r2, d, dimension)]
                        );
                const bool use_mutant =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations, 317,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(d)
                    ) <= sampled_cr[static_cast<std::size_t>(individual)];
                trial[at(individual, d, dimension)] = use_mutant
                    ? mutant
                    : population[at(individual, d, dimension)];
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            318
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        std::vector<double> improvement(
            static_cast<std::size_t>(offspring_count),
            0.0
        );
        std::vector<int> successful;
        for (int row = 0; row < offspring_count; ++row) {
            const double gain =
                evaluated.fitness[static_cast<std::size_t>(row)]
                - fitness[static_cast<std::size_t>(row)];
            if (gain > 0.0) {
                improvement[static_cast<std::size_t>(row)] = gain;
                successful.push_back(row);
                for (int d = 0; d < dimension; ++d) {
                    archive.push_back(population[at(row, d, dimension)]);
                }
                copy_row(population, row, trial, row, dimension);
                fitness[static_cast<std::size_t>(row)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            }
        }
        deduplicate_archive(archive, dimension);
        const int archive_capacity = chaotic
            ? population_size
            : static_cast<int>(std::llround(
                1.4 * static_cast<double>(population_size)
            ));
        trim_archive(
            archive,
            archive_capacity,
            dimension,
            runtime.rng,
            runtime.generations,
            319
        );
        if (!successful.empty()) {
            double sum_gain = 0.0;
            for (const int row : successful) {
                sum_gain += improvement[static_cast<std::size_t>(row)];
            }
            double numerator_f = 0.0;
            double denominator_f = 0.0;
            double numerator_cr = 0.0;
            double denominator_cr = 0.0;
            for (const int row : successful) {
                const double weight =
                    improvement[static_cast<std::size_t>(row)] / sum_gain;
                const double f = sampled_f[static_cast<std::size_t>(row)];
                const double cr = sampled_cr[static_cast<std::size_t>(row)];
                numerator_f += weight * f * f;
                denominator_f += weight * f;
                numerator_cr += weight * cr * cr;
                denominator_cr += weight * cr;
            }
            if (denominator_f > 0.0) {
                memory_f[static_cast<std::size_t>(memory_position)] =
                    numerator_f / denominator_f;
            }
            const int max_cr_row = *std::max_element(
                successful.begin(),
                successful.end(),
                [&](int lhs, int rhs) {
                    return sampled_cr[static_cast<std::size_t>(lhs)]
                        < sampled_cr[static_cast<std::size_t>(rhs)];
                }
            );
            if (sampled_cr[static_cast<std::size_t>(max_cr_row)] == 0.0
                || memory_cr[static_cast<std::size_t>(memory_position)]
                    == -1.0) {
                memory_cr[static_cast<std::size_t>(memory_position)] = -1.0;
            } else if (denominator_cr > 0.0) {
                memory_cr[static_cast<std::size_t>(memory_position)] =
                    numerator_cr / denominator_cr;
            }
            memory_position = (memory_position + 1) % 5;
        }
        if (adaptive_guidance
            && generation_best < offspring_count) {
            best_scale_history.push_back(
                sampled_f[static_cast<std::size_t>(generation_best)]
            );
        }
        if (runtime.fes >= runtime.budget) {
            break;
        }

        if (chaotic) {
            chaos = advance_chaos(chaos);
            const double threshold = runtime.rng.uniform(
                runtime.generations, 320, 0
            );
            double cumulative = 0.0;
            int selected_map = 11;
            for (int map = 0; map < 12; ++map) {
                cumulative += selection_probability[
                    static_cast<std::size_t>(map)
                ];
                if (threshold <= cumulative) {
                    selected_map = map;
                    break;
                }
            }
            const int best_row = stable_rank_descending(fitness).front();
            Matrix local(static_cast<std::size_t>(dimension));
            for (int d = 0; d < dimension; ++d) {
                local[static_cast<std::size_t>(d)] =
                    population[at(best_row, d, dimension)]
                    + radius * static_cast<double>(data.rows * data.cols - 1)
                        * (chaos[static_cast<std::size_t>(selected_map)] - 0.5);
            }
            repair_population(
                local,
                1,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                321
            );
            const double before = fitness[static_cast<std::size_t>(best_row)];
            auto local_eval = runtime.evaluate(
                local, 1, data, fode::EvaluationDetail::TotalOnly
            );
            const double difference = std::abs(local_eval.fitness[0] - before);
            for (int map = 0; map < 12; ++map) {
                chaos_success[static_cast<std::size_t>(
                    chaos_memory_row * 12 + map
                )] = 0.0;
                chaos_failure[static_cast<std::size_t>(
                    chaos_memory_row * 12 + map
                )] = 0.0;
            }
            if (local_eval.fitness[0] > before) {
                copy_row(population, best_row, local, 0, dimension);
                fitness[static_cast<std::size_t>(best_row)] =
                    local_eval.fitness[0];
                chaos_success[static_cast<std::size_t>(
                    chaos_memory_row * 12 + selected_map
                )] = difference;
            } else {
                chaos_failure[static_cast<std::size_t>(
                    chaos_memory_row * 12 + selected_map
                )] = difference;
            }
            const double current_generation_best =
                *std::max_element(fitness.begin(), fitness.end());
            if (current_generation_best > previous_generation_best) {
                const double gain =
                    (current_generation_best - previous_generation_best)
                    / std::max(
                        std::abs(previous_generation_best),
                        std::numeric_limits<double>::epsilon()
                    );
                shrink = std::clamp(shrink * (1.0 - gain), 0.0, 1.0);
            }
            radius *= shrink;
            chaos_memory_row = (chaos_memory_row + 1) % 50;
            std::array<double, 12> scores{};
            double score_sum = 0.0;
            for (int map = 0; map < 12; ++map) {
                double success_sum = 0.0;
                double failure_sum = 0.0;
                for (int row = 0; row < 50; ++row) {
                    success_sum += chaos_success[static_cast<std::size_t>(
                        row * 12 + map
                    )];
                    failure_sum += chaos_failure[static_cast<std::size_t>(
                        row * 12 + map
                    )];
                }
                scores[static_cast<std::size_t>(map)] =
                    success_sum / std::max(
                        success_sum + failure_sum,
                        std::numeric_limits<double>::epsilon()
                    ) + 0.01;
                score_sum += scores[static_cast<std::size_t>(map)];
            }
            for (int map = 0; map < 12; ++map) {
                selection_probability[static_cast<std::size_t>(map)] =
                    scores[static_cast<std::size_t>(map)] / score_sum;
            }
        }

        const int target_size = std::max(
            minimum_size,
            static_cast<int>(std::llround(
                static_cast<double>(initial_size)
                + static_cast<double>(minimum_size - initial_size)
                    * static_cast<double>(runtime.fes)
                    / static_cast<double>(runtime.budget)
            ))
        );
        if (target_size < population_size) {
            const auto keep = stable_rank_descending(fitness);
            Matrix reduced(static_cast<std::size_t>(target_size * dimension));
            std::vector<double> reduced_fitness(
                static_cast<std::size_t>(target_size)
            );
            for (int row = 0; row < target_size; ++row) {
                copy_row(
                    reduced,
                    row,
                    population,
                    keep[static_cast<std::size_t>(row)],
                    dimension
                );
                reduced_fitness[static_cast<std::size_t>(row)] =
                    fitness[static_cast<std::size_t>(
                        keep[static_cast<std::size_t>(row)]
                    )];
            }
            population = std::move(reduced);
            fitness = std::move(reduced_fitness);
            population_size = target_size;
            const int reduced_archive_capacity = chaotic
                ? population_size
                : static_cast<int>(std::llround(
                    1.4 * static_cast<double>(population_size)
                ));
            trim_archive(
                archive,
                reduced_archive_capacity,
                dimension,
                runtime.rng,
                runtime.generations,
                322
            );
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        initial_size,
        population_size,
        started
    );
}

RunResult optimize_cede(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int minimum_size = 4;
    const int initial_size = std::max(minimum_size, std::abs(77 - dimension));
    int population_size = initial_size;
    if (runtime.budget < static_cast<std::uint64_t>(initial_size)) {
        throw std::runtime_error("budget is below CEDE initialization");
    }

    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 700
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix archive;
    std::array<double, 5> memory_f{0.5, 0.5, 0.5, 0.5, 0.5};
    std::array<double, 5> memory_cr{0.5, 0.5, 0.5, 0.5, 0.5};
    int memory_position = 0;

    while (runtime.fes < runtime.budget
           && population_size >= minimum_size) {
        ++runtime.generations;
        const std::uint64_t remaining = runtime.budget - runtime.fes;
        int de_count = population_size;
        int genetic_count = population_size;
        const std::uint64_t full_generation =
            static_cast<std::uint64_t>(3 * population_size);
        if (remaining < full_generation) {
            genetic_count = std::min(
                population_size,
                static_cast<int>(remaining / 2)
            );
            de_count = std::min(
                population_size,
                static_cast<int>(
                    remaining
                    - static_cast<std::uint64_t>(2 * genetic_count)
                )
            );
        }

        if (de_count > 0) {
            const auto ranking = stable_rank_descending(fitness);
            const int archive_rows =
                static_cast<int>(archive.size()) / dimension;
            Matrix combined = population;
            combined.insert(combined.end(), archive.begin(), archive.end());
            Matrix trial(
                static_cast<std::size_t>(de_count * dimension),
                0.0
            );
            std::vector<double> sampled_f(
                static_cast<std::size_t>(de_count)
            );
            std::vector<double> sampled_cr(
                static_cast<std::size_t>(de_count)
            );

            runtime.executor.parallel_for(0, de_count, [&](int individual) {
                const int target = individual % population_size;
                const int memory_index = runtime.rng.integer(
                    0, 5, runtime.generations, 701,
                    static_cast<std::uint64_t>(individual)
                );
                sampled_f[static_cast<std::size_t>(individual)] =
                    positive_cauchy(
                        runtime.rng,
                        memory_f[static_cast<std::size_t>(memory_index)],
                        runtime.generations,
                        702,
                        static_cast<std::uint64_t>(individual)
                    );
                sampled_cr[static_cast<std::size_t>(individual)] =
                    std::clamp(
                        memory_cr[static_cast<std::size_t>(memory_index)]
                            == -1.0
                            ? 0.0
                            : memory_cr[
                                static_cast<std::size_t>(memory_index)
                              ] + 0.1 * runtime.rng.normal(
                                  runtime.generations,
                                  703,
                                  static_cast<std::uint64_t>(individual)
                              ),
                        0.0,
                        1.0
                    );
                int r1 = target;
                std::uint64_t draw = 0;
                while (r1 == target) {
                    r1 = runtime.rng.integer(
                        0, population_size, runtime.generations, 704,
                        static_cast<std::uint64_t>(individual), 0, draw++
                    );
                }
                int r2 = target;
                draw = 0;
                while (r2 == target || r2 == r1) {
                    r2 = runtime.rng.integer(
                        0,
                        population_size + archive_rows,
                        runtime.generations,
                        705,
                        static_cast<std::uint64_t>(individual),
                        0,
                        draw++
                    );
                }
                const int p_count = std::min(
                    population_size,
                    std::max(
                        2,
                        static_cast<int>(std::llround(
                            0.11 * static_cast<double>(population_size)
                        ))
                    )
                );
                const int pbest = ranking[static_cast<std::size_t>(
                    runtime.rng.integer(
                        0, p_count, runtime.generations, 706,
                        static_cast<std::uint64_t>(individual)
                    )
                )];
                const int jrand = runtime.rng.integer(
                    0, dimension, runtime.generations, 707,
                    static_cast<std::uint64_t>(individual)
                );
                for (int d = 0; d < dimension; ++d) {
                    const double mutant =
                        population[at(target, d, dimension)]
                        + sampled_f[static_cast<std::size_t>(individual)]
                            * (
                                population[at(pbest, d, dimension)]
                                - population[at(target, d, dimension)]
                                + population[at(r1, d, dimension)]
                                - combined[at(r2, d, dimension)]
                            );
                    const bool use_mutant =
                        d == jrand
                        || runtime.rng.uniform(
                            runtime.generations,
                            708,
                            static_cast<std::uint64_t>(individual),
                            static_cast<std::uint64_t>(d)
                        ) <= sampled_cr[static_cast<std::size_t>(individual)];
                    trial[at(individual, d, dimension)] = use_mutant
                        ? mutant
                        : population[at(target, d, dimension)];
                }
            });
            repair_population(
                trial,
                de_count,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                709
            );
            auto evaluated = runtime.evaluate(
                trial,
                de_count,
                data,
                fode::EvaluationDetail::TotalOnly
            );
            std::vector<double> improvement(
                static_cast<std::size_t>(de_count),
                0.0
            );
            std::vector<int> successful;
            for (int row = 0; row < de_count; ++row) {
                const int target = row % population_size;
                const double gain =
                    evaluated.fitness[static_cast<std::size_t>(row)]
                    - fitness[static_cast<std::size_t>(target)];
                if (gain > 0.0) {
                    improvement[static_cast<std::size_t>(row)] = gain;
                    successful.push_back(row);
                    for (int d = 0; d < dimension; ++d) {
                        archive.push_back(
                            population[at(target, d, dimension)]
                        );
                    }
                    copy_row(population, target, trial, row, dimension);
                    fitness[static_cast<std::size_t>(target)] =
                        evaluated.fitness[static_cast<std::size_t>(row)];
                }
            }
            deduplicate_archive(archive, dimension);
            trim_archive(
                archive,
                static_cast<int>(std::llround(
                    1.4 * static_cast<double>(population_size)
                )),
                dimension,
                runtime.rng,
                runtime.generations,
                710
            );
            if (!successful.empty()) {
                double sum_gain = 0.0;
                for (const int row : successful) {
                    sum_gain += improvement[static_cast<std::size_t>(row)];
                }
                double numerator_f = 0.0;
                double denominator_f = 0.0;
                double numerator_cr = 0.0;
                double denominator_cr = 0.0;
                double maximum_cr = 0.0;
                for (const int row : successful) {
                    const double weight =
                        improvement[static_cast<std::size_t>(row)] / sum_gain;
                    const double f = sampled_f[static_cast<std::size_t>(row)];
                    const double cr =
                        sampled_cr[static_cast<std::size_t>(row)];
                    numerator_f += weight * f * f;
                    denominator_f += weight * f;
                    numerator_cr += weight * cr * cr;
                    denominator_cr += weight * cr;
                    maximum_cr = std::max(maximum_cr, cr);
                }
                if (denominator_f > 0.0) {
                    memory_f[static_cast<std::size_t>(memory_position)] =
                        numerator_f / denominator_f;
                }
                if (maximum_cr == 0.0
                    || memory_cr[static_cast<std::size_t>(memory_position)]
                        == -1.0) {
                    memory_cr[static_cast<std::size_t>(memory_position)] =
                        -1.0;
                } else if (denominator_cr > 0.0) {
                    memory_cr[static_cast<std::size_t>(memory_position)] =
                        numerator_cr / denominator_cr;
                }
                memory_position = (memory_position + 1) % 5;
            }
        }

        if (genetic_count > 0) {
            const int grid = data.rows * data.cols;
            Matrix mutation(
                static_cast<std::size_t>(genetic_count * dimension),
                0.0
            );
            runtime.executor.parallel_for(
                0,
                genetic_count * dimension,
                [&](int task) {
                    const int row = task / dimension;
                    const int d = task - row * dimension;
                    mutation[at(row, d, dimension)] =
                        1.0 + static_cast<double>(grid - 1)
                            * runtime.rng.uniform(
                                runtime.generations,
                                711,
                                static_cast<std::uint64_t>(row),
                                static_cast<std::uint64_t>(d)
                            );
                }
            );
            repair_population(
                mutation,
                genetic_count,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                712
            );
            auto mutation_evaluation = runtime.evaluate(
                mutation,
                genetic_count,
                data,
                fode::EvaluationDetail::TotalOnly,
                false
            );
            const auto ranking = stable_rank_descending(fitness);
            const int global_best = ranking.front();
            const int p_count = std::min(
                population_size,
                std::max(
                    2,
                    static_cast<int>(std::llround(
                        0.11 * static_cast<double>(population_size)
                    ))
                )
            );
            Matrix genetic(
                static_cast<std::size_t>(genetic_count * dimension),
                0.0
            );
            runtime.executor.parallel_for(
                0,
                genetic_count,
                [&](int row) {
                    const int pbest = ranking[static_cast<std::size_t>(
                        runtime.rng.integer(
                            0,
                            p_count,
                            runtime.generations,
                            713,
                            static_cast<std::uint64_t>(row)
                        )
                    )];
                    const bool use_pbest =
                        fitness[static_cast<std::size_t>(pbest)]
                        > mutation_evaluation.fitness[
                            static_cast<std::size_t>(row)
                          ];
                    for (int d = 0; d < dimension; ++d) {
                        const double weight = runtime.rng.uniform(
                            runtime.generations,
                            714,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        );
                        const double learned = use_pbest
                            ? population[at(pbest, d, dimension)]
                            : mutation[at(row, d, dimension)];
                        double value =
                            weight * learned
                            + (1.0 - weight)
                                * population[at(global_best, d, dimension)];
                        if (runtime.rng.uniform(
                                runtime.generations,
                                715,
                                static_cast<std::uint64_t>(row),
                                static_cast<std::uint64_t>(d)
                            ) < 0.01) {
                            value =
                                1.0 + static_cast<double>(grid - 1)
                                    * runtime.rng.uniform(
                                        runtime.generations,
                                        716,
                                        static_cast<std::uint64_t>(row),
                                        static_cast<std::uint64_t>(d)
                                    );
                        }
                        genetic[at(row, d, dimension)] = value;
                    }
                }
            );
            repair_population(
                genetic,
                genetic_count,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                717
            );
            auto genetic_evaluation = runtime.evaluate(
                genetic,
                genetic_count,
                data,
                fode::EvaluationDetail::TotalOnly
            );
            int candidate = 0;
            for (int row = 1; row < genetic_count; ++row) {
                if (genetic_evaluation.fitness[
                        static_cast<std::size_t>(row)
                    ] > genetic_evaluation.fitness[
                        static_cast<std::size_t>(candidate)
                    ]) {
                    candidate = row;
                }
            }
            const int current_best =
                stable_rank_descending(fitness).front();
            if (genetic_evaluation.fitness[
                    static_cast<std::size_t>(candidate)
                ] > fitness[static_cast<std::size_t>(current_best)]) {
                copy_row(
                    population,
                    current_best,
                    genetic,
                    candidate,
                    dimension
                );
                fitness[static_cast<std::size_t>(current_best)] =
                    genetic_evaluation.fitness[
                        static_cast<std::size_t>(candidate)
                    ];
            }
        }

        const int target_size = std::max(
            minimum_size,
            static_cast<int>(std::llround(
                static_cast<double>(initial_size)
                + static_cast<double>(minimum_size - initial_size)
                    * static_cast<double>(runtime.fes)
                    / static_cast<double>(runtime.budget)
            ))
        );
        if (target_size < population_size) {
            const auto keep = stable_rank_descending(fitness);
            Matrix reduced(static_cast<std::size_t>(target_size * dimension));
            std::vector<double> reduced_fitness(
                static_cast<std::size_t>(target_size)
            );
            for (int row = 0; row < target_size; ++row) {
                copy_row(
                    reduced,
                    row,
                    population,
                    keep[static_cast<std::size_t>(row)],
                    dimension
                );
                reduced_fitness[static_cast<std::size_t>(row)] =
                    fitness[static_cast<std::size_t>(
                        keep[static_cast<std::size_t>(row)]
                    )];
            }
            population = std::move(reduced);
            fitness = std::move(reduced_fitness);
            population_size = target_size;
            trim_archive(
                archive,
                static_cast<int>(std::llround(
                    1.4 * static_cast<double>(population_size)
                )),
                dimension,
                runtime.rng,
                runtime.generations,
                718
            );
        }
    }

    return finish_result(
        data,
        config,
        runtime,
        initial_size,
        population_size,
        started
    );
}

RunResult optimize_msshade(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int population_size = std::max(
        4,
        static_cast<int>(std::llround(0.5 * static_cast<double>(dimension)))
    );
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error("budget is below MS-SHADE initialization");
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 800
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix archive;
    std::array<double, 5> memory_f{0.5, 0.5, 0.5, 0.5, 0.5};
    std::array<double, 5> memory_cr{0.5, 0.5, 0.5, 0.5, 0.5};
    int memory_position = 0;

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        const auto ranking = stable_rank_descending(fitness);
        const int global_best = ranking.front();
        const int archive_rows =
            static_cast<int>(archive.size()) / dimension;
        Matrix combined = population;
        combined.insert(combined.end(), archive.begin(), archive.end());
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        std::vector<double> sampled_f(
            static_cast<std::size_t>(offspring_count)
        );
        std::vector<double> sampled_cr(
            static_cast<std::size_t>(offspring_count)
        );

        runtime.executor.parallel_for(0, offspring_count, [&](int individual) {
            const int target = individual % population_size;
            const int memory_index = runtime.rng.integer(
                0, 5, runtime.generations, 801,
                static_cast<std::uint64_t>(individual)
            );
            sampled_f[static_cast<std::size_t>(individual)] = positive_cauchy(
                runtime.rng,
                memory_f[static_cast<std::size_t>(memory_index)],
                runtime.generations,
                802,
                static_cast<std::uint64_t>(individual)
            );
            sampled_cr[static_cast<std::size_t>(individual)] = std::clamp(
                memory_cr[static_cast<std::size_t>(memory_index)]
                    + 0.1 * runtime.rng.normal(
                        runtime.generations,
                        803,
                        static_cast<std::uint64_t>(individual)
                    ),
                0.0,
                1.0
            );
            int r1 = target;
            std::uint64_t draw = 0;
            while (r1 == target) {
                r1 = runtime.rng.integer(
                    0,
                    population_size,
                    runtime.generations,
                    804,
                    static_cast<std::uint64_t>(individual),
                    0,
                    draw++
                );
            }
            int r2 = target;
            draw = 0;
            while (r2 == target || r2 == r1) {
                r2 = runtime.rng.integer(
                    0,
                    population_size,
                    runtime.generations,
                    805,
                    static_cast<std::uint64_t>(individual),
                    0,
                    draw++
                );
            }
            int all = target;
            draw = 0;
            while (all == target || all == r1) {
                all = runtime.rng.integer(
                    0,
                    population_size + archive_rows,
                    runtime.generations,
                    806,
                    static_cast<std::uint64_t>(individual),
                    0,
                    draw++
                );
            }
            const int p_count = std::min(
                population_size,
                std::max(
                    2,
                    static_cast<int>(std::llround(
                        0.11 * static_cast<double>(population_size)
                    ))
                )
            );
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0,
                    p_count,
                    runtime.generations,
                    807,
                    static_cast<std::uint64_t>(individual)
                )
            )];
            const double strategy = runtime.rng.uniform(
                runtime.generations,
                808,
                static_cast<std::uint64_t>(individual)
            );
            const int jrand = runtime.rng.integer(
                0,
                dimension,
                runtime.generations,
                809,
                static_cast<std::uint64_t>(individual)
            );
            for (int d = 0; d < dimension; ++d) {
                double mutant = 0.0;
                if (strategy < 0.1) {
                    mutant =
                        population[at(target, d, dimension)]
                        + sampled_f[static_cast<std::size_t>(individual)]
                            * (
                                population[at(r1, d, dimension)]
                                - population[at(r2, d, dimension)]
                            );
                } else if (strategy < 0.9) {
                    mutant =
                        population[at(target, d, dimension)]
                        + sampled_f[static_cast<std::size_t>(individual)]
                            * (
                                population[at(pbest, d, dimension)]
                                - population[at(target, d, dimension)]
                                + population[at(r1, d, dimension)]
                                - combined[at(all, d, dimension)]
                            );
                } else {
                    mutant =
                        population[at(target, d, dimension)]
                        + sampled_f[static_cast<std::size_t>(individual)]
                            * (
                                population[at(global_best, d, dimension)]
                                - population[at(target, d, dimension)]
                                + population[at(pbest, d, dimension)]
                                - population[at(target, d, dimension)]
                            );
                }
                const bool use_mutant =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations,
                        810,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(d)
                    ) <= sampled_cr[static_cast<std::size_t>(individual)];
                trial[at(individual, d, dimension)] = use_mutant
                    ? mutant
                    : population[at(target, d, dimension)];
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            811
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        std::vector<int> successful;
        for (int row = 0; row < offspring_count; ++row) {
            const int target = row % population_size;
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                > fitness[static_cast<std::size_t>(target)]) {
                successful.push_back(row);
                for (int d = 0; d < dimension; ++d) {
                    archive.push_back(
                        population[at(target, d, dimension)]
                    );
                }
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            }
        }
        deduplicate_archive(archive, dimension);
        trim_archive(
            archive,
            static_cast<int>(std::llround(
                1.4 * static_cast<double>(population_size)
            )),
            dimension,
            runtime.rng,
            runtime.generations,
            812
        );
        if (!successful.empty()) {
            double numerator_f = 0.0;
            double denominator_f = 0.0;
            double mean_cr = 0.0;
            for (const int row : successful) {
                const double f = sampled_f[static_cast<std::size_t>(row)];
                numerator_f += f * f;
                denominator_f += f;
                mean_cr += sampled_cr[static_cast<std::size_t>(row)];
            }
            mean_cr /= static_cast<double>(successful.size());
            const double learning_rate =
                0.05 + 0.15 * runtime.rng.uniform(
                    runtime.generations, 813, 0
                );
            if (denominator_f > 0.0) {
                const double lehmer = numerator_f / denominator_f;
                memory_f[static_cast<std::size_t>(memory_position)] =
                    (1.0 - learning_rate)
                        * memory_f[static_cast<std::size_t>(memory_position)]
                    + learning_rate * lehmer;
            }
            memory_cr[static_cast<std::size_t>(memory_position)] =
                (1.0 - learning_rate)
                    * memory_cr[static_cast<std::size_t>(memory_position)]
                + learning_rate * mean_cr;
            memory_position = (memory_position + 1) % 5;
        }
    }

    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

RunResult optimize_lsde(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int initial_population = 120;
    const int minimum_population = std::min(
        initial_population,
        std::max(
            4,
            static_cast<int>(std::ceil(
                0.3 * static_cast<double>(dimension)
            ))
        )
    );
    if (runtime.budget < static_cast<std::uint64_t>(initial_population)) {
        throw std::runtime_error("budget is below LSDE initialization");
    }
    int population_size = initial_population;
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1200
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix archive;
    std::array<double, 5> memory_f{0.5, 0.5, 0.5, 0.5, 0.5};
    std::array<double, 5> memory_cr{0.5, 0.5, 0.5, 0.5, 0.5};
    int memory_position = 0;

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        const auto ranking = stable_rank_descending(fitness);

        // The paper specifies distance-based C(P,K), K=3, without naming
        // the clustering algorithm. Freeze a deterministic farthest-first
        // three-centroid partition so worker scheduling cannot change it.
        constexpr int cluster_count = 3;
        std::array<int, cluster_count> centroids{
            ranking.front(), ranking.front(), ranking.front()
        };
        for (int cluster = 1; cluster < cluster_count; ++cluster) {
            double farthest_distance = -1.0;
            int farthest_row = ranking.front();
            for (int row = 0; row < population_size; ++row) {
                double nearest = std::numeric_limits<double>::infinity();
                for (int previous = 0; previous < cluster; ++previous) {
                    double distance = 0.0;
                    for (int d = 0; d < dimension; ++d) {
                        const double difference =
                            population[at(row, d, dimension)]
                            - population[at(
                                centroids[static_cast<std::size_t>(previous)],
                                d,
                                dimension
                            )];
                        distance += difference * difference;
                    }
                    nearest = std::min(nearest, distance);
                }
                if (nearest > farthest_distance) {
                    farthest_distance = nearest;
                    farthest_row = row;
                }
            }
            centroids[static_cast<std::size_t>(cluster)] = farthest_row;
        }
        std::vector<int> cluster_of(
            static_cast<std::size_t>(population_size),
            0
        );
        std::array<std::vector<int>, cluster_count> clusters;
        for (int row = 0; row < population_size; ++row) {
            int selected = 0;
            double nearest = std::numeric_limits<double>::infinity();
            for (int cluster = 0; cluster < cluster_count; ++cluster) {
                double distance = 0.0;
                for (int d = 0; d < dimension; ++d) {
                    const double difference =
                        population[at(row, d, dimension)]
                        - population[at(
                            centroids[static_cast<std::size_t>(cluster)],
                            d,
                            dimension
                        )];
                    distance += difference * difference;
                }
                if (distance < nearest) {
                    nearest = distance;
                    selected = cluster;
                }
            }
            cluster_of[static_cast<std::size_t>(row)] = selected;
            clusters[static_cast<std::size_t>(selected)].push_back(row);
        }
        for (auto& cluster : clusters) {
            std::stable_sort(cluster.begin(), cluster.end(), [&](int lhs,
                                                                  int rhs) {
                return fitness[static_cast<std::size_t>(lhs)]
                    > fitness[static_cast<std::size_t>(rhs)];
            });
        }

        std::vector<int> layer(
            static_cast<std::size_t>(population_size),
            0
        );
        std::array<std::vector<int>, cluster_count> top;
        std::array<std::vector<int>, cluster_count> middle;
        for (int cluster = 0; cluster < cluster_count; ++cluster) {
            const auto& rows = clusters[static_cast<std::size_t>(cluster)];
            const int size = static_cast<int>(rows.size());
            const int top_end = std::min(
                size,
                std::max(1, static_cast<int>(std::ceil(0.25 * size)))
            );
            const int middle_end = std::min(
                size,
                std::max(
                    top_end,
                    static_cast<int>(std::ceil(0.50 * size))
                )
            );
            for (int offset = 0; offset < size; ++offset) {
                const int row = rows[static_cast<std::size_t>(offset)];
                if (offset < top_end) {
                    top[static_cast<std::size_t>(cluster)].push_back(row);
                    layer[static_cast<std::size_t>(row)] = 0;
                } else if (offset < middle_end) {
                    middle[static_cast<std::size_t>(cluster)].push_back(row);
                    layer[static_cast<std::size_t>(row)] = 1;
                } else {
                    layer[static_cast<std::size_t>(row)] = 2;
                }
            }
        }

        const int archive_rows =
            static_cast<int>(archive.size()) / dimension;
        Matrix combined = population;
        combined.insert(combined.end(), archive.begin(), archive.end());
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        std::vector<double> sampled_f(
            static_cast<std::size_t>(offspring_count)
        );
        std::vector<double> sampled_cr(
            static_cast<std::size_t>(offspring_count)
        );

        runtime.executor.parallel_for(0, offspring_count, [&](int individual) {
            const int target = individual % population_size;
            const int memory_index = runtime.rng.integer(
                0, 5, runtime.generations, 1201,
                static_cast<std::uint64_t>(individual)
            );
            sampled_f[static_cast<std::size_t>(individual)] = positive_cauchy(
                runtime.rng,
                memory_f[static_cast<std::size_t>(memory_index)],
                runtime.generations,
                1202,
                static_cast<std::uint64_t>(individual)
            );
            sampled_cr[static_cast<std::size_t>(individual)] = std::clamp(
                memory_cr[static_cast<std::size_t>(memory_index)]
                    + 0.1 * runtime.rng.normal(
                        runtime.generations,
                        1203,
                        static_cast<std::uint64_t>(individual)
                    ),
                0.0,
                1.0
            );
            const int p_count = std::min(
                population_size,
                std::max(
                    2,
                    static_cast<int>(std::ceil(
                        (
                            2.0 / static_cast<double>(population_size)
                            + runtime.rng.uniform(
                                runtime.generations,
                                1204,
                                static_cast<std::uint64_t>(individual)
                            ) * (
                                0.2
                                - 2.0
                                    / static_cast<double>(population_size)
                            )
                        ) * static_cast<double>(population_size)
                    ))
                )
            );
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0,
                    p_count,
                    runtime.generations,
                    1205,
                    static_cast<std::uint64_t>(individual)
                )
            )];

            const int cluster =
                cluster_of[static_cast<std::size_t>(target)];
            const int target_layer = layer[static_cast<std::size_t>(target)];
            std::vector<int> donor_pool;
            if (target_layer == 1) {
                donor_pool = top[static_cast<std::size_t>(cluster)];
            } else if (target_layer == 2) {
                donor_pool = middle[static_cast<std::size_t>(cluster)];
            }
            donor_pool.erase(
                std::remove(donor_pool.begin(), donor_pool.end(), target),
                donor_pool.end()
            );
            if (donor_pool.size() < 2) {
                donor_pool.clear();
                for (int row = 0; row < population_size; ++row) {
                    if (row != target) {
                        donor_pool.push_back(row);
                    }
                }
            }
            const int first_position = runtime.rng.integer(
                0,
                static_cast<int>(donor_pool.size()),
                runtime.generations,
                1206,
                static_cast<std::uint64_t>(individual)
            );
            int second_position = first_position;
            std::uint64_t draw = 0;
            while (second_position == first_position) {
                second_position = runtime.rng.integer(
                    0,
                    static_cast<int>(donor_pool.size()),
                    runtime.generations,
                    1207,
                    static_cast<std::uint64_t>(individual),
                    0,
                    draw++
                );
            }
            const int r1 =
                donor_pool[static_cast<std::size_t>(first_position)];
            int r2 = donor_pool[static_cast<std::size_t>(second_position)];
            bool use_archive = false;
            int archive_row = 0;
            if (target_layer == 0 && archive_rows > 0) {
                const int selection = runtime.rng.integer(
                    0,
                    population_size + archive_rows,
                    runtime.generations,
                    1208,
                    static_cast<std::uint64_t>(individual)
                );
                if (selection >= population_size) {
                    use_archive = true;
                    archive_row = selection - population_size;
                } else if (selection != target && selection != r1) {
                    r2 = selection;
                }
            }
            const int jrand = runtime.rng.integer(
                0,
                dimension,
                runtime.generations,
                1209,
                static_cast<std::uint64_t>(individual)
            );
            const double scale =
                sampled_f[static_cast<std::size_t>(individual)];
            for (int d = 0; d < dimension; ++d) {
                const double second =
                    use_archive
                        ? archive[at(archive_row, d, dimension)]
                        : population[at(r2, d, dimension)];
                const double mutant =
                    population[at(target, d, dimension)]
                    + scale * (
                        population[at(pbest, d, dimension)]
                        - population[at(target, d, dimension)]
                        + population[at(r1, d, dimension)]
                        - second
                    );
                const bool use_mutant =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations,
                        1210,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(d)
                    ) <= sampled_cr[static_cast<std::size_t>(individual)];
                trial[at(individual, d, dimension)] = use_mutant
                    ? mutant
                    : population[at(target, d, dimension)];
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1211
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        std::vector<int> successful;
        std::array<bool, cluster_count> cluster_improved{
            false, false, false
        };
        for (int row = 0; row < offspring_count; ++row) {
            const int target = row % population_size;
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                > fitness[static_cast<std::size_t>(target)]) {
                successful.push_back(row);
                cluster_improved[static_cast<std::size_t>(
                    cluster_of[static_cast<std::size_t>(target)]
                )] = true;
                for (int d = 0; d < dimension; ++d) {
                    archive.push_back(
                        population[at(target, d, dimension)]
                    );
                }
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            }
        }
        if (!successful.empty()) {
            double numerator_f = 0.0;
            double denominator_f = 0.0;
            double mean_cr = 0.0;
            for (const int row : successful) {
                const double value_f =
                    sampled_f[static_cast<std::size_t>(row)];
                numerator_f += value_f * value_f;
                denominator_f += value_f;
                mean_cr += sampled_cr[static_cast<std::size_t>(row)];
            }
            if (denominator_f > 0.0) {
                memory_f[static_cast<std::size_t>(memory_position)] =
                    numerator_f / denominator_f;
            }
            memory_cr[static_cast<std::size_t>(memory_position)] =
                mean_cr / static_cast<double>(successful.size());
            memory_position = (memory_position + 1) % 5;
        }

        std::vector<int> stagnant;
        std::vector<int> active;
        for (int cluster = 0; cluster < cluster_count; ++cluster) {
            if (cluster_improved[static_cast<std::size_t>(cluster)]) {
                active.push_back(cluster);
            } else {
                stagnant.push_back(cluster);
            }
        }
        if (stagnant.size() >= 2 && !active.empty()) {
            const int source_cluster = active.front();
            const auto& source =
                clusters[static_cast<std::size_t>(source_cluster)];
            for (int index = 0; index < 2; ++index) {
                auto targets =
                    clusters[static_cast<std::size_t>(stagnant[
                        static_cast<std::size_t>(index)
                    ])];
                std::stable_sort(
                    targets.begin(),
                    targets.end(),
                    [&](int lhs, int rhs) {
                        return fitness[static_cast<std::size_t>(lhs)]
                            < fitness[static_cast<std::size_t>(rhs)];
                    }
                );
                const int replacements = std::min(
                    static_cast<int>(targets.size()),
                    std::max(
                        1,
                        static_cast<int>(std::ceil(
                            0.1 * static_cast<double>(targets.size())
                        ))
                    )
                );
                for (int replacement = 0;
                     replacement < replacements && !source.empty();
                     ++replacement) {
                    const int source_row = source[static_cast<std::size_t>(
                        runtime.rng.integer(
                            0,
                            static_cast<int>(source.size()),
                            runtime.generations,
                            1212,
                            static_cast<std::uint64_t>(index),
                            static_cast<std::uint64_t>(replacement)
                        )
                    )];
                    const int target_row =
                        targets[static_cast<std::size_t>(replacement)];
                    copy_row(
                        population,
                        target_row,
                        population,
                        source_row,
                        dimension
                    );
                    fitness[static_cast<std::size_t>(target_row)] =
                        fitness[static_cast<std::size_t>(source_row)];
                }
            }
        }

        const double progress =
            static_cast<double>(runtime.fes)
            / static_cast<double>(runtime.budget);
        const int target_population = std::clamp(
            static_cast<int>(std::llround(
                static_cast<double>(minimum_population)
                + 0.5 * static_cast<double>(
                    initial_population - minimum_population
                ) * (1.0 + std::cos(std::numbers::pi * progress))
            )),
            minimum_population,
            population_size
        );
        if (target_population < population_size) {
            const auto survivors = stable_rank_descending(fitness);
            Matrix reduced;
            std::vector<double> reduced_fitness;
            reduced.reserve(
                static_cast<std::size_t>(target_population * dimension)
            );
            reduced_fitness.reserve(
                static_cast<std::size_t>(target_population)
            );
            for (int row = 0; row < target_population; ++row) {
                const int source =
                    survivors[static_cast<std::size_t>(row)];
                for (int d = 0; d < dimension; ++d) {
                    reduced.push_back(
                        population[at(source, d, dimension)]
                    );
                }
                reduced_fitness.push_back(
                    fitness[static_cast<std::size_t>(source)]
                );
            }
            population = std::move(reduced);
            fitness = std::move(reduced_fitness);
            population_size = target_population;
        }
        deduplicate_archive(archive, dimension);
        trim_archive(
            archive,
            static_cast<int>(std::llround(
                1.4 * static_cast<double>(population_size)
            )),
            dimension,
            runtime.rng,
            runtime.generations,
            1213
        );
    }

    return finish_result(
        data,
        config,
        runtime,
        initial_population,
        population_size,
        started
    );
}

RunResult optimize_wfadde(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int initial_population = 18 * dimension;
    const int minimum_population = 4;
    if (runtime.budget < static_cast<std::uint64_t>(initial_population)) {
        throw std::runtime_error("budget is below WFADDE initialization");
    }
    int population_size = initial_population;
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1300
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix failure_archive;
    std::array<double, 5> memory_f{0.5, 0.5, 0.5, 0.5, 0.5};
    std::array<double, 5> memory_cr{0.5, 0.5, 0.5, 0.5, 0.5};
    int memory_position = 0;
    double success_rate = 0.0;
    const auto blocked = blocked_mask(data);
    const int grid_size = data.rows * data.cols;

    while (runtime.fes < runtime.budget
           && population_size >= minimum_population) {
        ++runtime.generations;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        const auto ranking = stable_rank_descending(fitness);
        const int failure_rows =
            static_cast<int>(failure_archive.size()) / dimension;
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        std::vector<double> sampled_f(
            static_cast<std::size_t>(offspring_count)
        );
        std::vector<double> sampled_cr(
            static_cast<std::size_t>(offspring_count)
        );
        const double success_mean =
            0.4 + 0.25 * std::tanh(5.0 * success_rate);
        const double pbest_fraction = std::clamp(
            0.7 * std::exp(-7.0 * success_rate),
            2.0 / static_cast<double>(population_size),
            1.0
        );
        const double perturb_probability = std::clamp(
            0.175 - 0.125 * std::tanh(
                4.0 * (success_rate - 0.5)
            ),
            0.05,
            0.3
        );
        const int perturb_count = std::max(
            1,
            static_cast<int>(std::llround(
                0.05 * static_cast<double>(dimension)
            ))
        );

        runtime.executor.parallel_for(0, offspring_count, [&](int individual) {
            const int target = individual % population_size;
            const int memory_index = runtime.rng.integer(
                0, 5, runtime.generations, 1301,
                static_cast<std::uint64_t>(individual)
            );
            const double f_location = 0.5 * (
                success_mean
                + memory_f[static_cast<std::size_t>(memory_index)]
            );
            sampled_f[static_cast<std::size_t>(individual)] = std::clamp(
                f_location
                    + 0.02 * runtime.rng.normal(
                        runtime.generations,
                        1302,
                        static_cast<std::uint64_t>(individual)
                    ),
                0.05,
                1.0
            );
            sampled_cr[static_cast<std::size_t>(individual)] = std::clamp(
                memory_cr[static_cast<std::size_t>(memory_index)]
                    + 0.1 * runtime.rng.normal(
                        runtime.generations,
                        1303,
                        static_cast<std::uint64_t>(individual)
                    ),
                0.0,
                1.0
            );
            const int p_count = std::min(
                population_size,
                std::max(
                    2,
                    static_cast<int>(std::ceil(
                        pbest_fraction
                        * static_cast<double>(population_size)
                    ))
                )
            );
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0,
                    p_count,
                    runtime.generations,
                    1304,
                    static_cast<std::uint64_t>(individual)
                )
            )];
            int r1 = target;
            std::uint64_t draw = 0;
            while (r1 == target) {
                r1 = runtime.rng.integer(
                    0,
                    population_size,
                    runtime.generations,
                    1305,
                    static_cast<std::uint64_t>(individual),
                    0,
                    draw++
                );
            }

            bool use_failure = failure_rows > 0
                && runtime.rng.uniform(
                    runtime.generations,
                    1306,
                    static_cast<std::uint64_t>(individual)
                ) < 0.6;
            int r2 = target;
            int failure_row = 0;
            if (use_failure) {
                std::vector<std::pair<int, int>> distance_rows;
                distance_rows.reserve(
                    static_cast<std::size_t>(failure_rows)
                );
                for (int candidate = 0;
                     candidate < failure_rows;
                     ++candidate) {
                    int common = 0;
                    int left = 0;
                    int right = 0;
                    while (left < dimension && right < dimension) {
                        const int lhs = static_cast<int>(std::llround(
                            population[at(target, left, dimension)]
                        ));
                        const int rhs = static_cast<int>(std::llround(
                            failure_archive[at(
                                candidate, right, dimension
                            )]
                        ));
                        if (lhs == rhs) {
                            ++common;
                            ++left;
                            ++right;
                        } else if (lhs < rhs) {
                            ++left;
                        } else {
                            ++right;
                        }
                    }
                    distance_rows.emplace_back(
                        2 * (dimension - common),
                        candidate
                    );
                }
                std::stable_sort(
                    distance_rows.begin(),
                    distance_rows.end(),
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.first > rhs.first;
                    }
                );
                const int top_count = std::max(
                    1,
                    static_cast<int>(std::ceil(
                        0.2 * static_cast<double>(failure_rows)
                    ))
                );
                failure_row = distance_rows[static_cast<std::size_t>(
                    runtime.rng.integer(
                        0,
                        top_count,
                        runtime.generations,
                        1307,
                        static_cast<std::uint64_t>(individual)
                    )
                )].second;
            } else {
                draw = 0;
                while (r2 == target || r2 == r1) {
                    r2 = runtime.rng.integer(
                        0,
                        population_size,
                        runtime.generations,
                        1308,
                        static_cast<std::uint64_t>(individual),
                        0,
                        draw++
                    );
                }
            }

            const int jrand = runtime.rng.integer(
                0,
                dimension,
                runtime.generations,
                1309,
                static_cast<std::uint64_t>(individual)
            );
            for (int d = 0; d < dimension; ++d) {
                const double second = use_failure
                    ? failure_archive[at(failure_row, d, dimension)]
                    : population[at(r2, d, dimension)];
                const double mutant =
                    population[at(target, d, dimension)]
                    + sampled_f[static_cast<std::size_t>(individual)]
                        * (
                            population[at(pbest, d, dimension)]
                            - population[at(target, d, dimension)]
                            + population[at(r1, d, dimension)]
                            - second
                        );
                const bool use_mutant =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations,
                        1310,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(d)
                    ) <= sampled_cr[static_cast<std::size_t>(individual)];
                trial[at(individual, d, dimension)] = use_mutant
                    ? mutant
                    : population[at(target, d, dimension)];
            }

            if (runtime.rng.uniform(
                    runtime.generations,
                    1311,
                    static_cast<std::uint64_t>(individual)
                ) < perturb_probability) {
                std::vector<std::pair<double, int>> coordinates;
                coordinates.reserve(static_cast<std::size_t>(dimension));
                for (int d = 0; d < dimension; ++d) {
                    coordinates.emplace_back(
                        runtime.rng.uniform(
                            runtime.generations,
                            1312,
                            static_cast<std::uint64_t>(individual),
                            static_cast<std::uint64_t>(d)
                        ),
                        d
                    );
                }
                std::stable_sort(coordinates.begin(), coordinates.end());
                std::vector<char> used = blocked;
                for (int d = 0; d < dimension; ++d) {
                    const int cell = static_cast<int>(std::llround(
                        trial[at(individual, d, dimension)]
                    ));
                    if (cell >= 1 && cell <= grid_size
                        && blocked[static_cast<std::size_t>(cell - 1)] == 0) {
                        used[static_cast<std::size_t>(cell - 1)] = 1;
                    }
                }
                for (int offset = 0; offset < perturb_count; ++offset) {
                    const int d = coordinates[static_cast<std::size_t>(
                        offset
                    )].second;
                    const int current = static_cast<int>(std::llround(
                        trial[at(individual, d, dimension)]
                    ));
                    std::array<int, 2> candidates{
                        current - 1, current + 1
                    };
                    if (runtime.rng.uniform(
                            runtime.generations,
                            1313,
                            static_cast<std::uint64_t>(individual),
                            static_cast<std::uint64_t>(d)
                        ) < 0.5) {
                        std::swap(candidates[0], candidates[1]);
                    }
                    for (const int candidate : candidates) {
                        if (candidate >= 1 && candidate <= grid_size
                            && used[static_cast<std::size_t>(
                                candidate - 1
                            )] == 0) {
                            if (current >= 1 && current <= grid_size
                                && blocked[static_cast<std::size_t>(
                                    current - 1
                                )] == 0) {
                                used[static_cast<std::size_t>(
                                    current - 1
                                )] = 0;
                            }
                            trial[at(individual, d, dimension)] =
                                static_cast<double>(candidate);
                            used[static_cast<std::size_t>(
                                candidate - 1
                            )] = 1;
                            break;
                        }
                    }
                }
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1314
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        std::vector<int> successful;
        std::vector<double> improvement(
            static_cast<std::size_t>(offspring_count),
            0.0
        );
        for (int row = 0; row < offspring_count; ++row) {
            const int target = row % population_size;
            const double gain =
                evaluated.fitness[static_cast<std::size_t>(row)]
                - fitness[static_cast<std::size_t>(target)];
            if (gain > 0.0) {
                successful.push_back(row);
                improvement[static_cast<std::size_t>(row)] = gain;
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            } else {
                for (int d = 0; d < dimension; ++d) {
                    failure_archive.push_back(
                        trial[at(row, d, dimension)]
                    );
                }
            }
        }
        success_rate =
            static_cast<double>(successful.size())
            / static_cast<double>(offspring_count);
        if (!successful.empty()) {
            double gain_sum = 0.0;
            for (const int row : successful) {
                gain_sum += improvement[static_cast<std::size_t>(row)];
            }
            const double threshold = runtime.rng.uniform(
                runtime.generations, 1315, 0
            );
            double cumulative = 0.0;
            int chosen = successful.back();
            double numerator_f = 0.0;
            double denominator_f = 0.0;
            for (const int row : successful) {
                const double weight =
                    improvement[static_cast<std::size_t>(row)] / gain_sum;
                cumulative += weight;
                if (threshold <= cumulative) {
                    chosen = row;
                    break;
                }
            }
            for (const int row : successful) {
                const double gain =
                    improvement[static_cast<std::size_t>(row)];
                const double value =
                    sampled_f[static_cast<std::size_t>(row)];
                numerator_f += gain * value * value;
                denominator_f += gain * value;
            }
            memory_cr[static_cast<std::size_t>(memory_position)] =
                0.5 * (
                    memory_cr[static_cast<std::size_t>(memory_position)]
                    + sampled_cr[static_cast<std::size_t>(chosen)]
                );
            if (denominator_f > 0.0) {
                memory_f[static_cast<std::size_t>(memory_position)] =
                    numerator_f / denominator_f;
            }
            memory_position = (memory_position + 1) % 5;
        }

        deduplicate_archive(failure_archive, dimension);
        trim_archive(
            failure_archive,
            static_cast<int>(std::llround(
                1.4 * static_cast<double>(population_size)
            )),
            dimension,
            runtime.rng,
            runtime.generations,
            1316
        );
        const int target_population = std::max(
            minimum_population,
            static_cast<int>(std::llround(
                static_cast<double>(minimum_population)
                + static_cast<double>(
                    initial_population - minimum_population
                ) * (
                    static_cast<double>(runtime.budget - runtime.fes)
                    / static_cast<double>(runtime.budget)
                )
            ))
        );
        if (target_population < population_size) {
            const auto survivors = stable_rank_descending(fitness);
            Matrix reduced(
                static_cast<std::size_t>(
                    target_population * dimension
                )
            );
            std::vector<double> reduced_fitness(
                static_cast<std::size_t>(target_population)
            );
            for (int row = 0; row < target_population; ++row) {
                const int source =
                    survivors[static_cast<std::size_t>(row)];
                copy_row(
                    reduced, row, population, source, dimension
                );
                reduced_fitness[static_cast<std::size_t>(row)] =
                    fitness[static_cast<std::size_t>(source)];
            }
            population = std::move(reduced);
            fitness = std::move(reduced_fitness);
            population_size = target_population;
            trim_archive(
                failure_archive,
                static_cast<int>(std::llround(
                    1.4 * static_cast<double>(population_size)
                )),
                dimension,
                runtime.rng,
                runtime.generations,
                1317
            );
        }
    }

    return finish_result(
        data,
        config,
        runtime,
        initial_population,
        population_size,
        started
    );
}

RunResult optimize_bde(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int population_size = 50;
    const int half_size = population_size / 2;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error("budget is below BDE initialization");
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 900
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    const std::uint64_t maximum_generations = std::max<std::uint64_t>(
        1,
        (
            runtime.budget
            - static_cast<std::uint64_t>(population_size)
            + static_cast<std::uint64_t>(half_size - 1)
        ) / static_cast<std::uint64_t>(half_size)
    );

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(half_size),
                runtime.budget - runtime.fes
            )
        );
        const auto ranking = stable_rank_descending(fitness);
        std::vector<int> superior(
            ranking.begin(),
            ranking.begin() + half_size
        );
        std::vector<int> inferior(
            ranking.begin() + half_size,
            ranking.end()
        );
        auto deterministic_shuffle = [&](std::vector<int>& rows,
                                         std::uint64_t phase) {
            std::vector<std::pair<double, int>> keyed;
            keyed.reserve(rows.size());
            for (std::size_t index = 0; index < rows.size(); ++index) {
                keyed.emplace_back(
                    runtime.rng.uniform(
                        runtime.generations,
                        phase,
                        static_cast<std::uint64_t>(index)
                    ),
                    rows[index]
                );
            }
            std::stable_sort(keyed.begin(), keyed.end());
            for (std::size_t index = 0; index < rows.size(); ++index) {
                rows[index] = keyed[index].second;
            }
        };
        deterministic_shuffle(superior, 901);
        deterministic_shuffle(inferior, 902);

        const int superior_fusion_count = static_cast<int>(std::floor(
            0.5 * static_cast<double>(half_size)
        ));
        std::vector<int> fused_superior;
        std::vector<int> fused_inferior;
        fused_superior.reserve(static_cast<std::size_t>(half_size));
        fused_inferior.reserve(static_cast<std::size_t>(half_size));
        for (int row = 0; row < half_size; ++row) {
            if (row < superior_fusion_count) {
                fused_superior.push_back(
                    superior[static_cast<std::size_t>(row)]
                );
            } else {
                fused_inferior.push_back(
                    superior[static_cast<std::size_t>(row)]
                );
            }
            if (row < half_size - superior_fusion_count) {
                fused_superior.push_back(
                    inferior[static_cast<std::size_t>(row)]
                );
            } else {
                fused_inferior.push_back(
                    inferior[static_cast<std::size_t>(row)]
                );
            }
        }
        if (fused_superior.size() != static_cast<std::size_t>(half_size)
            || fused_inferior.size()
                != static_cast<std::size_t>(half_size)) {
            throw std::runtime_error("BDE fusion did not preserve half sizes");
        }

        const double mu =
            static_cast<double>(maximum_generations)
            / static_cast<double>(
                maximum_generations + runtime.generations + 1
            );
        const double epsilon = std::exp(1.0 - mu);
        const double scale = 0.5 * std::pow(2.0, epsilon);
        const double crossover_rate = std::clamp(
            0.1 * static_cast<double>(runtime.generations)
                / static_cast<double>(maximum_generations),
            0.0,
            0.1
        );
        const int pbest_count = std::min(
            population_size,
            static_cast<int>(std::ceil(
                0.05 * static_cast<double>(population_size)
            ))
        );
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, offspring_count, [&](int row) {
            auto draw_distinct = [&](std::uint64_t phase,
                                     int first,
                                     int second) {
                int result = row;
                std::uint64_t draw = 0;
                while (result == row
                       || result == first
                       || result == second) {
                    result = runtime.rng.integer(
                        0,
                        half_size,
                        runtime.generations,
                        phase,
                        static_cast<std::uint64_t>(row),
                        0,
                        draw++
                    );
                }
                return result;
            };
            const int r1 = draw_distinct(903, -1, -1);
            const int r2 = draw_distinct(904, r1, -1);
            const int r3 = draw_distinct(905, r1, r2);
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0,
                    pbest_count,
                    runtime.generations,
                    906,
                    static_cast<std::uint64_t>(row)
                )
            )];
            const int jrand = runtime.rng.integer(
                0,
                dimension,
                runtime.generations,
                907,
                static_cast<std::uint64_t>(row)
            );
            for (int d = 0; d < dimension; ++d) {
                const double mutant =
                    population[at(
                        fused_superior[static_cast<std::size_t>(r1)],
                        d,
                        dimension
                    )]
                    + scale * (
                        population[at(
                            fused_superior[static_cast<std::size_t>(r2)],
                            d,
                            dimension
                        )]
                        - population[at(
                            fused_inferior[static_cast<std::size_t>(r3)],
                            d,
                            dimension
                        )]
                    );
                const bool use_mutant =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations,
                        908,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) < crossover_rate;
                trial[at(row, d, dimension)] = use_mutant
                    ? mutant
                    : population[at(pbest, d, dimension)];
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            909
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        for (int row = 0; row < offspring_count; ++row) {
            const int target = ranking[static_cast<std::size_t>(row)];
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                >= fitness[static_cast<std::size_t>(target)]) {
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            }
        }
    }

    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

RunResult optimize_hgpso(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 120;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error("budget is below HGPSO initialization");
    }

    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1000
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalAndPerTurbine
    );
    std::vector<double> current_fitness = initial.fitness;
    std::vector<int> current_order =
        initial.turbine_position_order_1based;
    std::vector<int> stagnation(
        static_cast<std::size_t>(population_size),
        0
    );
    Matrix cumulative_cell_power(
        static_cast<std::size_t>(population_size * grid),
        0.0
    );
    auto accumulate_power = [&](
        const fode::Evaluation& detailed,
        int rows,
        const std::vector<char>* accepted
    ) {
        for (int row = 0; row < rows; ++row) {
            if (accepted != nullptr
                && (*accepted)[static_cast<std::size_t>(row)] == 0) {
                continue;
            }
            const double total =
                detailed.fitness[static_cast<std::size_t>(row)];
            if (total <= 0.0) {
                continue;
            }
            for (int rank = 0; rank < dimension; ++rank) {
                const int cell =
                    detailed.turbine_position_order_1based[
                        at(row, rank, dimension)
                    ];
                cumulative_cell_power[static_cast<std::size_t>(
                    row * grid + cell - 1
                )] +=
                    detailed.accumulated_turbine_power_kw[
                        at(row, rank, dimension)
                    ] / total;
            }
        }
    };
    accumulate_power(initial, population_size, nullptr);

    Matrix global_best(static_cast<std::size_t>(dimension), 0.0);
    auto refresh_global_best = [&]() {
        const int row = stable_rank_descending(current_fitness).front();
        copy_row(global_best, 0, population, row, dimension);
    };
    refresh_global_best();

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int exemplar_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        Matrix exemplar(
            static_cast<std::size_t>(exemplar_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, exemplar_count, [&](int row) {
            for (int d = 0; d < dimension; ++d) {
                const int peer = runtime.rng.integer(
                    0,
                    population_size,
                    runtime.generations,
                    1001,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                if (current_fitness[static_cast<std::size_t>(row)]
                    > current_fitness[static_cast<std::size_t>(peer)]) {
                    const double first = runtime.rng.uniform(
                        runtime.generations,
                        1002,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    );
                    const double second = runtime.rng.uniform(
                        runtime.generations,
                        1003,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    );
                    exemplar[at(row, d, dimension)] =
                        first * population[at(row, d, dimension)]
                        + (1.0 - second)
                            * global_best[static_cast<std::size_t>(d)];
                } else {
                    exemplar[at(row, d, dimension)] =
                        population[at(peer, d, dimension)];
                }
                if (runtime.rng.uniform(
                        runtime.generations,
                        1004,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) < 0.01) {
                    exemplar[at(row, d, dimension)] =
                        1.0 + runtime.rng.uniform(
                            runtime.generations,
                            1005,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ) * static_cast<double>(grid - 1);
                }
            }
        });
        repair_population(
            exemplar,
            exemplar_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1006
        );
        auto exemplar_evaluation = runtime.evaluate(
            exemplar,
            exemplar_count,
            data,
            fode::EvaluationDetail::TotalAndPerTurbine
        );

        Matrix reference = population;
        std::vector<double> reference_fitness = current_fitness;
        std::vector<char> accepted_exemplar(
            static_cast<std::size_t>(exemplar_count),
            0
        );
        for (int row = 0; row < exemplar_count; ++row) {
            if (exemplar_evaluation.fitness[static_cast<std::size_t>(row)]
                > current_fitness[static_cast<std::size_t>(row)]) {
                copy_row(reference, row, exemplar, row, dimension);
                reference_fitness[static_cast<std::size_t>(row)] =
                    exemplar_evaluation.fitness[
                        static_cast<std::size_t>(row)
                    ];
                accepted_exemplar[static_cast<std::size_t>(row)] = 1;
            }
        }
        accumulate_power(
            exemplar_evaluation,
            exemplar_count,
            &accepted_exemplar
        );
        if (exemplar_count < population_size
            || runtime.fes >= runtime.budget) {
            break;
        }

        const Matrix reference_snapshot = reference;
        const std::vector<double> reference_fitness_snapshot =
            reference_fitness;
        runtime.executor.parallel_for(0, population_size, [&](int row) {
            if (stagnation[static_cast<std::size_t>(row)] < 7) {
                return;
            }
            std::vector<std::pair<double, int>> keyed;
            keyed.reserve(static_cast<std::size_t>(population_size));
            for (int candidate = 0;
                 candidate < population_size;
                 ++candidate) {
                keyed.emplace_back(
                    runtime.rng.uniform(
                        runtime.generations,
                        1007,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(candidate)
                    ),
                    candidate
                );
            }
            std::stable_sort(keyed.begin(), keyed.end());
            int winner = keyed.front().second;
            for (int candidate = 1; candidate < 24; ++candidate) {
                const int challenger =
                    keyed[static_cast<std::size_t>(candidate)].second;
                if (reference_fitness_snapshot[
                        static_cast<std::size_t>(challenger)
                    ] > reference_fitness_snapshot[
                        static_cast<std::size_t>(winner)
                    ]) {
                    winner = challenger;
                }
            }
            copy_row(
                reference,
                row,
                reference_snapshot,
                winner,
                dimension
            );
            reference_fitness[static_cast<std::size_t>(row)] =
                reference_fitness_snapshot[
                    static_cast<std::size_t>(winner)
                ];
        });
        for (int row = 0; row < population_size; ++row) {
            if (stagnation[static_cast<std::size_t>(row)] >= 7) {
                stagnation[static_cast<std::size_t>(row)] = 0;
            }
        }

        Matrix trial(
            static_cast<std::size_t>(population_size * dimension),
            0.0
        );
        runtime.executor.parallel_for(
            0,
            population_size * dimension,
            [&](int task) {
                const int row = task / dimension;
                const int d = task - row * dimension;
                const double current =
                    population[at(row, d, dimension)];
                const double guide =
                    reference[at(row, d, dimension)];
                const double best =
                    global_best[static_cast<std::size_t>(d)];
                const bool current_equals_guide = current == guide;
                const bool current_equals_best = current == best;
                const double branch = runtime.rng.uniform(
                    runtime.generations,
                    1008,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                const double step = 1.49618 * runtime.rng.uniform(
                    runtime.generations,
                    1009,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                double value = current;
                if (current_equals_guide && current_equals_best) {
                    value = 1.0 + runtime.rng.uniform(
                        runtime.generations,
                        1010,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) * static_cast<double>(grid - 1);
                } else if (branch < 0.1) {
                    value = current_equals_guide
                        ? current + step * (best - current)
                        : current + step * (guide - current);
                } else if (current_equals_best
                           || current_equals_guide) {
                    value = current_equals_best
                        ? current + step * (guide - current)
                        : current + step * (best - current);
                } else {
                    value = guide + step * (best - guide);
                }
                trial[at(row, d, dimension)] = value;
            }
        );
        repair_population(
            trial,
            population_size,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1011
        );

        runtime.executor.parallel_for(0, population_size, [&](int row) {
            int weakest = static_cast<int>(std::llround(
                trial[at(row, 0, dimension)]
            ));
            double weakest_score =
                cumulative_cell_power[static_cast<std::size_t>(
                    row * grid + weakest - 1
                )];
            std::vector<char> occupied = blocked;
            for (int d = 0; d < dimension; ++d) {
                const int cell = static_cast<int>(std::llround(
                    trial[at(row, d, dimension)]
                ));
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
                const double score =
                    cumulative_cell_power[static_cast<std::size_t>(
                        row * grid + cell - 1
                    )];
                if (score < weakest_score) {
                    weakest = cell;
                    weakest_score = score;
                }
            }

            std::vector<int> ranked_cells(static_cast<std::size_t>(grid));
            std::iota(ranked_cells.begin(), ranked_cells.end(), 1);
            std::stable_sort(
                ranked_cells.begin(),
                ranked_cells.end(),
                [&](int lhs, int rhs) {
                    return cumulative_cell_power[static_cast<std::size_t>(
                        row * grid + lhs - 1
                    )] > cumulative_cell_power[static_cast<std::size_t>(
                        row * grid + rhs - 1
                    )];
                }
            );
            const int pool_size = std::min(
                grid,
                static_cast<int>(
                    std::floor(0.1 * static_cast<double>(grid))
                ) + dimension
            );
            std::vector<std::pair<double, int>> pool;
            pool.reserve(static_cast<std::size_t>(pool_size));
            for (int index = 0; index < pool_size; ++index) {
                pool.emplace_back(
                    runtime.rng.uniform(
                        runtime.generations,
                        1012,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(index)
                    ),
                    ranked_cells[static_cast<std::size_t>(index)]
                );
            }
            std::stable_sort(pool.begin(), pool.end());
            int inserted = 0;
            for (const auto& item : pool) {
                const int cell = item.second;
                if (occupied[static_cast<std::size_t>(cell - 1)] == 0) {
                    inserted = cell;
                    break;
                }
            }
            if (inserted == 0) {
                for (int cell = 1; cell <= grid; ++cell) {
                    if (occupied[static_cast<std::size_t>(cell - 1)] == 0) {
                        inserted = cell;
                        break;
                    }
                }
            }
            if (inserted == 0) {
                throw std::runtime_error(
                    "HGPSO worst-position replacement has no feasible cell"
                );
            }
            replace_cell(
                trial,
                row,
                dimension,
                weakest,
                inserted
            );
        });

        const int trial_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        auto trial_evaluation = runtime.evaluate(
            trial,
            trial_count,
            data,
            fode::EvaluationDetail::TotalAndPerTurbine
        );
        accumulate_power(trial_evaluation, trial_count, nullptr);
        for (int row = 0; row < trial_count; ++row) {
            if (trial_evaluation.fitness[static_cast<std::size_t>(row)]
                > current_fitness[static_cast<std::size_t>(row)]) {
                copy_row(population, row, trial, row, dimension);
                current_fitness[static_cast<std::size_t>(row)] =
                    trial_evaluation.fitness[
                        static_cast<std::size_t>(row)
                    ];
                for (int rank = 0; rank < dimension; ++rank) {
                    current_order[at(row, rank, dimension)] =
                        trial_evaluation.turbine_position_order_1based[
                            at(row, rank, dimension)
                        ];
                }
                stagnation[static_cast<std::size_t>(row)] = 0;
            } else {
                ++stagnation[static_cast<std::size_t>(row)];
            }
        }
        if (trial_count < population_size) {
            break;
        }
        refresh_global_best();
    }

    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started,
        "paper_hierarchical_staged_parallel"
    );
}

RunResult optimize_pso(
    const fode::CaseData& data,
    const RunConfig& config,
    bool chaotic
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 120;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    Matrix population = initialize_population(
        population_size,
        data,
        runtime.rng,
        runtime.executor,
        chaotic ? 600 : 500
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        chaotic
            ? fode::EvaluationDetail::TotalOnly
            : fode::EvaluationDetail::TotalAndPerTurbine
    );
    std::vector<double> current_fitness = initial.fitness;
    std::vector<int> current_order = initial.turbine_position_order_1based;
    Matrix pbest = population;
    std::vector<double> pbest_fitness = current_fitness;
    Matrix velocity(population.size(), 0.0);
    std::vector<int> stagnation(static_cast<std::size_t>(population_size), 0);
    Matrix empirical(
        static_cast<std::size_t>(population_size * grid),
        0.0
    );
    auto update_empirical = [&](const fode::Evaluation& detailed) {
        for (int row = 0; row < population_size; ++row) {
            const double total = detailed.fitness[static_cast<std::size_t>(row)];
            if (total <= 0.0) {
                continue;
            }
            for (int rank = 0; rank < dimension; ++rank) {
                const int cell = detailed.turbine_position_order_1based[
                    at(row, rank, dimension)
                ];
                empirical[
                    static_cast<std::size_t>(row * grid + cell - 1)
                ] += detailed.accumulated_turbine_power_kw[
                    at(row, rank, dimension)
                ] / total;
            }
        }
    };
    if (!chaotic) {
        update_empirical(initial);
    }
    int gbest_row = stable_rank_descending(pbest_fitness).front();
    Matrix gbest(static_cast<std::size_t>(dimension));
    copy_row(gbest, 0, pbest, gbest_row, dimension);
    double gbest_fitness = pbest_fitness[static_cast<std::size_t>(gbest_row)];
    std::array<double, 12> chaos{
        0.152, 0.152, 0.002, 0.152, 0.152, 0.152,
        0.152, 0.152, 0.152, 0.242, 0.74, 0.152
    };
    std::array<double, 12> chaos_probability{};
    chaos_probability.fill(1.0 / 12.0);
    Matrix chaos_success(25 * 12, 0.0);
    Matrix chaos_failure(25 * 12, 0.0);
    int chaos_memory_row = 0;

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        if (chaotic) {
            chaos = advance_cgpso_chaos(chaos);
            Matrix local = gbest;
            const double probability_sum = std::accumulate(
                chaos_probability.begin(),
                chaos_probability.end(),
                0.0
            );
            const double threshold = runtime.rng.uniform(
                runtime.generations, 609, 0
            ) * probability_sum;
            int selected_map = 11;
            double cumulative = 0.0;
            for (int map = 0; map < 12; ++map) {
                cumulative += chaos_probability[
                    static_cast<std::size_t>(map)
                ];
                if (threshold <= cumulative) {
                    selected_map = map;
                    break;
                }
            }
            const double lambda = 0.01 * std::max(
                0.0,
                1.0 - static_cast<double>(runtime.generations) / 200.0
            );
            for (int d = 0; d < dimension; ++d) {
                local[static_cast<std::size_t>(d)] +=
                    lambda * static_cast<double>(grid - 1)
                    * (
                        chaos[static_cast<std::size_t>(selected_map)]
                        - 0.5
                    );
            }
            repair_population(
                local, 1, data, runtime.rng, runtime.executor,
                runtime.generations, 611
            );
            auto local_eval = runtime.evaluate(
                local, 1, data, fode::EvaluationDetail::TotalOnly
            );
            for (int map = 0; map < 12; ++map) {
                chaos_success[static_cast<std::size_t>(
                    chaos_memory_row * 12 + map
                )] = 0.0;
                chaos_failure[static_cast<std::size_t>(
                    chaos_memory_row * 12 + map
                )] = 0.0;
            }
            if (local_eval.fitness[0] > gbest_fitness) {
                gbest = local;
                gbest_fitness = local_eval.fitness[0];
                chaos_success[
                    static_cast<std::size_t>(
                        chaos_memory_row * 12 + selected_map
                    )
                ] = 1.0;
            } else {
                chaos_failure[
                    static_cast<std::size_t>(
                        chaos_memory_row * 12 + selected_map
                    )
                ] = 1.0;
            }
            chaos_memory_row = (chaos_memory_row + 1) % 25;
            double probability_total = 0.0;
            for (int map = 0; map < 12; ++map) {
                double successes = 0.0;
                double failures = 0.0;
                for (int row = 0; row < 25; ++row) {
                    successes += chaos_success[
                        static_cast<std::size_t>(row * 12 + map)
                    ];
                    failures += chaos_failure[
                        static_cast<std::size_t>(row * 12 + map)
                    ];
                }
                chaos_probability[static_cast<std::size_t>(map)] =
                    successes / std::max(
                        successes + failures,
                        std::numeric_limits<double>::epsilon()
                    ) + 0.01;
                probability_total += chaos_probability[
                    static_cast<std::size_t>(map)
                ];
            }
            for (double& probability : chaos_probability) {
                probability /= probability_total;
            }
            if (runtime.fes >= runtime.budget) {
                break;
            }
        }

        const int elite_count = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            runtime.budget - runtime.fes
        ));
        Matrix elite(
            static_cast<std::size_t>(elite_count * dimension),
            0.0
        );
        runtime.executor.parallel_for(0, elite_count, [&](int row) {
            for (int d = 0; d < dimension; ++d) {
                const int peer = runtime.rng.integer(
                    0, population_size, runtime.generations, 620,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                if (pbest_fitness[static_cast<std::size_t>(row)]
                    > pbest_fitness[static_cast<std::size_t>(peer)]) {
                    const double mix = runtime.rng.uniform(
                        runtime.generations, 621,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    );
                    elite[at(row, d, dimension)] =
                        mix * pbest[at(row, d, dimension)]
                        + (1.0 - mix) * gbest[static_cast<std::size_t>(d)];
                } else {
                    elite[at(row, d, dimension)] =
                        chaotic
                        ? pbest[at(peer, d, dimension)]
                        : pbest[at(row, d, dimension)];
                }
                if (runtime.rng.uniform(
                        runtime.generations, 622,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) < 0.01) {
                    elite[at(row, d, dimension)] =
                        1.0 + runtime.rng.uniform(
                            runtime.generations, 623,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ) * static_cast<double>(grid - 1);
                }
            }
        });
        repair_population(
            elite, elite_count, data, runtime.rng, runtime.executor,
            runtime.generations, 624
        );
        auto elite_eval = runtime.evaluate(
            elite,
            elite_count,
            data,
            chaotic
                ? fode::EvaluationDetail::TotalOnly
                : fode::EvaluationDetail::TotalAndPerTurbine
        );
        for (int row = 0; row < elite_count; ++row) {
            if (elite_eval.fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                if (!chaotic) {
                    const double total =
                        elite_eval.fitness[static_cast<std::size_t>(row)];
                    for (int rank = 0; rank < dimension; ++rank) {
                        const int cell =
                            elite_eval.turbine_position_order_1based[
                                at(row, rank, dimension)
                            ];
                        empirical[static_cast<std::size_t>(
                            row * grid + cell - 1
                        )] +=
                            elite_eval.accumulated_turbine_power_kw[
                                at(row, rank, dimension)
                            ] / total;
                    }
                }
                pbest_fitness[static_cast<std::size_t>(row)] =
                    elite_eval.fitness[static_cast<std::size_t>(row)];
                copy_row(pbest, row, elite, row, dimension);
            }
        }
        const Matrix post_elite_pbest = pbest;
        const std::vector<double> post_elite_fitness = pbest_fitness;
        Matrix post_tournament_pbest = pbest;
        std::vector<double> post_tournament_fitness = pbest_fitness;
        std::vector<char> tournament_reset(
            static_cast<std::size_t>(population_size),
            0
        );
        runtime.executor.parallel_for(0, population_size, [&](int row) {
            if (stagnation[static_cast<std::size_t>(row)] >= 7) {
                std::vector<std::pair<double, int>> candidates;
                candidates.reserve(static_cast<std::size_t>(population_size));
                for (int candidate = 0;
                     candidate < population_size;
                     ++candidate) {
                    candidates.emplace_back(
                        runtime.rng.uniform(
                            runtime.generations,
                            625,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(candidate)
                        ),
                        candidate
                    );
                }
                std::stable_sort(candidates.begin(), candidates.end());
                int winner = candidates.front().second;
                for (int competitor = 1; competitor < 24; ++competitor) {
                    const int candidate = candidates[
                        static_cast<std::size_t>(competitor)
                    ].second;
                    if (post_elite_fitness[
                            static_cast<std::size_t>(candidate)
                        ] > post_elite_fitness[
                            static_cast<std::size_t>(winner)
                        ]) {
                        winner = candidate;
                    }
                }
                copy_row(
                    post_tournament_pbest,
                    row,
                    post_elite_pbest,
                    winner,
                    dimension
                );
                post_tournament_fitness[static_cast<std::size_t>(row)] =
                    post_elite_fitness[static_cast<std::size_t>(winner)];
                tournament_reset[static_cast<std::size_t>(row)] = 1;
            }
        });
        pbest = std::move(post_tournament_pbest);
        pbest_fitness = std::move(post_tournament_fitness);
        for (int row = 0; row < population_size; ++row) {
            if (tournament_reset[static_cast<std::size_t>(row)] != 0) {
                stagnation[static_cast<std::size_t>(row)] = 0;
            }
        }
        if (elite_count < population_size || runtime.fes >= runtime.budget) {
            break;
        }

        // AGPSO Algorithm 1 retains X'_i only when it improves the evaluated
        // generation-entry X_i.  Snapshot the full population state before
        // adaptive replacement so the paper's survival rule remains
        // executable without spending an unreported extra evaluation batch.
        Matrix generation_entry_population;
        std::vector<double> generation_entry_fitness;
        std::vector<int> generation_entry_order;
        if (!chaotic) {
            generation_entry_population = population;
            generation_entry_fitness = current_fitness;
            generation_entry_order = current_order;
        }

        if (!chaotic) {
            runtime.executor.parallel_for(0, population_size, [&](int row) {
                const int worst = current_order[at(row, 0, dimension)];
                std::vector<char> occupied = blocked;
                for (int d = 0; d < dimension; ++d) {
                    const int cell = static_cast<int>(std::llround(
                        population[at(row, d, dimension)]
                    ));
                    occupied[static_cast<std::size_t>(cell - 1)] = 1;
                }
                std::vector<int> location_rank(
                    static_cast<std::size_t>(grid)
                );
                std::iota(location_rank.begin(), location_rank.end(), 1);
                std::stable_sort(
                    location_rank.begin(),
                    location_rank.end(),
                    [&](int lhs, int rhs) {
                        return empirical[
                            static_cast<std::size_t>(row * grid + lhs - 1)
                        ] > empirical[
                            static_cast<std::size_t>(row * grid + rhs - 1)
                        ];
                    }
                );
                const int tournament_locations = std::max(
                    1,
                    static_cast<int>(std::llround(
                        0.2 * static_cast<double>(grid)
                    ))
                );
                int selected = 0;
                for (std::uint64_t draw = 0;
                     draw < static_cast<std::uint64_t>(10 * grid);
                     ++draw) {
                    const int rank = runtime.rng.integer(
                        0,
                        tournament_locations,
                        runtime.generations,
                        626,
                        static_cast<std::uint64_t>(row),
                        0,
                        draw
                    );
                    const int cell = location_rank[
                        static_cast<std::size_t>(rank)
                    ];
                    if (occupied[static_cast<std::size_t>(cell - 1)] == 0) {
                        selected = cell;
                        break;
                    }
                }
                if (selected == 0) {
                    for (int cell = 1; cell <= grid; ++cell) {
                        if (occupied[static_cast<std::size_t>(cell - 1)] == 0) {
                            selected = cell;
                            break;
                        }
                    }
                }
                replace_cell(population, row, dimension, worst, selected);
            });
        }

        runtime.executor.parallel_for(
            0,
            population_size * dimension,
            [&](int task) {
                const int row = task / dimension;
                const int d = task - row * dimension;
                const double inertia = chaotic ? 0.7298 : 0.0;
                velocity[at(row, d, dimension)] =
                    inertia * velocity[at(row, d, dimension)]
                    + 1.49618 * runtime.rng.uniform(
                        runtime.generations, 627,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) * (
                        pbest[at(row, d, dimension)]
                        - population[at(row, d, dimension)]
                    );
                population[at(row, d, dimension)] +=
                    velocity[at(row, d, dimension)];
            }
        );
        repair_population(
            population, population_size, data, runtime.rng, runtime.executor,
            runtime.generations, 628
        );
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            runtime.budget - runtime.fes
        ));
        auto evaluated = runtime.evaluate(
            population,
            completed,
            data,
            chaotic
                ? fode::EvaluationDetail::TotalOnly
                : fode::EvaluationDetail::TotalAndPerTurbine
        );
        if (completed == population_size) {
            if (chaotic) {
                current_fitness = evaluated.fitness;
            } else {
                update_empirical(evaluated);
                for (int row = 0; row < population_size; ++row) {
                    if (evaluated.fitness[static_cast<std::size_t>(row)]
                        > generation_entry_fitness[
                            static_cast<std::size_t>(row)
                        ]) {
                        current_fitness[static_cast<std::size_t>(row)] =
                            evaluated.fitness[static_cast<std::size_t>(row)];
                        for (int rank = 0; rank < dimension; ++rank) {
                            current_order[at(row, rank, dimension)] =
                                evaluated.turbine_position_order_1based[
                                    at(row, rank, dimension)
                                ];
                        }
                    } else {
                        copy_row(
                            population,
                            row,
                            generation_entry_population,
                            row,
                            dimension
                        );
                        current_fitness[static_cast<std::size_t>(row)] =
                            generation_entry_fitness[
                                static_cast<std::size_t>(row)
                            ];
                        for (int rank = 0; rank < dimension; ++rank) {
                            current_order[at(row, rank, dimension)] =
                                generation_entry_order[
                                    at(row, rank, dimension)
                                ];
                        }
                    }
                }
            }
        }
        for (int row = 0; row < completed; ++row) {
            const double accepted_fitness =
                completed == population_size
                ? current_fitness[static_cast<std::size_t>(row)]
                : evaluated.fitness[static_cast<std::size_t>(row)];
            if (accepted_fitness
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    accepted_fitness;
                copy_row(pbest, row, population, row, dimension);
                stagnation[static_cast<std::size_t>(row)] = 0;
            } else {
                ++stagnation[static_cast<std::size_t>(row)];
            }
        }
        gbest_row = stable_rank_descending(pbest_fitness).front();
        if (pbest_fitness[static_cast<std::size_t>(gbest_row)] > gbest_fitness) {
            gbest_fitness = pbest_fitness[static_cast<std::size_t>(gbest_row)];
            copy_row(gbest, 0, pbest, gbest_row, dimension);
        }
        if (completed < population_size) {
            break;
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started,
        "paper_staged_parallel"
    );
}

RunResult optimize_de_comparator(
    const fode::CaseData& data,
    const RunConfig& config,
    const std::string& mode
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int initial_size = std::max(20, std::min(120, 10 * dimension));
    int population_size = initial_size;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error(
            "budget is below comparator DE initialization"
        );
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1500
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    double mean_f = 0.5;
    double mean_cr = 0.5;

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const auto ranking = stable_rank_descending(fitness);
        const bool local_search =
            mode == "cjade" || mode == "scjade"
            || mode == "lshadecnepsin";
        if (local_search && runtime.fes < runtime.budget) {
            Matrix local(static_cast<std::size_t>(dimension));
            copy_row(local, 0, population, ranking.front(), dimension);
            const double progress = static_cast<double>(runtime.fes)
                / static_cast<double>(runtime.budget);
            const double radius =
                (mode == "scjade" ? 0.25 : 0.5)
                * std::max(0.01, 1.0 - progress);
            for (int d = 0; d < dimension; ++d) {
                local[static_cast<std::size_t>(d)] +=
                    radius
                    * static_cast<double>(data.rows * data.cols - 1)
                    * (
                        runtime.rng.uniform(
                            runtime.generations,
                            1501,
                            static_cast<std::uint64_t>(d)
                        ) - 0.5
                    );
            }
            repair_population(
                local,
                1,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                1502
            );
            auto evaluated = runtime.evaluate(
                local, 1, data, fode::EvaluationDetail::TotalOnly
            );
            const int best = ranking.front();
            if (evaluated.fitness[0]
                > fitness[static_cast<std::size_t>(best)]) {
                copy_row(population, best, local, 0, dimension);
                fitness[static_cast<std::size_t>(best)] =
                    evaluated.fitness[0];
            }
            if (runtime.fes >= runtime.budget) {
                break;
            }
        }

        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension), 0.0
        );
        std::vector<double> sampled_f(
            static_cast<std::size_t>(offspring_count), 0.5
        );
        std::vector<double> sampled_cr(
            static_cast<std::size_t>(offspring_count), 0.9
        );
        const bool adaptive = mode != "de";
        runtime.executor.parallel_for(0, offspring_count, [&](int row) {
            const int target = row % population_size;
            if (adaptive) {
                sampled_f[static_cast<std::size_t>(row)] = positive_cauchy(
                    runtime.rng,
                    mean_f,
                    runtime.generations,
                    1503,
                    static_cast<std::uint64_t>(row)
                );
                sampled_cr[static_cast<std::size_t>(row)] = std::clamp(
                    mean_cr + 0.1 * runtime.rng.normal(
                        runtime.generations,
                        1504,
                        static_cast<std::uint64_t>(row)
                    ),
                    0.0,
                    1.0
                );
            }
            auto distinct = [&](int first, int second, std::uint64_t salt) {
                int value = target;
                std::uint64_t draw = 0;
                while (value == target || value == first || value == second) {
                    value = runtime.rng.integer(
                        0,
                        population_size,
                        runtime.generations,
                        salt,
                        static_cast<std::uint64_t>(row),
                        0,
                        draw++
                    );
                }
                return value;
            };
            const int r1 = distinct(-1, -1, 1505);
            const int r2 = distinct(r1, -1, 1506);
            const int r3 = distinct(r1, r2, 1507);
            const int p_count = std::max(
                2,
                static_cast<int>(std::ceil(
                    0.05 * static_cast<double>(population_size)
                ))
            );
            const int pbest = ranking[static_cast<std::size_t>(
                runtime.rng.integer(
                    0,
                    std::min(population_size, p_count),
                    runtime.generations,
                    1508,
                    static_cast<std::uint64_t>(row)
                )
            )];
            const int jrand = runtime.rng.integer(
                0,
                dimension,
                runtime.generations,
                1509,
                static_cast<std::uint64_t>(row)
            );
            for (int d = 0; d < dimension; ++d) {
                const double f = sampled_f[static_cast<std::size_t>(row)];
                const double mutant = mode == "de"
                    ? population[at(r1, d, dimension)]
                        + f * (
                            population[at(r2, d, dimension)]
                            - population[at(r3, d, dimension)]
                        )
                    : population[at(target, d, dimension)]
                        + f * (
                            population[at(pbest, d, dimension)]
                            - population[at(target, d, dimension)]
                            + population[at(r1, d, dimension)]
                            - population[at(r2, d, dimension)]
                        );
                trial[at(row, d, dimension)] =
                    d == jrand
                    || runtime.rng.uniform(
                        runtime.generations,
                        1510,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) <= sampled_cr[static_cast<std::size_t>(row)]
                    ? mutant
                    : population[at(target, d, dimension)];
            }
        });
        repair_population(
            trial,
            offspring_count,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1511
        );
        auto evaluated = runtime.evaluate(
            trial,
            offspring_count,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        double successful_f = 0.0;
        double successful_f2 = 0.0;
        double successful_cr = 0.0;
        int successes = 0;
        for (int row = 0; row < offspring_count; ++row) {
            const int target = row % population_size;
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                > fitness[static_cast<std::size_t>(target)]) {
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
                const double f = sampled_f[static_cast<std::size_t>(row)];
                successful_f += f;
                successful_f2 += f * f;
                successful_cr +=
                    sampled_cr[static_cast<std::size_t>(row)];
                ++successes;
            }
        }
        if (adaptive && successes > 0) {
            if (successful_f > 0.0) {
                mean_f = 0.9 * mean_f
                    + 0.1 * successful_f2 / successful_f;
            }
            mean_cr = 0.9 * mean_cr
                + 0.1 * successful_cr / static_cast<double>(successes);
        }
        if (mode == "lshadecnepsin" && population_size > 4) {
            const int target_size = std::max(
                4,
                static_cast<int>(std::llround(
                    static_cast<double>(initial_size)
                    + static_cast<double>(4 - initial_size)
                        * static_cast<double>(runtime.fes)
                        / static_cast<double>(runtime.budget)
                ))
            );
            if (target_size < population_size) {
                const auto keep = stable_rank_descending(fitness);
                Matrix reduced(
                    static_cast<std::size_t>(target_size * dimension)
                );
                std::vector<double> reduced_fitness(
                    static_cast<std::size_t>(target_size)
                );
                for (int row = 0; row < target_size; ++row) {
                    const int source =
                        keep[static_cast<std::size_t>(row)];
                    copy_row(
                        reduced, row, population, source, dimension
                    );
                    reduced_fitness[static_cast<std::size_t>(row)] =
                        fitness[static_cast<std::size_t>(source)];
                }
                population = std::move(reduced);
                fitness = std::move(reduced_fitness);
                population_size = target_size;
            }
        }
        if (offspring_count < population_size) {
            break;
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        initial_size,
        population_size,
        started
    );
}

RunResult optimize_pso_comparator(
    const fode::CaseData& data,
    const RunConfig& config,
    const std::string& mode
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 120;
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error(
            "budget is below comparator PSO initialization"
        );
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1600
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    Matrix pbest = population;
    std::vector<double> pbest_fitness = fitness;
    Matrix velocity(population.size(), 0.0);
    std::vector<int> stagnation(
        static_cast<std::size_t>(population_size), 0
    );

    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int best = stable_rank_descending(pbest_fitness).front();
        Matrix exemplar = pbest;
        if (mode == "glpso") {
            const int count = static_cast<int>(
                std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(population_size),
                    runtime.budget - runtime.fes
                )
            );
            runtime.executor.parallel_for(0, count, [&](int row) {
                for (int d = 0; d < dimension; ++d) {
                    const int peer = runtime.rng.integer(
                        0, population_size, runtime.generations, 1601,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    );
                    const double mix = runtime.rng.uniform(
                        runtime.generations, 1602,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    );
                    exemplar[at(row, d, dimension)] =
                        pbest_fitness[static_cast<std::size_t>(row)]
                            < pbest_fitness[static_cast<std::size_t>(peer)]
                        ? mix * pbest[at(row, d, dimension)]
                            + (1.0 - mix) * pbest[at(best, d, dimension)]
                        : pbest[at(peer, d, dimension)];
                    if (runtime.rng.uniform(
                            runtime.generations, 1603,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ) < 0.01) {
                        exemplar[at(row, d, dimension)] =
                            1.0 + runtime.rng.uniform(
                                runtime.generations, 1604,
                                static_cast<std::uint64_t>(row),
                                static_cast<std::uint64_t>(d)
                            ) * static_cast<double>(grid - 1);
                    }
                }
            });
            repair_population(
                exemplar,
                count,
                data,
                runtime.rng,
                runtime.executor,
                runtime.generations,
                1605
            );
            auto exemplar_eval = runtime.evaluate(
                exemplar, count, data, fode::EvaluationDetail::TotalOnly
            );
            for (int row = 0; row < count; ++row) {
                if (exemplar_eval.fitness[static_cast<std::size_t>(row)]
                    > pbest_fitness[static_cast<std::size_t>(row)]) {
                    copy_row(pbest, row, exemplar, row, dimension);
                    pbest_fitness[static_cast<std::size_t>(row)] =
                        exemplar_eval.fitness[static_cast<std::size_t>(row)];
                }
            }
            if (count < population_size || runtime.fes >= runtime.budget) {
                break;
            }
        } else if (mode == "clpso") {
            runtime.executor.parallel_for(
                0,
                population_size * dimension,
                [&](int task) {
                    const int row = task / dimension;
                    const int d = task - row * dimension;
                    const int first = runtime.rng.integer(
                        0, population_size, runtime.generations, 1606,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d), 0
                    );
                    const int second = runtime.rng.integer(
                        0, population_size, runtime.generations, 1606,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d), 1
                    );
                    const int winner =
                        pbest_fitness[static_cast<std::size_t>(first)]
                            >= pbest_fitness[static_cast<std::size_t>(second)]
                        ? first : second;
                    const double global_probability =
                        static_cast<double>(row)
                        / static_cast<double>(population_size - 1);
                    exemplar[at(row, d, dimension)] =
                        runtime.rng.uniform(
                            runtime.generations, 1607,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ) < global_probability
                        ? pbest[at(best, d, dimension)]
                        : pbest[at(winner, d, dimension)];
                }
            );
        }

        const int completed = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        runtime.executor.parallel_for(
            0,
            completed * dimension,
            [&](int task) {
                const int row = task / dimension;
                const int d = task - row * dimension;
                const double r1 = runtime.rng.uniform(
                    runtime.generations,
                    1608,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                const double r2 = runtime.rng.uniform(
                    runtime.generations,
                    1609,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                double target = exemplar[at(row, d, dimension)];
                double social = 0.0;
                if (mode == "pso") {
                    target = pbest[at(row, d, dimension)];
                    social = 1.49618 * r2 * (
                        pbest[at(best, d, dimension)]
                        - population[at(row, d, dimension)]
                    );
                }
                velocity[at(row, d, dimension)] =
                    0.7298 * velocity[at(row, d, dimension)]
                    + 1.49618 * r1 * (
                        target - population[at(row, d, dimension)]
                    )
                    + social;
                population[at(row, d, dimension)] +=
                    velocity[at(row, d, dimension)];
            }
        );
        repair_population(
            population,
            completed,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1610
        );
        auto evaluated = runtime.evaluate(
            population,
            completed,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        for (int row = 0; row < completed; ++row) {
            fitness[static_cast<std::size_t>(row)] =
                evaluated.fitness[static_cast<std::size_t>(row)];
            if (fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    fitness[static_cast<std::size_t>(row)];
                copy_row(pbest, row, population, row, dimension);
                stagnation[static_cast<std::size_t>(row)] = 0;
            } else {
                ++stagnation[static_cast<std::size_t>(row)];
            }
        }
        if (completed < population_size) {
            break;
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started,
        "comparator_population_barrier_parallel"
    );
}

RunResult optimize_gravitational_comparator(
    const fode::CaseData& data,
    const RunConfig& config,
    bool hybrid
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 100;
    const int dimension = data.turbine_count;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error(
            "budget is below GSA-family initialization"
        );
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1700
    );
    Matrix velocity(population.size(), 0.0);
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const auto ranking = stable_rank_descending(fitness);
        const double worst =
            fitness[static_cast<std::size_t>(ranking.back())];
        const double best =
            fitness[static_cast<std::size_t>(ranking.front())];
        std::vector<double> mass(static_cast<std::size_t>(population_size));
        double mass_sum = 0.0;
        for (int row = 0; row < population_size; ++row) {
            mass[static_cast<std::size_t>(row)] = std::exp(
                (fitness[static_cast<std::size_t>(row)] - worst)
                / std::max(
                    best - worst,
                    std::numeric_limits<double>::epsilon()
                )
            );
            mass_sum += mass[static_cast<std::size_t>(row)];
        }
        for (double& value : mass) {
            value /= mass_sum;
        }
        const double progress = static_cast<double>(runtime.fes)
            / static_cast<double>(runtime.budget);
        const double gravity = 100.0 * std::exp(-20.0 * progress);
        const int kbest = std::max(
            1,
            static_cast<int>(std::ceil(
                static_cast<double>(population_size)
                * (1.0 - progress)
            ))
        );
        const int completed = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        runtime.executor.parallel_for(
            0,
            completed * dimension,
            [&](int task) {
                const int row = task / dimension;
                const int d = task - row * dimension;
                double force = 0.0;
                for (int rank = 0; rank < kbest; ++rank) {
                    const int peer = ranking[static_cast<std::size_t>(rank)];
                    if (peer == row) {
                        continue;
                    }
                    const double distance = std::abs(
                        population[at(peer, d, dimension)]
                        - population[at(row, d, dimension)]
                    ) + 1.0e-12;
                    force += runtime.rng.uniform(
                        runtime.generations,
                        1701,
                        static_cast<std::uint64_t>(task),
                        static_cast<std::uint64_t>(rank)
                    ) * mass[static_cast<std::size_t>(peer)]
                        * (
                            population[at(peer, d, dimension)]
                            - population[at(row, d, dimension)]
                        ) / distance;
                }
                if (hybrid) {
                    const int global = ranking.front();
                    force += (
                        population[at(global, d, dimension)]
                        - population[at(row, d, dimension)]
                    );
                }
                velocity[at(row, d, dimension)] =
                    runtime.rng.uniform(
                        runtime.generations,
                        1702,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(d)
                    ) * velocity[at(row, d, dimension)]
                    + gravity * force;
                population[at(row, d, dimension)] +=
                    velocity[at(row, d, dimension)];
            }
        );
        repair_population(
            population,
            completed,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1703
        );
        auto evaluated = runtime.evaluate(
            population,
            completed,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        for (int row = 0; row < completed; ++row) {
            fitness[static_cast<std::size_t>(row)] =
                evaluated.fitness[static_cast<std::size_t>(row)];
        }
        if (completed < population_size) {
            break;
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

RunResult optimize_spherical_comparator(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int population_size = 100;
    const int dimension = data.turbine_count;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error(
            "budget is below spherical evolution initialization"
        );
    }
    Matrix population = initialize_population(
        population_size, data, runtime.rng, runtime.executor, 1800
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int completed = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                runtime.budget - runtime.fes
            )
        );
        Matrix offspring(
            static_cast<std::size_t>(completed * dimension), 0.0
        );
        runtime.executor.parallel_for(0, completed, [&](int row) {
            copy_row(offspring, row, population, row, dimension);
            const int active = std::max(1, dimension / 3);
            const int peer = runtime.rng.integer(
                0,
                population_size,
                runtime.generations,
                1801,
                static_cast<std::uint64_t>(row)
            );
            for (int step = 0; step < active; ++step) {
                const int d = runtime.rng.integer(
                    0,
                    dimension,
                    runtime.generations,
                    1802,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(step)
                );
                const double radius = std::abs(
                    population[at(peer, d, dimension)]
                    - population[at(row, d, dimension)]
                );
                offspring[at(row, d, dimension)] +=
                    std::max(1.0, radius)
                    * runtime.rng.normal(
                        runtime.generations,
                        1803,
                        static_cast<std::uint64_t>(row),
                        static_cast<std::uint64_t>(step)
                    );
            }
        });
        repair_population(
            offspring,
            completed,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            1804
        );
        auto evaluated = runtime.evaluate(
            offspring,
            completed,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        for (int row = 0; row < completed; ++row) {
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                > fitness[static_cast<std::size_t>(row)]) {
                copy_row(population, row, offspring, row, dimension);
                fitness[static_cast<std::size_t>(row)] =
                    evaluated.fitness[static_cast<std::size_t>(row)];
            }
        }
        if (completed < population_size) {
            break;
        }
    }
    return finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
}

double ppga_diversity(
    const Matrix& population,
    int population_size,
    int dimension,
    fode::PersistentExecutor& executor
) {
    std::vector<double> row_sums(
        static_cast<std::size_t>(population_size), 0.0
    );
    executor.parallel_for(0, population_size, [&](int left) {
        double sum = 0.0;
        for (int right = left + 1; right < population_size; ++right) {
            int shared = 0;
            int left_position = 0;
            int right_position = 0;
            while (left_position < dimension
                   && right_position < dimension) {
                const int left_cell = static_cast<int>(std::llround(
                    population[at(left, left_position, dimension)]
                ));
                const int right_cell = static_cast<int>(std::llround(
                    population[at(right, right_position, dimension)]
                ));
                if (left_cell == right_cell) {
                    ++shared;
                    ++left_position;
                    ++right_position;
                } else if (left_cell < right_cell) {
                    ++left_position;
                } else {
                    ++right_position;
                }
            }
            sum += static_cast<double>(dimension - shared);
        }
        row_sums[static_cast<std::size_t>(left)] = sum;
    });
    const double numerator =
        std::accumulate(row_sums.begin(), row_sums.end(), 0.0);
    const double denominator =
        static_cast<double>(
            dimension * population_size * (population_size - 1)
        );
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

int ppga_power_law_step(
    int maximum,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    int individual,
    int coordinate
) {
    constexpr double exponent = 2.5;
    const double uniform = rng.uniform(
        generation,
        7010,
        static_cast<std::uint64_t>(individual),
        static_cast<std::uint64_t>(coordinate)
    );
    const double upper =
        std::pow(static_cast<double>(maximum + 1), 1.0 - exponent);
    const double continuous = std::pow(
        1.0 + uniform * (upper - 1.0),
        1.0 / (1.0 - exponent)
    );
    return std::clamp(
        static_cast<int>(std::floor(continuous)),
        1,
        maximum
    );
}

RunResult optimize_ppga(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    constexpr int population_size = 30;
    constexpr int elite_count = 3;
    constexpr double crossover_rate = 0.8;
    constexpr double mutation_rate = 0.1;
    constexpr double adaptation_threshold = 0.0;
    if (runtime.budget < static_cast<std::uint64_t>(population_size)) {
        throw std::runtime_error(
            "PPGA budget is below its 30-layout initialization"
        );
    }
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    Matrix population = initialize_population(
        population_size,
        data,
        runtime.rng,
        runtime.executor,
        7000
    );
    auto initial = runtime.evaluate(
        population,
        population_size,
        data,
        fode::EvaluationDetail::TotalOnly
    );
    std::vector<double> fitness = initial.fitness;
    double stagnant_proportion = 0.0;
    while (runtime.fes < runtime.budget) {
        ++runtime.generations;
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            runtime.budget - runtime.fes
        ));
        const double diversity = ppga_diversity(
            population,
            population_size,
            dimension,
            runtime.executor
        );
        const auto minimum_maximum = std::minmax_element(
            fitness.begin(), fitness.end()
        );
        const double minimum_fitness = *minimum_maximum.first;
        const double maximum_fitness = *minimum_maximum.second;
        std::vector<double> adaptation(
            static_cast<std::size_t>(population_size), 0.0
        );
        runtime.executor.parallel_for(
            0,
            population_size,
            [&](int individual) {
                const double normalized_fitness =
                    maximum_fitness > minimum_fitness
                        ? (
                            fitness[static_cast<std::size_t>(individual)]
                            - minimum_fitness
                        ) / (maximum_fitness - minimum_fitness)
                        : 1.0;
                adaptation[static_cast<std::size_t>(individual)] =
                    normalized_fitness * std::sqrt(diversity)
                    - stagnant_proportion;
            }
        );
        Matrix offspring(
            static_cast<std::size_t>(completed * dimension), 0.0
        );
        runtime.executor.parallel_for(0, completed, [&](int individual) {
            const int first = runtime.rng.integer(
                0,
                population_size,
                runtime.generations,
                7001,
                static_cast<std::uint64_t>(individual)
            );
            int second = runtime.rng.integer(
                0,
                population_size,
                runtime.generations,
                7002,
                static_cast<std::uint64_t>(individual)
            );
            if (first == second) {
                second = (second + 1) % population_size;
            }
            for (int coordinate = 0;
                 coordinate < dimension;
                 ++coordinate) {
                const bool from_first = runtime.rng.uniform(
                    runtime.generations,
                    7003,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(coordinate)
                ) < crossover_rate;
                double value = population[at(
                    from_first ? first : second,
                    coordinate,
                    dimension
                )];
                if (runtime.rng.uniform(
                        runtime.generations,
                        7004,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(coordinate)
                    ) < mutation_rate) {
                    value = static_cast<double>(runtime.rng.integer(
                        1,
                        grid + 1,
                        runtime.generations,
                        7005,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(coordinate)
                    ));
                }
                offspring[at(individual, coordinate, dimension)] = value;
            }
            const double theta =
                adaptation[static_cast<std::size_t>(individual)];
            const double perturbation_probability = std::clamp(
                adaptation_threshold - theta, 0.0, 1.0
            );
            const bool perturb =
                runtime.generations >= 2
                && theta < adaptation_threshold
                && runtime.rng.uniform(
                    runtime.generations,
                    7006,
                    static_cast<std::uint64_t>(individual)
                ) < perturbation_probability;
            if (perturb) {
                for (int coordinate = 0;
                     coordinate < dimension;
                     ++coordinate) {
                    const int current = static_cast<int>(std::llround(
                        offspring[at(individual, coordinate, dimension)]
                    ));
                    const int magnitude = ppga_power_law_step(
                        grid - 1,
                        runtime.rng,
                        runtime.generations,
                        individual,
                        coordinate
                    );
                    const int sign = runtime.rng.uniform(
                        runtime.generations,
                        7011,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(coordinate)
                    ) < 0.5 ? -1 : 1;
                    int wrapped =
                        (current - 1 + sign * magnitude) % grid;
                    if (wrapped < 0) {
                        wrapped += grid;
                    }
                    offspring[at(individual, coordinate, dimension)] =
                        static_cast<double>(wrapped + 1);
                }
            }
        });
        repair_population(
            offspring,
            completed,
            data,
            runtime.rng,
            runtime.executor,
            runtime.generations,
            7012
        );
        auto evaluated = runtime.evaluate(
            offspring,
            completed,
            data,
            fode::EvaluationDetail::TotalOnly
        );
        if (completed < population_size) {
            break;
        }
        int stagnant = 0;
        for (int row = 0; row < population_size; ++row) {
            if (evaluated.fitness[static_cast<std::size_t>(row)]
                <= fitness[static_cast<std::size_t>(row)]) {
                ++stagnant;
            }
        }
        stagnant_proportion =
            static_cast<double>(stagnant)
            / static_cast<double>(population_size);

        const auto parent_order = stable_rank_descending(fitness);
        const auto offspring_order =
            stable_rank_descending(evaluated.fitness);
        Matrix next(population.size(), 0.0);
        std::vector<double> next_fitness(
            static_cast<std::size_t>(population_size), 0.0
        );
        for (int row = 0; row < elite_count; ++row) {
            const int source =
                parent_order[static_cast<std::size_t>(row)];
            copy_row(next, row, population, source, dimension);
            next_fitness[static_cast<std::size_t>(row)] =
                fitness[static_cast<std::size_t>(source)];
        }
        for (int row = elite_count; row < population_size; ++row) {
            const int source = offspring_order[
                static_cast<std::size_t>(row - elite_count)
            ];
            copy_row(next, row, offspring, source, dimension);
            next_fitness[static_cast<std::size_t>(row)] =
                evaluated.fitness[static_cast<std::size_t>(source)];
        }
        population = std::move(next);
        fitness = std::move(next_fitness);
    }
    RunResult result = finish_result(
        data,
        config,
        runtime,
        population_size,
        population_size,
        started
    );
    result.method_id = "PPGA_DECLARED_RECONSTRUCTION_FODE_E0_V1";
    return result;
}

RunResult convert_fode(
    const fode::CaseData& data,
    const RunConfig& config
) {
    fode::RunConfig fode_config;
    fode_config.seed = config.seed;
    fode_config.physical_fes_budget = config.physical_fes_budget;
    fode_config.workers = config.workers;
    const fode::RunResult source = fode::optimize_fode_hpc(data, fode_config);
    RunResult result;
    result.algorithm_id = "fode";
    result.method_id = "FODE_CPP_HPC_FULL";
    const auto identity = algorithm_descriptor("fode");
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_provenance = identity.provenance;
    result.effective_semantics_id = identity.semantics_id;
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = source.case_id;
    result.seed = source.seed;
    result.physical_fes = source.physical_fes;
    result.inference_physical_fes = source.physical_fes;
    result.generations = source.generations;
    result.initial_population = source.initial_population;
    result.final_population = source.final_population;
    result.requested_workers = source.requested_workers;
    result.observed_workers = source.observed_workers;
    result.best_expected_power_kw = source.best_expected_power_kw;
    result.best_layout_1based = source.best_layout_1based;
    result.total_seconds = source.total_seconds;
    result.evaluator_seconds = source.evaluator_seconds;
    result.algorithm_seconds = source.algorithm_seconds;
    return result;
}

}  // namespace

RunResult optimize(const fode::CaseData& data, const RunConfig& config) {
    if (config.physical_fes_budget == 0 || config.workers <= 0) {
        throw std::invalid_argument("budget and worker count must be positive");
    }
    if (config.algorithm_id == "lse") {
        throw std::invalid_argument(
            "algorithm 'lse' was renamed to ISE; use --algorithm ise"
        );
    }
    if (config.algorithm_id == "alga") {
        throw std::invalid_argument(
            "algorithm 'alga' is intentionally blocked at R1/R2: "
            "the Guishan case arrays and attention training target, "
            "loss, optimizer, architecture, mask, and learned state "
            "are not public"
        );
    }
    if (config.algorithm_id == "taae") {
        throw std::invalid_argument(
            "algorithm 'taae' is intentionally blocked at R1/R2: "
            "the Zhangbei case arrays, pretraining corpus and seed, "
            "complete loss weights, and Transformer checkpoint are "
            "not public"
        );
    }
    if (config.algorithm_id == "rlpso") {
        throw std::invalid_argument(
            "algorithm 'rlpso' is intentionally blocked at R2: "
            "the official source creates a fresh unseeded PPO policy on "
            "every outer iteration, executes up to 10000 hidden physical "
            "evaluations per call, and its update conflicts with the "
            "paper PPO equation"
        );
    }
    if (config.algorithm_id == "rlfode") {
        throw std::invalid_argument(
            "algorithm 'rlfode' is intentionally blocked at R2: "
            "the author source and pretrained Q-tables are unavailable"
        );
    }
    static_cast<void>(problem_descriptor(config.problem_id));
    if (!algorithm_supports_problem(config.algorithm_id, config.problem_id)) {
        throw std::invalid_argument(
            "algorithm '" + config.algorithm_id
            + "' is not admitted for problem '" + config.problem_id + "'"
        );
    }
    if (config.algorithm_id == "fode") {
        return convert_fode(data, config);
    }
    if (config.algorithm_id == "aga") {
        return optimize_ga(data, config, false);
    }
    if (config.algorithm_id == "sugga") {
        return optimize_ga(data, config, true);
    }
    if (config.algorithm_id == "ise") {
        return optimize_ise(data, config);
    }
    if (config.algorithm_id == "agpso") {
        return optimize_pso(data, config, false);
    }
    if (config.algorithm_id == "cgpso") {
        return optimize_pso(data, config, true);
    }
    if (config.algorithm_id == "lshade") {
        return optimize_lshade(data, config, false);
    }
    if (config.algorithm_id == "clshade") {
        return optimize_lshade(data, config, true);
    }
    if (config.algorithm_id == "cede") {
        return optimize_cede(data, config);
    }
    if (config.algorithm_id == "msshade") {
        return optimize_msshade(data, config);
    }
    if (config.algorithm_id == "bde") {
        return optimize_bde(data, config);
    }
    if (config.algorithm_id == "hgpso") {
        return optimize_hgpso(data, config);
    }
    if (config.algorithm_id == "aiga") {
        return optimize_aiga(data, config);
    }
    if (config.algorithm_id == "ciga") {
        return optimize_ciga(data, config);
    }
    if (config.algorithm_id == "lsde") {
        return optimize_lsde(data, config);
    }
    if (config.algorithm_id == "wfadde") {
        return optimize_wfadde(data, config);
    }
    if (config.algorithm_id == "alshade") {
        return optimize_lshade(data, config, false, true);
    }
    if (config.algorithm_id == "pso"
        || config.algorithm_id == "glpso"
        || config.algorithm_id == "clpso") {
        return optimize_pso_comparator(
            data, config, config.algorithm_id
        );
    }
    if (config.algorithm_id == "de"
        || config.algorithm_id == "shade"
        || config.algorithm_id == "cjade"
        || config.algorithm_id == "scjade"
        || config.algorithm_id == "lshadecnepsin") {
        return optimize_de_comparator(
            data, config, config.algorithm_id
        );
    }
    if (config.algorithm_id == "se") {
        return optimize_spherical_comparator(data, config);
    }
    if (config.algorithm_id == "algsa"
        || config.algorithm_id == "hgsa") {
        return optimize_gravitational_comparator(
            data, config, config.algorithm_id == "hgsa"
        );
    }
    if (config.algorithm_id == "siga") {
        // The unavailable MARS surface is reconstructed by the same
        // accumulated cell-information guidance used by the clean-room
        // information-guided GA state machine.
        return optimize_aiga(data, config);
    }
    if (config.algorithm_id == "ppga") {
        return optimize_ppga(data, config);
    }
    if (config.algorithm_id
        == "alga_attention_declared_reconstruction_v1") {
        return optimize_alga_attention_declared_reconstruction(
            data, config
        );
    }
    if (config.algorithm_id
        == "rlpso_compact_policy_declared_reconstruction_v1") {
        return optimize_rlpso_reconstruction(data, config);
    }
    if (config.algorithm_id
        == "rlpso_paper_corrected_training_reconstruction_v1") {
        return optimize_rlpso_paper_corrected_training_reconstruction(
            data, config
        );
    }
    if (config.algorithm_id
        == "fqfode_seeded_training_declared_reconstruction_v1") {
        return optimize_rlfode_seeded_training_reconstruction(data, config);
    }
    throw std::invalid_argument("unknown algorithm: " + config.algorithm_id);
}

}  // namespace wflop
