/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: BDE WS5/WS6 declared-proxy state machine
Paper title: Discrete Bi-Population Differential Evolution for Optimizing
Complex Wind Farm Layouts in Diverse Terrains
DOI: 10.1016/j.energy.2025.137885
Paper provides: Eqs.18-27, L=50, FES=10000, Imax=400, p=0.05, and CR0=0.1.
Public author code URL: https://raw.githubusercontent.com/toyamaailab/toyamaailab.github.io/main/resource/BDE-WindFarm_code.zip
Public author code revision or archive hash: sha256:f4a317d4d727a9d452f76376373e2c8ad5546e35ff19530eda0ba328682dd140
Public code/assets provide: half-population fusion indexing, repair/evaluator
behavior, and a conflicting decreasing scale schedule and 10050-call driver.
Known missing information: exact WS5/WS6 arrays and author conflict
adjudications.
Reconstruction performed here: literal paper Imax=400 schedules, source-
resolved 12/13 fusion, exact complete-layout FES, counter RNG, and CPU receipts.
Method evidence tier: M2_CITATION_PREDECESSOR subtype
paper_equation_direct_source_resolved.
Problem evidence tier: P3_DECLARED_PROXY subtype composite_proxy.
Method semantic ID: bde_paper_equations_imax400_exact_fes_v1
Problem semantic ID: bde2025_ws5_paper250_declared_proxy_v1 or
bde2025_ws6_paper250_declared_proxy_v1.
Controlling contracts: shared/contracts/bde_ws56_declared_proxy_contract.json
and shared/contracts/bde_ws56_transition_parity_audit.json
Claim boundary: distinct method on P3 composite cases only; no original-array
or paper-result reproduction and no WS1-WS4 pooling.
Last evidence audit date: 2026-07-29
Paper schedule: L=50 and Imax=2*10000/L=400. Exact physical FES includes
the 50 initial complete-layout evaluations, so a 10000-FES run executes
398 half-population generations. Imax remains 400 in Eqs. 23 and 25.
Fusion resolution: source-visible half-sized counts are used to resolve
Eq. 20: floor(0.5*25)=12 superior plus 13 inferior in G-prime, with the
complementary 13 plus 12 in B-prime.
Parallel completion: persistent pure-CPU executor, counter-keyed
schedule-independent RNG, fixed-order reductions, and actual stage/work
receipts. A physical FES is one complete layout over every wind state.
Claim boundary: the P3 WS5/WS6 problems remain separate from official-source
WS1--WS4 replay and cannot be pooled with it.
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "bde_ws56/evolution.hpp"

#include "fode/evaluator.hpp"
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace bde_ws56 {
namespace {

using Clock = std::chrono::steady_clock;
using Matrix = std::vector<double>;

constexpr int kPopulationSize = 50;
constexpr int kHalfSize = 25;
constexpr std::uint64_t kPaperImax = 400;
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::size_t at(int row, int column, int dimension) {
    return static_cast<std::size_t>(row * dimension + column);
}

void fnv_bytes(std::uint64_t& hash, const void* source, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(source);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
}

template <typename Value>
void fnv_value(std::uint64_t& hash, const Value& value) {
    fnv_bytes(hash, &value, sizeof(value));
}

void fnv_string(std::uint64_t& hash, std::string_view value) {
    fnv_bytes(hash, value.data(), value.size());
}

std::string hex_hash(std::uint64_t hash) {
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

std::uint64_t algorithm_salt(std::string_view algorithm) {
    std::uint64_t value = kFnvOffset;
    for (const unsigned char byte : algorithm) {
        value ^= static_cast<std::uint64_t>(byte);
        value *= kFnvPrime;
    }
    return value;
}

std::vector<char> blocked_mask(const fode::CaseData& data) {
    std::vector<char> blocked(
        static_cast<std::size_t>(data.rows * data.cols), 0
    );
    for (const int cell : data.unavailable_cells_1based) {
        blocked[static_cast<std::size_t>(cell - 1)] = 1;
    }
    return blocked;
}

std::vector<int> available_cells(const fode::CaseData& data) {
    const std::vector<char> blocked = blocked_mask(data);
    std::vector<int> result;
    for (int cell = 1; cell <= data.rows * data.cols; ++cell) {
        if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
            result.push_back(cell);
        }
    }
    return result;
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

StageReceipt stage_receipt(
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

template <typename Task>
StageReceipt timed_parallel(
    fode::PersistentExecutor& executor,
    int begin,
    int end,
    Task task
) {
    executor.reset_work_receipt();
    const auto started = Clock::now();
    executor.parallel_for(begin, end, task);
    return stage_receipt(
        std::chrono::duration<double>(Clock::now() - started).count(),
        executor
    );
}

template <typename Task>
StageReceipt timed_granularity_aware(
    fode::PersistentExecutor& executor,
    int begin,
    int end,
    int logical_work,
    Task task
) {
    constexpr int kParallelWorkThreshold = 2048;
    if (logical_work >= kParallelWorkThreshold) {
        return timed_parallel(executor, begin, end, task);
    }
    executor.reset_work_receipt();
    const auto started = Clock::now();
    for (int item = begin; item < end; ++item) {
        task(item);
    }
    StageReceipt receipt;
    receipt.wall_seconds = std::chrono::duration<double>(
        Clock::now() - started
    ).count();
    receipt.task_items =
        static_cast<std::uint64_t>(std::max(0, end - begin));
    receipt.participant_activations = end > begin ? 1 : 0;
    receipt.distinct_participants = end > begin ? 1 : 0;
    receipt.peak_region_participants = end > begin ? 1 : 0;
    return receipt;
}

std::vector<int> stable_rank_descending(
    const std::vector<double>& fitness
) {
    std::vector<int> order(fitness.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        return fitness[static_cast<std::size_t>(left)]
            > fitness[static_cast<std::size_t>(right)];
    });
    return order;
}

void copy_row(
    Matrix& target,
    int target_row,
    const Matrix& source,
    int source_row,
    int dimension
) {
    std::copy_n(
        source.begin()
            + static_cast<std::ptrdiff_t>(source_row * dimension),
        dimension,
        target.begin()
            + static_cast<std::ptrdiff_t>(target_row * dimension)
    );
}

std::string layout_hash(const std::vector<int>& layout) {
    std::uint64_t hash = kFnvOffset;
    for (const int cell : layout) {
        fnv_value(hash, cell);
    }
    return hex_hash(hash);
}

std::string population_hash(
    const Matrix& population,
    int dimension
) {
    std::uint64_t hash = kFnvOffset;
    for (int row = 0; row < kPopulationSize; ++row) {
        for (int column = 0; column < dimension; ++column) {
            const int cell = static_cast<int>(std::llround(
                population[at(row, column, dimension)]
            ));
            fnv_value(hash, cell);
        }
    }
    return hex_hash(hash);
}

std::string escape_json(std::string_view value) {
    std::ostringstream output;
    for (const char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\';
        }
        output << character;
    }
    return output.str();
}

std::string stage_json(const StageReceipt& stage) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"wall_seconds\":" << stage.wall_seconds
           << ",\"parallel_regions\":" << stage.parallel_regions
           << ",\"task_items\":" << stage.task_items
           << ",\"participant_activations\":"
           << stage.participant_activations
           << ",\"distinct_participants\":"
           << stage.distinct_participants
           << ",\"peak_region_participants\":"
           << stage.peak_region_participants << '}';
    return output.str();
}

int resolve_workers(int requested) {
    if (requested < 0) {
        throw std::runtime_error("workers cannot be negative");
    }
    if (requested > 0) {
        return requested;
    }
    const unsigned int visible = std::thread::hardware_concurrency();
    return std::max(1, static_cast<int>(visible));
}

void validate_case_identity(const fode::CaseData& data) {
    const bool ws5 = data.case_id.rfind("BDEWS5P3", 0) == 0;
    const bool ws6 = data.case_id.rfind("BDEWS6P3", 0) == 0;
    if (!ws5 && !ws6) {
        throw std::runtime_error(
            "case is outside the isolated BDE WS5/WS6 P3 namespace"
        );
    }
    if ((ws5 && (data.velocity.size() != 8 || data.theta.size() != 12))
        || (ws6
            && (data.velocity.size() != 8 || data.theta.size() != 16))) {
        throw std::runtime_error("case wind cardinality violates identity");
    }
    const bool standard = data.case_id.find("STD") != std::string::npos;
    const bool daeg = data.case_id.find("DAE") != std::string::npos;
    if ((standard
         && (data.rows != 21 || data.cols != 21
             || data.cell_width != 231.0))
        || (daeg
            && (data.rows != 28 || data.cols != 28
                || data.cell_width != 250.0))
        || (!standard && !daeg)) {
        throw std::runtime_error("case terrain/spacing identity mismatch");
    }
}

}  // namespace

std::string problem_semantic_id(const fode::CaseData& data) {
    validate_case_identity(data);
    return data.case_id.rfind("BDEWS5P3", 0) == 0
        ? kWs5ProblemSemanticId
        : kWs6ProblemSemanticId;
}

double turbine_power_kw(double wind_speed_mps) {
    if (wind_speed_mps < 2.0 || wind_speed_mps >= 18.0) {
        return 0.0;
    }
    if (wind_speed_mps < 12.8) {
        return 0.3 * wind_speed_mps * wind_speed_mps * wind_speed_mps;
    }
    return 629.1;
}

double no_wake_expected_power_kw(const fode::CaseData& data) {
    validate_case_identity(data);
    double per_turbine = 0.0;
    const std::size_t speeds = data.velocity.size();
    for (std::size_t direction = 0;
         direction < data.theta.size();
         ++direction) {
        for (std::size_t speed = 0; speed < speeds; ++speed) {
            per_turbine +=
                turbine_power_kw(data.velocity[speed])
                * data.probability[direction * speeds + speed];
        }
    }
    return static_cast<double>(data.turbine_count) * per_turbine;
}

std::string objective_semantics_hash(const fode::CaseData& data) {
    validate_case_identity(data);
    std::uint64_t hash = kFnvOffset;
    fnv_string(
        hash,
        "park2019_partial_overlap_jensen_rss_ge1p5sle_"
        "rotor77_hub80_roughness0p00025_cut2_rated12p8_cutout18_"
        "cubic0p3_rated629p1_expected_total_power_kw_fixed_order"
    );
    fnv_value(hash, data.rows);
    fnv_value(hash, data.cols);
    fnv_value(hash, data.turbine_count);
    fnv_value(hash, std::bit_cast<std::uint64_t>(data.cell_width));
    for (const double value : data.theta) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (const double value : data.velocity) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (const double value : data.probability) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hex_hash(hash);
}

std::string feasible_set_hash(const fode::CaseData& data) {
    validate_case_identity(data);
    std::uint64_t hash = kFnvOffset;
    fnv_string(
        hash,
        "sorted_unique_one_based_grid_indices_exact_turbine_count"
    );
    fnv_value(hash, data.rows);
    fnv_value(hash, data.cols);
    fnv_value(hash, data.turbine_count);
    fnv_value(hash, std::bit_cast<std::uint64_t>(data.cell_width));
    const std::vector<char> blocked = blocked_mask(data);
    for (int cell = 1; cell <= data.rows * data.cols; ++cell) {
        fnv_value(hash, cell);
        fnv_value(hash, blocked[static_cast<std::size_t>(cell - 1)]);
    }
    return hex_hash(hash);
}

double evaluate_layout(
    const fode::CaseData& data,
    const std::vector<int>& layout_1based,
    int workers
) {
    validate_case_identity(data);
    if (layout_1based.size()
        != static_cast<std::size_t>(data.turbine_count)) {
        throw std::runtime_error("layout turbine count mismatch");
    }
    std::vector<int> sorted = layout_1based;
    std::sort(sorted.begin(), sorted.end());
    if (sorted != layout_1based
        || std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::runtime_error("layout must be sorted and unique");
    }
    const std::vector<char> blocked = blocked_mask(data);
    for (const int cell : sorted) {
        if (cell < 1 || cell > data.rows * data.cols
            || blocked[static_cast<std::size_t>(cell - 1)] != 0) {
            throw std::runtime_error("layout contains infeasible cell");
        }
    }
    Matrix values;
    values.reserve(sorted.size());
    for (const int cell : sorted) {
        values.push_back(static_cast<double>(cell));
    }
    fode::PersistentExecutor executor(resolve_workers(workers));
    const fode::Evaluation evaluation = fode::evaluate_population_hpc(
        values,
        1,
        data,
        executor,
        fode::EvaluationDetail::TotalOnly,
        fode::EvaluationSchedule::GranularityAware
    );
    return evaluation.fitness.front();
}

Result run(const Config& config, const fode::CaseData& data) {
    validate_case_identity(data);
    if (config.execution_mode != "cpu"
        && config.execution_mode != "auto") {
        throw std::runtime_error(
            "execution mode " + config.execution_mode
            + " is unsupported; admitted modes are cpu and auto-to-cpu"
        );
    }
    if (config.physical_fes < kPopulationSize) {
        throw std::runtime_error("budget is below BDE initialization");
    }
    const int resolved_workers = resolve_workers(config.workers);
    fode::PersistentExecutor executor(resolved_workers);
    const fode::CounterRng rng(config.seed ^ algorithm_salt("bde"));
    const int dimension = data.turbine_count;
    const std::vector<int> available = available_cells(data);
    if (static_cast<int>(available.size()) < dimension) {
        throw std::runtime_error("insufficient feasible cells");
    }

    Result result;
    result.method_semantic_id = kMethodSemanticId;
    result.execution_profile_id = kExecutionProfileId;
    result.problem_semantic_id = problem_semantic_id(data);
    result.case_id = data.case_id;
    result.objective_semantics_hash = objective_semantics_hash(data);
    result.feasible_set_hash = feasible_set_hash(data);
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.resolved_workers = resolved_workers;
    result.requested_execution_mode = config.execution_mode;
    result.resolved_execution_mode = "cpu";
    const auto total_started = Clock::now();

    Matrix population(
        static_cast<std::size_t>(kPopulationSize * dimension), 0.0
    );
    result.initialization_stage = timed_parallel(
        executor, 0, kPopulationSize, [&](int individual) {
            std::vector<std::pair<double, int>> keys;
            keys.reserve(available.size());
            for (std::size_t index = 0; index < available.size(); ++index) {
                keys.emplace_back(
                    rng.uniform(
                        0,
                        900,
                        static_cast<std::uint64_t>(individual),
                        static_cast<std::uint64_t>(index)
                    ),
                    available[index]
                );
            }
            std::stable_sort(keys.begin(), keys.end());
            std::vector<int> row;
            row.reserve(static_cast<std::size_t>(dimension));
            for (int column = 0; column < dimension; ++column) {
                row.push_back(
                    keys[static_cast<std::size_t>(column)].second
                );
            }
            std::sort(row.begin(), row.end());
            for (int column = 0; column < dimension; ++column) {
                population[at(individual, column, dimension)] =
                    static_cast<double>(
                        row[static_cast<std::size_t>(column)]
                    );
            }
        }
    );

    double best = -std::numeric_limits<double>::infinity();
    std::vector<int> best_layout;
    std::uint64_t fes = 0;
    auto evaluate = [&](const Matrix& candidates, int rows) {
        const int completed = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(rows),
            config.physical_fes - fes
        ));
        Matrix prefix(
            candidates.begin(),
            candidates.begin()
                + static_cast<std::ptrdiff_t>(completed * dimension)
        );
        executor.reset_work_receipt();
        const auto started = Clock::now();
        fode::Evaluation evaluation = fode::evaluate_population_hpc(
            prefix,
            completed,
            data,
            executor,
            fode::EvaluationDetail::TotalOnly,
            fode::EvaluationSchedule::GranularityAware
        );
        add_stage(
            result.evaluator_stage,
            stage_receipt(
                std::chrono::duration<double>(
                    Clock::now() - started
                ).count(),
                executor
            )
        );
        for (int row = 0; row < completed; ++row) {
            const double value =
                evaluation.fitness[static_cast<std::size_t>(row)];
            if (value > best) {
                best = value;
                best_layout.resize(static_cast<std::size_t>(dimension));
                for (int column = 0; column < dimension; ++column) {
                    best_layout[static_cast<std::size_t>(column)] =
                        static_cast<int>(std::llround(
                            candidates[at(row, column, dimension)]
                        ));
                }
            }
        }
        fes += static_cast<std::uint64_t>(completed);
        result.work.complete_layout_evaluations +=
            static_cast<std::uint64_t>(completed);
        return evaluation.fitness;
    };

    std::vector<double> fitness = evaluate(population, kPopulationSize);
    while (fes < config.physical_fes) {
        ++result.generations;
        const auto selection_started = Clock::now();
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                kHalfSize, config.physical_fes - fes
            )
        );
        const std::vector<int> ranking =
            stable_rank_descending(fitness);
        result.work.ranked_individuals += kPopulationSize;
        std::vector<int> superior(
            ranking.begin(), ranking.begin() + kHalfSize
        );
        std::vector<int> inferior(
            ranking.begin() + kHalfSize, ranking.end()
        );
        auto deterministic_shuffle = [&](std::vector<int>& rows,
                                         std::uint64_t phase) {
            std::vector<std::pair<double, int>> keyed;
            keyed.reserve(rows.size());
            for (std::size_t index = 0; index < rows.size(); ++index) {
                keyed.emplace_back(
                    rng.uniform(
                        result.generations,
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

        constexpr int kSuperiorFusionCount = 12;
        std::vector<int> fused_superior;
        std::vector<int> fused_inferior;
        fused_superior.reserve(kHalfSize);
        fused_inferior.reserve(kHalfSize);
        for (int row = 0; row < kHalfSize; ++row) {
            if (row < kSuperiorFusionCount) {
                fused_superior.push_back(
                    superior[static_cast<std::size_t>(row)]
                );
            } else {
                fused_inferior.push_back(
                    superior[static_cast<std::size_t>(row)]
                );
            }
            if (row < kHalfSize - kSuperiorFusionCount) {
                fused_superior.push_back(
                    inferior[static_cast<std::size_t>(row)]
                );
            } else {
                fused_inferior.push_back(
                    inferior[static_cast<std::size_t>(row)]
                );
            }
        }
        result.work.fusion_memberships +=
            static_cast<std::uint64_t>(
                fused_superior.size() + fused_inferior.size()
            );
        const double mu = static_cast<double>(kPaperImax)
            / static_cast<double>(
                kPaperImax + result.generations + 1
            );
        const double scale =
            0.5 * std::pow(2.0, std::exp(1.0 - mu));
        const double crossover_rate = std::clamp(
            0.1 * static_cast<double>(result.generations)
                / static_cast<double>(kPaperImax),
            0.0,
            0.1
        );
        constexpr int kPbestCount = 3;
        result.selection_other_stage.wall_seconds +=
            std::chrono::duration<double>(
                Clock::now() - selection_started
            ).count();

        Matrix trial(
            static_cast<std::size_t>(offspring_count * dimension), 0.0
        );
        const std::vector<char> blocked = blocked_mask(data);
        std::vector<std::uint64_t> repair_draws(
            static_cast<std::size_t>(offspring_count), 0
        );
        add_stage(
            result.fusion_variation_repair_stage,
            timed_granularity_aware(
                executor,
                0,
                offspring_count,
                offspring_count * dimension,
                [&](int row) {
                auto draw_distinct = [&](std::uint64_t phase,
                                         int first,
                                         int second) {
                    int candidate = row;
                    std::uint64_t draw = 0;
                    while (candidate == row
                           || candidate == first
                           || candidate == second) {
                        candidate = rng.integer(
                            0,
                            kHalfSize,
                            result.generations,
                            phase,
                            static_cast<std::uint64_t>(row),
                            0,
                            draw++
                        );
                    }
                    return candidate;
                };
                const int r1 = draw_distinct(903, -1, -1);
                const int r2 = draw_distinct(904, r1, -1);
                const int r3 = draw_distinct(905, r1, r2);
                const int pbest = ranking[static_cast<std::size_t>(
                    rng.integer(
                        0,
                        kPbestCount,
                        result.generations,
                        906,
                        static_cast<std::uint64_t>(row)
                    )
                )];
                const int jrand = rng.integer(
                    0,
                    dimension,
                    result.generations,
                    907,
                    static_cast<std::uint64_t>(row)
                );
                for (int column = 0; column < dimension; ++column) {
                    const double mutant =
                        population[at(
                            fused_superior[
                                static_cast<std::size_t>(r1)
                            ],
                            column,
                            dimension
                        )]
                        + scale * (
                            population[at(
                                fused_superior[
                                    static_cast<std::size_t>(r2)
                                ],
                                column,
                                dimension
                            )]
                            - population[at(
                                fused_inferior[
                                    static_cast<std::size_t>(r3)
                                ],
                                column,
                                dimension
                            )]
                        );
                    const bool use_mutant =
                        column == jrand
                        || rng.uniform(
                            result.generations,
                            908,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(column)
                        ) < crossover_rate;
                    trial[at(row, column, dimension)] = use_mutant
                        ? mutant
                        : population[at(pbest, column, dimension)];
                }
                std::vector<char> used = blocked;
                std::uint64_t draws = 0;
                for (int column = 0; column < dimension; ++column) {
                    int cell = static_cast<int>(
                        std::ceil(trial[at(row, column, dimension)])
                    );
                    if (cell < 1 || cell > data.rows * data.cols) {
                        cell = rng.integer(
                            1,
                            data.rows * data.cols + 1,
                            result.generations,
                            909,
                            static_cast<std::uint64_t>(row),
                            static_cast<std::uint64_t>(column)
                        );
                        ++draws;
                    }
                    if (blocked[static_cast<std::size_t>(cell - 1)] == 0) {
                        used[static_cast<std::size_t>(cell - 1)] = 1;
                    }
                }
                int placed = 0;
                for (int cell = 0; cell < data.rows * data.cols; ++cell) {
                    if (used[static_cast<std::size_t>(cell)] != 0
                        && blocked[static_cast<std::size_t>(cell)] == 0) {
                        ++placed;
                    }
                }
                std::uint64_t draw = 0;
                while (placed < dimension) {
                    const int cell = rng.integer(
                        1,
                        data.rows * data.cols + 1,
                        result.generations,
                        910,
                        static_cast<std::uint64_t>(row),
                        0,
                        draw++
                    );
                    const std::size_t index =
                        static_cast<std::size_t>(cell - 1);
                    if (used[index] == 0) {
                        used[index] = 1;
                        ++placed;
                    }
                }
                draws += draw;
                int output = 0;
                for (int cell = 0;
                     cell < data.rows * data.cols && output < dimension;
                     ++cell) {
                    if (used[static_cast<std::size_t>(cell)] != 0
                        && blocked[static_cast<std::size_t>(cell)] == 0) {
                        trial[at(row, output++, dimension)] =
                            static_cast<double>(cell + 1);
                    }
                }
                repair_draws[static_cast<std::size_t>(row)] = draws;
            })
        );
        result.work.mutation_vectors +=
            static_cast<std::uint64_t>(offspring_count);
        result.work.crossover_gene_trials +=
            static_cast<std::uint64_t>(offspring_count * dimension);
        result.work.forced_crossover_genes +=
            static_cast<std::uint64_t>(offspring_count);
        result.work.repair_random_draws += std::accumulate(
            repair_draws.begin(),
            repair_draws.end(),
            std::uint64_t{0}
        );

        std::vector<double> trial_fitness =
            evaluate(trial, offspring_count);
        const auto replacement_started = Clock::now();
        for (int row = 0; row < offspring_count; ++row) {
            const int target = ranking[static_cast<std::size_t>(row)];
            if (trial_fitness[static_cast<std::size_t>(row)]
                >= fitness[static_cast<std::size_t>(target)]) {
                copy_row(population, target, trial, row, dimension);
                fitness[static_cast<std::size_t>(target)] =
                    trial_fitness[static_cast<std::size_t>(row)];
                ++result.work.accepted_replacements;
            }
        }
        result.selection_other_stage.wall_seconds +=
            std::chrono::duration<double>(
                Clock::now() - replacement_started
            ).count();
    }

    result.physical_fes = fes;
    result.best_expected_power_kw = best;
    result.no_wake_expected_power_kw =
        no_wake_expected_power_kw(data);
    result.conversion_efficiency_percent =
        100.0 * result.best_expected_power_kw
        / result.no_wake_expected_power_kw;
    result.best_layout_1based = best_layout;
    result.best_layout_hash = layout_hash(best_layout);
    result.population_layout_hash =
        population_hash(population, dimension);
    result.total_wall_seconds =
        std::chrono::duration<double>(Clock::now() - total_started).count();
    return result;
}

std::string result_to_json(const Result& result) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"schema_version\":1"
           << ",\"algorithm_id\":\"bde\""
           << ",\"method_semantic_id\":\""
           << escape_json(result.method_semantic_id) << "\""
           << ",\"execution_profile_id\":\""
           << escape_json(result.execution_profile_id) << "\""
           << ",\"problem_semantic_id\":\""
           << escape_json(result.problem_semantic_id) << "\""
           << ",\"case_id\":\"" << escape_json(result.case_id) << "\""
           << ",\"objective_semantics_hash\":\""
           << result.objective_semantics_hash << "\""
           << ",\"feasible_set_hash\":\""
           << result.feasible_set_hash << "\""
           << ",\"seed\":" << result.seed
           << ",\"physical_fes\":" << result.physical_fes
           << ",\"generations\":" << result.generations
           << ",\"schedule_imax\":" << result.schedule_imax
           << ",\"population_size\":" << result.population_size
           << ",\"requested_workers\":" << result.requested_workers
           << ",\"resolved_workers\":" << result.resolved_workers
           << ",\"requested_execution_mode\":\""
           << escape_json(result.requested_execution_mode) << "\""
           << ",\"resolved_execution_mode\":\""
           << result.resolved_execution_mode << "\""
           << ",\"total_wall_seconds\":" << result.total_wall_seconds
           << ",\"stage_receipts\":{"
           << "\"initialization\":"
           << stage_json(result.initialization_stage)
           << ",\"fusion_variation_repair\":"
           << stage_json(result.fusion_variation_repair_stage)
           << ",\"evaluator\":" << stage_json(result.evaluator_stage)
           << ",\"selection_other\":"
           << stage_json(result.selection_other_stage) << "}"
           << ",\"work_receipt\":{"
           << "\"complete_layout_evaluations\":"
           << result.work.complete_layout_evaluations
           << ",\"ranked_individuals\":"
           << result.work.ranked_individuals
           << ",\"fusion_memberships\":"
           << result.work.fusion_memberships
           << ",\"mutation_vectors\":"
           << result.work.mutation_vectors
           << ",\"crossover_gene_trials\":"
           << result.work.crossover_gene_trials
           << ",\"forced_crossover_genes\":"
           << result.work.forced_crossover_genes
           << ",\"repair_random_draws\":"
           << result.work.repair_random_draws
           << ",\"accepted_replacements\":"
           << result.work.accepted_replacements << "}"
           << ",\"best_expected_power_kw\":"
           << result.best_expected_power_kw
           << ",\"no_wake_expected_power_kw\":"
           << result.no_wake_expected_power_kw
           << ",\"conversion_efficiency_percent\":"
           << result.conversion_efficiency_percent
           << ",\"best_layout_1based\":[";
    for (std::size_t index = 0;
         index < result.best_layout_1based.size();
         ++index) {
        if (index != 0) {
            output << ',';
        }
        output << result.best_layout_1based[index];
    }
    output << "]"
           << ",\"best_layout_hash\":\""
           << result.best_layout_hash << "\""
           << ",\"population_layout_hash\":\""
           << result.population_layout_hash << "\""
           << ",\"claim_boundary\":\"P3 declared WS5/WS6 proxy;"
           << " never pool or rank with WS1-WS4 source replay\"}";
    return output.str();
}

}  // namespace bde_ws56
