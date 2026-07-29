/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T42 seeded RPSO reconstruction profiles
Paper title: Reinforcement Learning-Based Particle Swarm Optimization for Wind Farm Layout Problems
DOI: 10.1016/j.energy.2024.134050
Paper provides: RPSO equations, PPO objective, four actions, 0.001 action step, population 50 and 200 iterations
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/RPSO_Wind_Code.zip
Public author code revision or archive hash: sha256:44e89c033e90f5aaaa9b84c826c95f29d3b8ad73dd363ff68de99418cdfa93a2
Public code/assets provide: MATLAB PSO lifecycle, Python PPO prototype and WS1-WS4 problem arrays
Known missing information: author-result seed lifecycle and a PPO update consistent with the paper
Reconstruction performed here: (1) the retained seeded linear categorical
  engineering proxy and (2) a separately identified persistent, seeded
  2-256-64 actor/critic PPO reconstruction using sampled-action log
  probabilities, gamma=0.99, clip=0.2, Adam, K=80, the paper's 0.001 action
  step, a clear-after-update on-policy buffer, and a complete FES ledger
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: P0_AUTHOR_ASSET
Method semantic IDs: rlpso_compact_policy_declared_reconstruction_v1 and
  rlpso_paper_corrected_training_reconstruction_v1
Problem semantic ID: rpso2024_source_problem_ws1_ws4_v1
Controlling contracts: shared/contracts/rlpso_reconstruction_execution_contract.json
Claim boundary: both profiles are declared M3 reconstructions; neither is an
  author checkpoint or reproduction of the paper's reported numerical results
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/algorithms.hpp"
#include "wflop/ppo.hpp"
#include "wflop/rlpso_transition.hpp"

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
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace wflop {
namespace {

using Matrix = std::vector<double>;
using Clock = std::chrono::steady_clock;

std::size_t index_of(int row, int column, int dimension) {
    return static_cast<std::size_t>(row * dimension + column);
}

std::vector<int> allowed_cells(const fode::CaseData& data) {
    std::vector<char> blocked(
        static_cast<std::size_t>(data.rows * data.cols), 0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    std::vector<int> allowed;
    for (int cell = 1; cell <= data.rows * data.cols; ++cell) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            allowed.push_back(cell);
        }
    }
    return allowed;
}

void repair(
    Matrix& values,
    int rows,
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    fode::PersistentExecutor& executor,
    std::uint64_t generation,
    std::uint64_t phase
) {
    const int dimension = data.turbine_count;
    const auto allowed = allowed_cells(data);
    executor.parallel_for(0, rows, [&](int row) {
        std::vector<std::pair<double, int>> candidates;
        candidates.reserve(allowed.size());
        for (std::size_t k = 0; k < allowed.size(); ++k) {
            const int cell = allowed[k];
            double distance = std::numeric_limits<double>::infinity();
            for (int d = 0; d < dimension; ++d) {
                distance = std::min(
                    distance,
                    std::abs(values[index_of(row, d, dimension)]
                             - static_cast<double>(cell))
                );
            }
            candidates.emplace_back(
                distance + 1.0e-12 * rng.uniform(
                    generation, phase, static_cast<std::uint64_t>(row), k
                ),
                cell
            );
        }
        std::stable_sort(candidates.begin(), candidates.end());
        std::vector<int> selected;
        selected.reserve(static_cast<std::size_t>(dimension));
        for (int d = 0; d < dimension; ++d) {
            selected.push_back(candidates[static_cast<std::size_t>(d)].second);
        }
        std::sort(selected.begin(), selected.end());
        for (int d = 0; d < dimension; ++d) {
            values[index_of(row, d, dimension)] =
                static_cast<double>(selected[static_cast<std::size_t>(d)]);
        }
    });
}

Matrix initialize(
    int population_size,
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    fode::PersistentExecutor& executor
) {
    const int dimension = data.turbine_count;
    const auto allowed = allowed_cells(data);
    Matrix population(
        static_cast<std::size_t>(population_size * dimension), 0.0
    );
    executor.parallel_for(0, population_size, [&](int row) {
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(allowed.size());
        for (std::size_t k = 0; k < allowed.size(); ++k) {
            keyed.emplace_back(
                rng.uniform(0, 1, static_cast<std::uint64_t>(row), k),
                allowed[k]
            );
        }
        std::stable_sort(keyed.begin(), keyed.end());
        std::vector<int> selected;
        for (int d = 0; d < dimension; ++d) {
            selected.push_back(keyed[static_cast<std::size_t>(d)].second);
        }
        std::sort(selected.begin(), selected.end());
        for (int d = 0; d < dimension; ++d) {
            population[index_of(row, d, dimension)] =
                static_cast<double>(selected[static_cast<std::size_t>(d)]);
        }
    });
    return population;
}

std::array<double, 4> probabilities(
    const std::array<double, 12>& actor,
    double r1,
    double r2
) {
    std::array<double, 4> result{};
    double maximum = -std::numeric_limits<double>::infinity();
    for (int action = 0; action < 4; ++action) {
        result[static_cast<std::size_t>(action)] =
            actor[static_cast<std::size_t>(3 * action)]
            + actor[static_cast<std::size_t>(3 * action + 1)] * r1
            + actor[static_cast<std::size_t>(3 * action + 2)] * r2;
        maximum = std::max(maximum, result[static_cast<std::size_t>(action)]);
    }
    double total = 0.0;
    for (double& value : result) {
        value = std::exp(value - maximum);
        total += value;
    }
    for (double& value : result) {
        value /= total;
    }
    return result;
}

int sample_action(
    const std::array<double, 4>& probability,
    double draw
) {
    double cumulative = 0.0;
    for (int action = 0; action < 4; ++action) {
        cumulative += probability[static_cast<std::size_t>(action)];
        if (draw <= cumulative) {
            return action;
        }
    }
    return 3;
}

std::string hexadecimal_hash(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] =
            digits[static_cast<std::size_t>(value & 0xfULL)];
        value >>= 4;
    }
    return "fnv1a64:" + result;
}

}  // namespace

RunResult optimize_rlpso_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
) {
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    fode::CounterRng rng(config.seed ^ 0x5250534fULL);
    const int population_size = static_cast<int>(
        std::min<std::uint64_t>(50, config.physical_fes_budget)
    );
    const int dimension = data.turbine_count;
    Matrix population = initialize(population_size, data, rng, executor);
    std::uint64_t fes = 0;
    std::uint64_t training_fes = 0;
    std::uint64_t generations = 0;
    double evaluator_seconds = 0.0;
    double best = -std::numeric_limits<double>::infinity();
    std::vector<int> best_layout;
    auto evaluate = [&](const Matrix& batch, int requested, bool training) {
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(requested),
            config.physical_fes_budget - fes
        ));
        Matrix prefix(
            batch.begin(),
            batch.begin()
                + static_cast<std::ptrdiff_t>(completed * dimension)
        );
        auto result = fode::evaluate_population_hpc(
            prefix, completed, data, executor, fode::EvaluationDetail::TotalOnly,
            fode::EvaluationSchedule::GranularityAware
        );
        evaluator_seconds += result.elapsed_seconds;
        for (int row = 0; row < completed; ++row) {
            if (result.fitness[static_cast<std::size_t>(row)] > best) {
                best = result.fitness[static_cast<std::size_t>(row)];
                best_layout.resize(static_cast<std::size_t>(dimension));
                for (int d = 0; d < dimension; ++d) {
                    best_layout[static_cast<std::size_t>(d)] =
                        static_cast<int>(std::llround(
                            prefix[index_of(row, d, dimension)]
                        ));
                }
            }
        }
        fes += static_cast<std::uint64_t>(completed);
        if (training) {
            training_fes += static_cast<std::uint64_t>(completed);
        }
        return result;
    };
    auto initial = evaluate(population, population_size, false);
    std::vector<double> fitness = initial.fitness;
    Matrix pbest = population;
    std::vector<double> pbest_fitness = fitness;
    Matrix velocity(population.size(), 0.0);
    int best_row = static_cast<int>(
        std::distance(
            pbest_fitness.begin(),
            std::max_element(pbest_fitness.begin(), pbest_fitness.end())
        )
    );
    Matrix gbest(static_cast<std::size_t>(dimension));
    std::copy_n(
        pbest.begin() + static_cast<std::ptrdiff_t>(best_row * dimension),
        dimension,
        gbest.begin()
    );
    std::array<double, 12> actor{};
    for (std::size_t k = 0; k < actor.size(); ++k) {
        actor[k] = 0.01 * rng.normal(0, 10, 0, k);
    }
    double r1 = 0.5;
    double r2 = 0.5;
    while (fes < config.physical_fes_budget) {
        ++generations;
        const int training_limit = 8;
        for (int step_index = 0;
             step_index < training_limit && fes < config.physical_fes_budget;
             ++step_index) {
            const auto old_probability = probabilities(actor, r1, r2);
            int action = sample_action(
                old_probability,
                rng.uniform(generations, 20, step_index)
            );
            const double old_r1 = r1;
            const double old_r2 = r2;
            double proposed_r1 = r1;
            double proposed_r2 = r2;
            rlpso_transition::apply_compact_proxy_action(
                action, proposed_r1, proposed_r2
            );
            const int row = step_index % population_size;
            const int peer = rng.integer(
                0, population_size, generations, 21,
                static_cast<std::uint64_t>(step_index)
            );
            Matrix candidate(static_cast<std::size_t>(dimension));
            for (int d = 0; d < dimension; ++d) {
                candidate[static_cast<std::size_t>(d)] =
                    pbest_fitness[static_cast<std::size_t>(row)]
                        > pbest_fitness[static_cast<std::size_t>(peer)]
                    ? rlpso_transition::source_candidate(
                        proposed_r1,
                        proposed_r2,
                        pbest[index_of(row, d, dimension)],
                        gbest[static_cast<std::size_t>(d)]
                    )
                    : pbest[index_of(peer, d, dimension)];
            }
            repair(
                candidate, 1, data, rng, executor, generations,
                static_cast<std::uint64_t>(30 + step_index)
            );
            const double prior = best;
            const auto observation = evaluate(candidate, 1, true);
            const double reward = observation.fitness[0] > prior ? 1.1 : -1.0;
            if (rlpso_transition::accept_training_candidate(
                    observation.fitness[0], prior
                )) {
                r1 = proposed_r1;
                r2 = proposed_r2;
                gbest = candidate;
                std::copy(
                    candidate.begin(),
                    candidate.end(),
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(best_row * dimension)
                );
                pbest_fitness[static_cast<std::size_t>(best_row)] =
                    observation.fitness[0];
            }
            const double learning_rate = 0.001 * reward;
            const std::array<double, 3> features{1.0, old_r1, old_r2};
            for (int output = 0; output < 4; ++output) {
                const double gradient =
                    ((output == action) ? 1.0 : 0.0)
                    - old_probability[static_cast<std::size_t>(output)];
                for (int feature = 0; feature < 3; ++feature) {
                    actor[static_cast<std::size_t>(3 * output + feature)]
                        += learning_rate * gradient
                        * features[static_cast<std::size_t>(feature)];
                }
            }
        }
        if (fes >= config.physical_fes_budget) {
            break;
        }
        const int batch = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            config.physical_fes_budget - fes
        ));
        Matrix offspring(static_cast<std::size_t>(batch * dimension), 0.0);
        executor.parallel_for(0, batch, [&](int row) {
            for (int d = 0; d < dimension; ++d) {
                const int peer = rng.integer(
                    0, population_size, generations, 40,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                offspring[index_of(row, d, dimension)] =
                    pbest_fitness[static_cast<std::size_t>(row)]
                        > pbest_fitness[static_cast<std::size_t>(peer)]
                    ? rlpso_transition::source_candidate(
                        r1,
                        r2,
                        pbest[index_of(row, d, dimension)],
                        gbest[static_cast<std::size_t>(d)]
                    )
                    : pbest[index_of(peer, d, dimension)];
            }
        });
        repair(offspring, batch, data, rng, executor, generations, 41);
        auto offspring_eval = evaluate(offspring, batch, false);
        for (int row = 0; row < batch; ++row) {
            if (offspring_eval.fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    offspring_eval.fitness[static_cast<std::size_t>(row)];
                std::copy_n(
                    offspring.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension),
                    dimension,
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension)
                );
            }
        }
        if (batch < population_size || fes >= config.physical_fes_budget) {
            break;
        }
        best_row = static_cast<int>(std::distance(
            pbest_fitness.begin(),
            std::max_element(pbest_fitness.begin(), pbest_fitness.end())
        ));
        std::copy_n(
            pbest.begin() + static_cast<std::ptrdiff_t>(best_row * dimension),
            dimension,
            gbest.begin()
        );
        executor.parallel_for(0, population_size * dimension, [&](int task) {
            const int row = task / dimension;
            const int d = task % dimension;
            velocity[index_of(row, d, dimension)] =
                rlpso_transition::source_velocity(
                0.9,
                velocity[index_of(row, d, dimension)],
                1.49618,
                rng.uniform(
                    generations, 50, static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                ),
                pbest[index_of(row, d, dimension)],
                1.49618,
                rng.uniform(
                    generations, 51, static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                ),
                population[index_of(row, d, dimension)]
            );
            population[index_of(row, d, dimension)] +=
                velocity[index_of(row, d, dimension)];
        });
        repair(population, population_size, data, rng, executor, generations, 52);
        const int update_batch = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            config.physical_fes_budget - fes
        ));
        auto updated = evaluate(population, update_batch, false);
        for (int row = 0; row < update_batch; ++row) {
            if (updated.fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    updated.fitness[static_cast<std::size_t>(row)];
                std::copy_n(
                    population.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension),
                    dimension,
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension)
                );
            }
        }
    }
    RunResult result;
    const auto& descriptor = algorithm_descriptor(config.algorithm_id);
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_id = config.algorithm_id;
    result.method_id = "RLPSO_COMPACT_POLICY_DECLARED_RECONSTRUCTION_V1";
    result.algorithm_provenance = descriptor.provenance;
    result.effective_semantics_id = descriptor.semantics_id;
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = fes;
    result.training_physical_fes = training_fes;
    result.inference_physical_fes = fes - training_fes;
    result.generations = generations;
    result.initial_population = population_size;
    result.final_population = population_size;
    result.requested_workers = config.workers;
    result.observed_workers = executor.thread_count();
    result.best_expected_power_kw = best;
    result.best_layout_1based = best_layout;
    result.total_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - evaluator_seconds);
    result.pso_update_semantics =
        "declared_linear_categorical_immediate_reward_policy_gradient_proxy";
    return result;
}

RunResult optimize_rlpso_training_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config,
    bool literal_source_replay
) {
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    fode::CounterRng rng(config.seed ^ 0x5250534f50504fULL);
    const int population_size = static_cast<int>(
        std::min<std::uint64_t>(50, config.physical_fes_budget)
    );
    const int dimension = data.turbine_count;
    Matrix population = initialize(population_size, data, rng, executor);
    std::uint64_t fes = 0;
    std::uint64_t training_fes = 0;
    std::uint64_t generations = 0;
    std::uint64_t training_step = 0;
    std::uint64_t policy_updates = 0;
    double evaluator_seconds = 0.0;
    double policy_training_seconds = 0.0;
    double policy_update_seconds = 0.0;
    double best = -std::numeric_limits<double>::infinity();
    std::vector<int> best_layout;
    auto evaluate = [&](const Matrix& batch, int requested, bool training) {
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(requested),
            config.physical_fes_budget - fes
        ));
        if (completed <= 0) {
            throw std::runtime_error(
                "paper-corrected RLPSO evaluation exhausted its FES budget"
            );
        }
        Matrix prefix(
            batch.begin(),
            batch.begin()
                + static_cast<std::ptrdiff_t>(completed * dimension)
        );
        auto result = fode::evaluate_population_hpc(
            prefix,
            completed,
            data,
            executor,
            fode::EvaluationDetail::TotalOnly,
            fode::EvaluationSchedule::GranularityAware
        );
        evaluator_seconds += result.elapsed_seconds;
        for (int row = 0; row < completed; ++row) {
            if (result.fitness[static_cast<std::size_t>(row)] > best) {
                best = result.fitness[static_cast<std::size_t>(row)];
                best_layout.resize(static_cast<std::size_t>(dimension));
                for (int d = 0; d < dimension; ++d) {
                    best_layout[static_cast<std::size_t>(d)] =
                        static_cast<int>(std::llround(
                            prefix[index_of(row, d, dimension)]
                        ));
                }
            }
        }
        fes += static_cast<std::uint64_t>(completed);
        if (training) {
            training_fes += static_cast<std::uint64_t>(completed);
        }
        return result;
    };

    auto initial = evaluate(population, population_size, false);
    std::vector<double> fitness = initial.fitness;
    Matrix pbest = population;
    std::vector<double> pbest_fitness = fitness;
    Matrix velocity(population.size(), 0.0);
    int best_row = static_cast<int>(std::distance(
        pbest_fitness.begin(),
        std::max_element(pbest_fitness.begin(), pbest_fitness.end())
    ));
    Matrix gbest(static_cast<std::size_t>(dimension));
    std::copy_n(
        pbest.begin() + static_cast<std::ptrdiff_t>(best_row * dimension),
        dimension,
        gbest.begin()
    );

    ppo::Hyperparameters policy_parameters;
    policy_parameters.literal_source_argmax_logprob_bug =
        literal_source_replay;
    auto make_policy = [&](std::uint64_t generation) {
        return std::make_unique<ppo::SeededPpo>(
            rng,
            literal_source_replay
                ? 0x50504fULL ^ generation
                : 0x50504fULL,
            policy_parameters
        );
    };
    std::unique_ptr<ppo::SeededPpo> policy =
        make_policy(0);
    std::vector<ppo::Transition> rollout;
    rollout.reserve(literal_source_replay ? 10000 : 500);
    auto update_policy = [&]() {
        const auto update_started = Clock::now();
        static_cast<void>(policy->update(rollout));
        const double elapsed = std::chrono::duration<double>(
            Clock::now() - update_started
        ).count();
        policy_update_seconds += elapsed;
        ++policy_updates;
        if (!literal_source_replay) {
            rollout.clear();
        }
        return elapsed;
    };
    double r1 = 0.5;
    double r2 = 0.5;

    while (fes < config.physical_fes_budget) {
        ++generations;
        if (literal_source_replay) {
            policy = make_policy(generations);
            rollout.clear();
        }
        const auto training_started = Clock::now();
        double source_cumulative_reward = -100.0;
        double source_previous_fitness = 1.01;
        constexpr int interactions_per_generation = 10000;
        for (int interaction = 0;
             interaction < interactions_per_generation
                 && fes < config.physical_fes_budget;
             ++interaction) {
            const std::array<double, 2> state{r1, r2};
            const auto sample = policy->sample_action(
                state,
                rng,
                ppo::RngKey{
                    generations,
                    20,
                    static_cast<std::uint64_t>(interaction),
                    0,
                    training_step
                }
            );
            if (literal_source_replay) {
                rlpso_transition::apply_literal_source_action(
                    sample.action, r1, r2
                );
            } else {
                rlpso_transition::apply_paper_corrected_action(
                    sample.action, r1, r2
                );
            }
            const int row = interaction % population_size;
            Matrix candidate(static_cast<std::size_t>(dimension));
            for (int d = 0; d < dimension; ++d) {
                const int peer = rng.integer(
                    0,
                    population_size,
                    generations,
                    21,
                    static_cast<std::uint64_t>(interaction),
                    static_cast<std::uint64_t>(d)
                );
                candidate[static_cast<std::size_t>(d)] =
                    pbest_fitness[static_cast<std::size_t>(row)]
                        > pbest_fitness[static_cast<std::size_t>(peer)]
                    ? (
                        literal_source_replay
                        ? rlpso_transition::source_candidate(
                            r1,
                            r2,
                            pbest[index_of(row, d, dimension)],
                            gbest[static_cast<std::size_t>(d)]
                        )
                        : rlpso_transition::paper_corrected_candidate(
                            r1,
                            r2,
                            pbest[index_of(row, d, dimension)],
                            gbest[static_cast<std::size_t>(d)]
                        )
                    )
                    : pbest[index_of(peer, d, dimension)];
            }
            repair(
                candidate,
                1,
                data,
                rng,
                executor,
                generations,
                static_cast<std::uint64_t>(30 + interaction)
            );
            const double prior_best = best;
            const auto observation = evaluate(candidate, 1, true);
            const double candidate_fitness = observation.fitness.front();
            double reward = candidate_fitness > prior_best ? 1.1 : -1.0;
            if (literal_source_replay) {
                if (candidate_fitness > source_previous_fitness) {
                    source_cumulative_reward += 1.1;
                } else if (candidate_fitness < source_previous_fitness) {
                    source_cumulative_reward -= 1.0;
                }
                source_previous_fitness = candidate_fitness;
                reward = source_cumulative_reward;
            }
            ++training_step;
            rollout.push_back(ppo::Transition{
                state,
                sample.action,
                sample.log_probability,
                reward,
                literal_source_replay
                    ? candidate_fitness > prior_best
                    : training_step % 100 == 0
            });
            if (candidate_fitness > prior_best) {
                best_row = row;
                gbest = candidate;
            }
            if (candidate_fitness
                    > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    candidate_fitness;
                std::copy(
                    candidate.begin(),
                    candidate.end(),
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension)
                );
            }
            if (rollout.size() == 500) {
                static_cast<void>(update_policy());
            } else if (
                literal_source_replay
                && rollout.size() > 500
                && rollout.size() % 500 == 0
            ) {
                static_cast<void>(update_policy());
            }
        }
        policy_training_seconds += std::chrono::duration<double>(
            Clock::now() - training_started
        ).count();
        if (fes >= config.physical_fes_budget) {
            break;
        }

        const int exemplar_batch = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                config.physical_fes_budget - fes
            )
        );
        Matrix offspring(
            static_cast<std::size_t>(exemplar_batch * dimension),
            0.0
        );
        executor.parallel_for(0, exemplar_batch, [&](int row) {
            for (int d = 0; d < dimension; ++d) {
                const int peer = rng.integer(
                    0,
                    population_size,
                    generations,
                    40,
                    static_cast<std::uint64_t>(row),
                    static_cast<std::uint64_t>(d)
                );
                offspring[index_of(row, d, dimension)] =
                    pbest_fitness[static_cast<std::size_t>(row)]
                        > pbest_fitness[static_cast<std::size_t>(peer)]
                    ? (
                        literal_source_replay
                        ? rlpso_transition::source_candidate(
                            r1,
                            r2,
                            pbest[index_of(row, d, dimension)],
                            gbest[static_cast<std::size_t>(d)]
                        )
                        : rlpso_transition::paper_corrected_candidate(
                            r1,
                            r2,
                            pbest[index_of(row, d, dimension)],
                            gbest[static_cast<std::size_t>(d)]
                        )
                    )
                    : pbest[index_of(peer, d, dimension)];
            }
        });
        repair(
            offspring,
            exemplar_batch,
            data,
            rng,
            executor,
            generations,
            41
        );
        const auto offspring_evaluation =
            evaluate(offspring, exemplar_batch, false);
        for (int row = 0; row < exemplar_batch; ++row) {
            if (offspring_evaluation.fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    offspring_evaluation.fitness[static_cast<std::size_t>(row)];
                std::copy_n(
                    offspring.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension),
                    dimension,
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension)
                );
            }
        }
        if (exemplar_batch < population_size
            || fes >= config.physical_fes_budget) {
            break;
        }

        best_row = static_cast<int>(std::distance(
            pbest_fitness.begin(),
            std::max_element(pbest_fitness.begin(), pbest_fitness.end())
        ));
        std::copy_n(
            pbest.begin() + static_cast<std::ptrdiff_t>(best_row * dimension),
            dimension,
            gbest.begin()
        );
        executor.parallel_for(
            0,
            population_size * dimension,
            [&](int task) {
                const int row = task / dimension;
                const int d = task % dimension;
                velocity[index_of(row, d, dimension)] =
                    rlpso_transition::source_velocity(
                        0.9,
                        velocity[index_of(row, d, dimension)],
                        1.49618,
                        rng.uniform(
                            generations,
                            50,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ),
                        pbest[index_of(row, d, dimension)],
                        1.49618,
                        rng.uniform(
                            generations,
                            51,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(d)
                        ),
                        population[index_of(row, d, dimension)]
                    );
                population[index_of(row, d, dimension)] +=
                    velocity[index_of(row, d, dimension)];
            }
        );
        repair(
            population,
            population_size,
            data,
            rng,
            executor,
            generations,
            52
        );
        const int population_batch = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                config.physical_fes_budget - fes
            )
        );
        const auto population_evaluation =
            evaluate(population, population_batch, false);
        for (int row = 0; row < population_batch; ++row) {
            if (population_evaluation.fitness[static_cast<std::size_t>(row)]
                > pbest_fitness[static_cast<std::size_t>(row)]) {
                pbest_fitness[static_cast<std::size_t>(row)] =
                    population_evaluation.fitness[static_cast<std::size_t>(row)];
                std::copy_n(
                    population.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension),
                    dimension,
                    pbest.begin()
                        + static_cast<std::ptrdiff_t>(row * dimension)
                );
            }
        }
    }

    if (!literal_source_replay && !rollout.empty()) {
        rollout.back().terminal = true;
        policy_training_seconds += update_policy();
    }

    RunResult result;
    const auto& descriptor = algorithm_descriptor(config.algorithm_id);
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_id = config.algorithm_id;
    result.method_id =
        literal_source_replay
            ? "RLPSO_LITERAL_OFFICIAL_SOURCE_REPLAY_V1"
            : "RLPSO_PAPER_CORRECTED_TRAINING_RECONSTRUCTION_V1";
    result.algorithm_provenance = descriptor.provenance;
    result.effective_semantics_id = descriptor.semantics_id;
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = fes;
    result.training_physical_fes = training_fes;
    result.inference_physical_fes = fes - training_fes;
    result.policy_interactions = training_step;
    result.policy_updates = policy_updates;
    result.generations = generations;
    result.initial_population = population_size;
    result.final_population = population_size;
    result.requested_workers = config.workers;
    result.observed_workers = executor.thread_count();
    result.best_expected_power_kw = best;
    result.best_layout_1based = best_layout;
    result.total_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - evaluator_seconds);
    result.policy_training_seconds = policy_training_seconds;
    result.policy_update_seconds = policy_update_seconds;
    result.pso_update_semantics = literal_source_replay
        ? "literal_source_reinitialized_ppo_step001_argmax_bug_memory_reuse"
        : "paper_corrected_seeded_persistent_ppo_staged_parallel";
    result.learned_state_hash = hexadecimal_hash(policy->parameter_hash());
    return result;
}

RunResult optimize_rlpso_paper_corrected_training_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
) {
    return optimize_rlpso_training_reconstruction(data, config, false);
}

RunResult optimize_rlpso_literal_source_replay(
    const fode::CaseData& data,
    const RunConfig& config
) {
    return optimize_rlpso_training_reconstruction(data, config, true);
}

}  // namespace wflop
