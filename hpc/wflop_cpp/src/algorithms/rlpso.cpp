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
Known paper/source conflicts: the paper and archive differ in action step,
random seed handling, sampled-action log probability, buffer clearing, and
PPO lifecycle.
Reconstruction performed here: (1) the retained seeded linear categorical
  engineering proxy and (2) a separately identified persistent, seeded
  2-256-64 actor/critic PPO reconstruction using sampled-action log
  probabilities, gamma=0.99, clip=0.2, Adam, K=80, the paper's 0.001 action
  step, a clear-after-update on-policy buffer, and a complete FES ledger
Plan-004 artifact path: the paper-corrected profile can load the typed
LibTorch actor, critic, and Adam state; sampled actions then drive the same
real repaired candidate, native evaluator, pbest/gbest, exact-FES terminal
partial rollout, and final 80-epoch PPO update. Literal-source replay rejects
this artifact path.
Method evidence tier: M3_DECLARED_COMPLETION
Problem evidence tier: P0_AUTHOR_ASSET
Method semantic IDs: rlpso_compact_policy_declared_reconstruction_v1 and
  rlpso_paper_corrected_training_reconstruction_v1
Problem semantic ID: rpso2024_source_problem_ws1_ws4_v1
Training semantic ID: rlpso_train_from_scratch_v1
Production backend: persistent-worker C++ CPU plus target LibTorch C++/CPU
PPO; LibTorch C++/CUDA and CPU-CUDA hybrid profiles require end-to-end
validation and never silently fall back to CPU
Controlling contracts: shared/contracts/rlpso_reconstruction_execution_contract.json
Claim boundary: both profiles are academically fixed train-from-scratch M3
reconstructions; unavailable author policy state limits author-original
attribution but does not block formal baseline execution
Last evidence audit date: 2026-07-29
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/algorithms.hpp"
#include "wflop/ppo.hpp"
#include "wflop/rlpso_transition.hpp"

#include "fode/evaluator.hpp"
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#ifdef WFLOP_PLAN004_LIBTORCH
#include "wflop_learning/models.hpp"
#endif

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

void mix_decision(std::uint64_t& hash, std::uint64_t value) {
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= value & 0xffULL;
        hash *= 1099511628211ULL;
        value >>= 8;
    }
}

#ifdef WFLOP_PLAN004_LIBTORCH
class LibTorchRlpsoPolicy {
public:
    LibTorchRlpsoPolicy(
        const std::string& artifact_path,
        std::uint64_t seed,
        int batch_threads
    )
        : model_(seed),
          optimizer_(
              model_->parameters(),
              torch::optim::AdamOptions(1.0e-3)
          ),
          batch_threads_(batch_threads) {
        model_->to(torch::kFloat64);
        static_cast<void>(wflop_learning::load_artifact(
            *model_,
            optimizer_,
            wflop_learning::ModelKind::Rlpso,
            artifact_path,
            torch::Device(torch::kCPU)
        ));
        static_cast<void>(
            wflop_learning::set_torch_intraop_threads(1)
        );
    }

    ppo::ActionSample sample_action(
        const std::array<double, 2>& state,
        double draw
    ) {
        torch::NoGradGuard no_grad;
        model_->eval();
        torch::Tensor state_tensor = torch::tensor(
            {{state[0], state[1]}},
            torch::TensorOptions().dtype(torch::kFloat64)
        );
        const wflop_learning::RlpsoOutput output =
            model_->forward(state_tensor);
        const torch::Tensor probability =
            output.probabilities.to(torch::kCPU).contiguous();
        ppo::ActionSample sample;
        for (int action = 0; action < 4; ++action) {
            sample.evaluation.probabilities[
                static_cast<std::size_t>(action)
            ] = probability[0][action].item<double>();
        }
        sample.evaluation.value = output.value[0].item<double>();
        sample.action = ::wflop::sample_action(
            sample.evaluation.probabilities,
            draw
        );
        sample.log_probability = std::log(std::max(
            sample.evaluation.probabilities[
                static_cast<std::size_t>(sample.action)
            ],
            1.0e-12
        ));
        return sample;
    }

    void update(const std::vector<ppo::Transition>& rollout) {
        if (rollout.empty()) {
            return;
        }
        static_cast<void>(
            wflop_learning::set_torch_intraop_threads(batch_threads_)
        );
        const std::int64_t count =
            static_cast<std::int64_t>(rollout.size());
        std::vector<double> state;
        std::vector<std::int64_t> action;
        std::vector<double> old_log_probability;
        std::vector<double> reward;
        std::vector<std::uint8_t> terminal;
        state.reserve(rollout.size() * 2);
        action.reserve(rollout.size());
        old_log_probability.reserve(rollout.size());
        reward.reserve(rollout.size());
        terminal.reserve(rollout.size());
        for (const ppo::Transition& value : rollout) {
            state.push_back(value.state[0]);
            state.push_back(value.state[1]);
            action.push_back(value.action);
            old_log_probability.push_back(value.old_log_probability);
            reward.push_back(value.reward);
            terminal.push_back(value.terminal ? 1U : 0U);
        }
        const auto floating =
            torch::TensorOptions().dtype(torch::kFloat64);
        const auto integer =
            torch::TensorOptions().dtype(torch::kInt64);
        wflop_learning::PpoBatch batch{
            torch::from_blob(state.data(), {count, 2}, floating).clone(),
            torch::from_blob(action.data(), {count}, integer).clone(),
            torch::from_blob(
                old_log_probability.data(), {count}, floating
            ).clone(),
            {},
            {},
        };
        torch::Tensor reward_tensor =
            torch::from_blob(reward.data(), {count}, floating).clone();
        torch::Tensor terminal_tensor = torch::from_blob(
            terminal.data(),
            {count},
            torch::TensorOptions().dtype(torch::kUInt8)
        ).clone().to(torch::kBool);
        batch.returns =
            wflop_learning::rlpso_discounted_normalized_returns(
                reward_tensor,
                terminal_tensor
            );
        {
            torch::NoGradGuard no_grad;
            batch.advantage = batch.returns
                - model_->forward(batch.state).value;
        }
        model_->train();
        for (std::int64_t epoch = 0;
             epoch < wflop_learning::kRlpsoUpdateEpochs;
             ++epoch) {
            optimizer_.zero_grad();
            const wflop_learning::PpoLoss loss =
                wflop_learning::rlpso_ppo_loss(
                    model_->forward(batch.state),
                    batch
                );
            loss.total.backward();
            optimizer_.step();
        }
        static_cast<void>(
            wflop_learning::set_torch_intraop_threads(1)
        );
    }

    std::string parameter_hash() const {
        return wflop_learning::learned_state_hash(*model_);
    }

private:
    wflop_learning::RlpsoActorCritic model_;
    torch::optim::Adam optimizer_;
    int batch_threads_;
};
#endif

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
    if (literal_source_replay && !config.learning_artifact_path.empty()) {
        throw std::invalid_argument(
            "Plan-004 RLPSO artifacts are only valid for the "
            "paper-corrected training reconstruction"
        );
    }
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
    std::uint64_t learning_decision_hash = 1469598103934665603ULL;
    int terminal_training_interactions = 0;
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
    std::unique_ptr<ppo::SeededPpo> policy;
#ifdef WFLOP_PLAN004_LIBTORCH
    std::unique_ptr<LibTorchRlpsoPolicy> artifact_policy;
    if (!config.learning_artifact_path.empty()) {
        artifact_policy = std::make_unique<LibTorchRlpsoPolicy>(
            config.learning_artifact_path,
            config.seed,
            config.torch_intraop_threads
        );
    } else {
        policy = make_policy(0);
    }
#else
    if (!config.learning_artifact_path.empty()) {
        throw std::invalid_argument(
            "RLPSO learning artifact requires WFLOP_ENABLE_TORCH"
        );
    }
    policy = make_policy(0);
#endif
    std::vector<ppo::Transition> rollout;
    rollout.reserve(literal_source_replay ? 10000 : 500);
    auto update_policy = [&]() {
        const auto update_started = Clock::now();
#ifdef WFLOP_PLAN004_LIBTORCH
        if (artifact_policy) {
            artifact_policy->update(rollout);
        } else
#endif
        {
            static_cast<void>(policy->update(rollout));
        }
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
            terminal_training_interactions = interaction + 1;
            const std::array<double, 2> state{r1, r2};
            ppo::ActionSample sample;
#ifdef WFLOP_PLAN004_LIBTORCH
            if (artifact_policy) {
                sample = artifact_policy->sample_action(
                    state,
                    rng.uniform(
                        generations,
                        20,
                        static_cast<std::uint64_t>(interaction),
                        0,
                        training_step
                    )
                );
            } else
#endif
            {
                sample = policy->sample_action(
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
            }
            mix_decision(
                learning_decision_hash,
                static_cast<std::uint64_t>(sample.action)
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
#ifdef WFLOP_PLAN004_LIBTORCH
    if (artifact_policy) {
        result.learning_artifact_consumed = true;
        result.learning_decision_hash =
            hexadecimal_hash(learning_decision_hash);
        result.learned_state_hash = artifact_policy->parameter_hash();
    } else
#endif
    {
        result.learned_state_hash =
            hexadecimal_hash(policy->parameter_hash());
    }
    result.terminal_partial_work =
        terminal_training_interactions < 10000
        ? "exact_fes_partial_training_interactions="
            + std::to_string(terminal_training_interactions)
            + "_of_10000"
        : "exact_fes_full_training_block";
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
