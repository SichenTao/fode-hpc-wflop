#include "wflop/algorithms.hpp"

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

std::pair<std::string, std::string> registered_identity(
    const std::string& algorithm
) {
    if (algorithm == "fode") {
        return {
            "archived_matlab_source",
            "fode_e0_physical_fes"
        };
    }
    if (algorithm == "aga") {
        return {
            "paper_first_archived_matlab_completed",
            "aga_paper_first_e0_physical_fes_v1"
        };
    }
    if (algorithm == "sugga") {
        return {
            "archived_matlab_source_and_frozen_surrogate",
            "sugga_frozen_surrogate_e0_physical_fes_v1"
        };
    }
    if (algorithm == "ise") {
        return {
            "paper_derived",
            "ise_paper_derived_e0_physical_fes_v1"
        };
    }
    if (algorithm == "agpso") {
        return {
            "paper_first_source_completed",
            "agpso_paper_staged_parallel_e0_physical_fes_v1"
        };
    }
    if (algorithm == "cgpso") {
        return {
            "paper_first_source_completed",
            "cgpso_paper_staged_parallel_e0_physical_fes_v1"
        };
    }
    if (algorithm == "lshade") {
        return {
            "paper_first_archived_matlab_completed",
            "lshade_paper_first_e0_physical_fes_v1"
        };
    }
    if (algorithm == "clshade") {
        return {
            "paper_derived",
            "clshade_paper_derived_e0_physical_fes_v1"
        };
    }
    throw std::invalid_argument("unregistered algorithm identity: " + algorithm);
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
        fode::EvaluationDetail detail
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
        for (int row = 0; row < completed; ++row) {
            const double value = result.fitness[static_cast<std::size_t>(row)];
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
    const auto identity = registered_identity(config.algorithm_id);
    result.algorithm_provenance = identity.first;
    result.effective_semantics_id = identity.second;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = runtime.fes;
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
    bool chaotic
) {
    const auto started = Clock::now();
    Runtime runtime(config);
    const int dimension = data.turbine_count;
    const int initial_size = 18 * dimension;
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
    const auto identity = registered_identity("fode");
    result.algorithm_provenance = identity.first;
    result.effective_semantics_id = identity.second;
    result.case_id = source.case_id;
    result.seed = source.seed;
    result.physical_fes = source.physical_fes;
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

const std::vector<std::string>& algorithm_ids() {
    static const std::vector<std::string> ids{
        "fode", "aga", "sugga", "ise",
        "agpso", "cgpso", "lshade", "clshade"
    };
    return ids;
}

RunResult optimize(const fode::CaseData& data, const RunConfig& config) {
    if (config.physical_fes_budget == 0 || config.workers <= 0) {
        throw std::invalid_argument("budget and worker count must be positive");
    }
    if (config.algorithm_id == "lse") {
        throw std::invalid_argument(
            "algorithm 'lse' was renamed to ISE; use --algorithm ise"
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
    throw std::invalid_argument("unknown algorithm: " + config.algorithm_id);
}

}  // namespace wflop
