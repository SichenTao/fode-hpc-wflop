/*
WFLOP IMPLEMENTATION FACT DECLARATION

Implementation unit: deterministic CPU reconstruction of the ALGA paper formulas
Paper DOI: 10.1016/j.swevo.2025.102018
Paper provides: population 30, crossover 0.8, mutation 0.1, eight attention
heads, learning rate 1e-3, Eqs. (19)-(24), and high-level pseudocode.
Public author ALGA code/data URL: none found by the bounded search recorded in
docs/source-dossiers/T45.json.
Related public asset: WFLO-GGA at https://github.com/zbh0528/WFLO-GGA,
commit 6ce41326e6c1d3685a01e038baf6d1d07aa46126. It provides an AGA
predecessor implementation, Guishan planar wind/site data, and a one-point
mutation/repair convention; it does not provide ALGA source, 3D terrain, the
paper's four seasonal arrays, or trained attention state.
Missing paper fields: target, loss, optimizer, batch/epoch schedule, key width,
elite count, mask construction/cardinality, initialization, and repair.
Reconstruction action: use the frozen completions in
shared/contracts/alga_attention_declared_reconstruction_contract.json.
Method semantic ID: alga_attention_declared_reconstruction_v1.
Step 11 width-two probing is a sensitivity-only independent method semantic.
The width-one baseline remains unchanged; distinct method semantics are never
pooled or used for cross-semantic ranking.
Problem scope: primary P3 reconstruction combines a declared analytic
Guishan-family terrain and seasonal wind completion with the paper-visible
grid, MySE11-230 controls, and 3D Gaussian wake equations. The historical
planar and FODE-E0 profiles remain interoperability transfers. The P3 profile
is distinct from the unavailable original Guishan arrays.
Backend: persistent-team deterministic CPU parallel implementation. Its
scientific semantics are admitted; HPC throughput admission remains pending an
uncontended formal host. Hybrid/GPU names are fail-closed compatibility
interfaces and never silently fall back to CPU.
Claim boundary: M3 declared engineering reconstruction; never report it as the
original ALGA implementation or as reproduction of the paper's numerical results.
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/algorithms.hpp"

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
#include <string>
#include <utility>
#include <vector>

namespace wflop {
namespace {

using Clock = std::chrono::steady_clock;
using Matrix = std::vector<double>;

constexpr int kPopulationSize = 30;
constexpr int kAttentionHeads = 8;
constexpr int kEliteCount = 6;
constexpr double kLearningRate = 1.0e-3;
constexpr double kCrossoverRate = 0.8;
constexpr double kMutationRate = 0.1;
constexpr double kMaskFraction = 0.1;
constexpr const char* kGuishanTransferCaseHash =
    "fnv1a64:7b33990a4305cdf7";

std::size_t index_of(int row, int column, int width) {
    return static_cast<std::size_t>(row * width + column);
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

void copy_row(
    Matrix& target,
    int target_row,
    const Matrix& source,
    int source_row,
    int width
) {
    std::copy_n(
        source.begin() + static_cast<std::ptrdiff_t>(source_row * width),
        width,
        target.begin() + static_cast<std::ptrdiff_t>(target_row * width)
    );
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

std::string transfer_case_hash(const fode::CaseData& data) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix_byte = [&](std::uint8_t value) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    auto mix_u64 = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            mix_byte(static_cast<std::uint8_t>(value & 0xffULL));
            value >>= 8;
        }
    };
    auto mix_double = [&](double value) {
        mix_u64(std::bit_cast<std::uint64_t>(value));
    };
    for (const unsigned char byte : data.case_id) {
        mix_byte(byte);
    }
    mix_u64(static_cast<std::uint64_t>(data.rows));
    mix_u64(static_cast<std::uint64_t>(data.cols));
    mix_u64(static_cast<std::uint64_t>(data.turbine_count));
    mix_double(data.cell_width);
    mix_u64(static_cast<std::uint64_t>(data.theta.size()));
    for (const double value : data.theta) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(data.velocity.size()));
    for (const double value : data.velocity) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(data.probability.size()));
    for (const double value : data.probability) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(
        data.unavailable_cells_1based.size()
    ));
    for (const int value : data.unavailable_cells_1based) {
        mix_u64(static_cast<std::uint64_t>(value));
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

Matrix initialize_population(
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    fode::PersistentExecutor& executor
) {
    const int dimension = data.turbine_count;
    const auto available = available_cells(data);
    Matrix population(
        static_cast<std::size_t>(kPopulationSize * dimension),
        0.0
    );
    executor.parallel_for(0, kPopulationSize, [&](int individual) {
        std::vector<std::pair<double, int>> keyed;
        keyed.reserve(available.size());
        for (std::size_t cell = 0; cell < available.size(); ++cell) {
            keyed.emplace_back(
                rng.uniform(
                    0,
                    8100,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(cell)
                ),
                available[cell]
            );
        }
        std::stable_sort(keyed.begin(), keyed.end());
        std::vector<int> row;
        row.reserve(static_cast<std::size_t>(dimension));
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            row.push_back(
                keyed[static_cast<std::size_t>(coordinate)].second
            );
        }
        std::sort(row.begin(), row.end());
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            population[index_of(individual, coordinate, dimension)] =
                static_cast<double>(
                    row[static_cast<std::size_t>(coordinate)]
                );
        }
    });
    return population;
}

void repair_row(
    Matrix& population,
    int row,
    const fode::CaseData& data,
    const fode::CounterRng& rng,
    std::uint64_t generation
) {
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    const auto blocked = blocked_mask(data);
    std::vector<char> used = blocked;
    for (int coordinate = 0; coordinate < dimension; ++coordinate) {
        const int cell = static_cast<int>(std::llround(
            population[index_of(row, coordinate, dimension)]
        ));
        if (cell >= 1 && cell <= grid
            && blocked[static_cast<std::size_t>(cell - 1)] == 0) {
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
            8101,
            static_cast<std::uint64_t>(row),
            0,
            draw++
        );
        const std::size_t offset = static_cast<std::size_t>(cell - 1);
        if (used[offset] == 0) {
            used[offset] = 1;
            ++placed;
        }
    }
    int output = 0;
    for (int cell = 0; cell < grid && output < dimension; ++cell) {
        if (used[static_cast<std::size_t>(cell)] != 0
            && blocked[static_cast<std::size_t>(cell)] == 0) {
            population[index_of(row, output++, dimension)] =
                static_cast<double>(cell + 1);
        }
    }
}

struct AttentionModel {
    int dimension = 0;
    int hidden_width = 1;
    Matrix query;
    Matrix key;
    Matrix value;
    std::vector<double> output;
    double bias = 0.0;

    AttentionModel(
        int input_dimension,
        int attention_hidden_width,
        const fode::CounterRng& rng
    ) : dimension(input_dimension),
        hidden_width(attention_hidden_width),
        query(static_cast<std::size_t>(
            kAttentionHeads * input_dimension
        )),
        key(query.size()),
        value(query.size()),
        output(static_cast<std::size_t>(
            kAttentionHeads * attention_hidden_width
        )) {
        const double limit = std::sqrt(
            6.0 / static_cast<double>(input_dimension + 1)
        );
        for (int head = 0; head < kAttentionHeads; ++head) {
            for (int coordinate = 0;
                 coordinate < input_dimension;
                 ++coordinate) {
                const std::size_t offset =
                    index_of(head, coordinate, input_dimension);
                query[offset] = limit * (
                    2.0 * rng.uniform(
                        0, 8110,
                        static_cast<std::uint64_t>(head),
                        static_cast<std::uint64_t>(coordinate)
                    ) - 1.0
                );
                key[offset] = limit * (
                    2.0 * rng.uniform(
                        0, 8111,
                        static_cast<std::uint64_t>(head),
                        static_cast<std::uint64_t>(coordinate)
                    ) - 1.0
                );
                value[offset] = limit * (
                    2.0 * rng.uniform(
                        0, 8112,
                        static_cast<std::uint64_t>(head),
                        static_cast<std::uint64_t>(coordinate)
                    ) - 1.0
                );
            }
            for (int hidden = 0; hidden < hidden_width; ++hidden) {
                const std::size_t output_index =
                    index_of(head, hidden, hidden_width);
                output[output_index] = limit * (
                    2.0 * (
                        hidden == 0
                        ? rng.uniform(
                            0, 8113,
                            static_cast<std::uint64_t>(head)
                        )
                        : rng.uniform(
                            0, 8113,
                            static_cast<std::uint64_t>(head),
                            static_cast<std::uint64_t>(hidden)
                        )
                    ) - 1.0
                );
            }
        }
    }
};

struct AttentionForward {
    Matrix input;
    Matrix query;
    Matrix key;
    Matrix value;
    Matrix weights;
    Matrix attended;
    std::vector<double> prediction;
};

AttentionForward attention_forward(
    const AttentionModel& model,
    const Matrix& population,
    int population_size,
    int grid
) {
    const int dimension = model.dimension;
    AttentionForward state;
    state.input.resize(
        static_cast<std::size_t>(population_size * dimension)
    );
    state.query.resize(
        static_cast<std::size_t>(kAttentionHeads * population_size)
    );
    state.key.resize(state.query.size());
    state.value.resize(state.query.size());
    state.weights.resize(
        static_cast<std::size_t>(
            kAttentionHeads * population_size * population_size
        )
    );
    state.attended.resize(state.query.size());
    state.prediction.assign(
        static_cast<std::size_t>(population_size),
        model.bias
    );
    for (int row = 0; row < population_size; ++row) {
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            state.input[index_of(row, coordinate, dimension)] =
                population[index_of(row, coordinate, dimension)]
                    / static_cast<double>(grid)
                - 0.5;
        }
    }
    // Every declared head projects the D-dimensional layout to one scalar
    // query/key channel, so the paper's 1/sqrt(d_k) factor is exactly one.
    constexpr double scale = 1.0;
    for (int head = 0; head < kAttentionHeads; ++head) {
        for (int row = 0; row < population_size; ++row) {
            double query = 0.0;
            double key = 0.0;
            double value = 0.0;
            for (int coordinate = 0;
                 coordinate < dimension;
                 ++coordinate) {
                const double input =
                    state.input[index_of(row, coordinate, dimension)];
                const std::size_t weight =
                    index_of(head, coordinate, dimension);
                query += input * model.query[weight];
                key += input * model.key[weight];
                value += input * model.value[weight];
            }
            state.query[index_of(head, row, population_size)] = query;
            state.key[index_of(head, row, population_size)] = key;
            state.value[index_of(head, row, population_size)] = value;
        }
        for (int row = 0; row < population_size; ++row) {
            double maximum = -std::numeric_limits<double>::infinity();
            for (int column = 0; column < population_size; ++column) {
                maximum = std::max(
                    maximum,
                    state.query[index_of(head, row, population_size)]
                        * state.key[index_of(
                            head, column, population_size
                        )]
                        * scale
                );
            }
            double denominator = 0.0;
            for (int column = 0; column < population_size; ++column) {
                const double weight = std::exp(
                    state.query[index_of(head, row, population_size)]
                        * state.key[index_of(
                            head, column, population_size
                        )]
                        * scale
                    - maximum
                );
                const std::size_t offset = static_cast<std::size_t>(
                    (head * population_size + row) * population_size
                    + column
                );
                state.weights[offset] = weight;
                denominator += weight;
            }
            double attended = 0.0;
            for (int column = 0; column < population_size; ++column) {
                const std::size_t offset = static_cast<std::size_t>(
                    (head * population_size + row) * population_size
                    + column
                );
                state.weights[offset] /= denominator;
                attended += state.weights[offset]
                    * state.value[index_of(
                        head, column, population_size
                    )];
            }
            state.attended[index_of(head, row, population_size)] =
                attended;
            state.prediction[static_cast<std::size_t>(row)] +=
                model.output[index_of(head, 0, model.hidden_width)]
                * attended;
            if (model.hidden_width == 2) {
                state.prediction[static_cast<std::size_t>(row)] +=
                    model.output[index_of(head, 1, model.hidden_width)]
                    * std::max(0.0, attended);
            }
        }
    }
    return state;
}

std::vector<double> normalized_fitness_targets(
    const std::vector<double>& fitness
) {
    const auto [minimum, maximum] = std::minmax_element(
        fitness.begin(), fitness.end()
    );
    std::vector<double> target(fitness.size(), 1.0);
    if (*maximum > *minimum) {
        for (std::size_t row = 0; row < fitness.size(); ++row) {
            target[row] =
                (fitness[row] - *minimum) / (*maximum - *minimum);
        }
    }
    return target;
}

AttentionForward train_one_full_batch_step(
    AttentionModel& model,
    const Matrix& population,
    const std::vector<double>& fitness,
    int grid
) {
    const int size = static_cast<int>(fitness.size());
    const int dimension = model.dimension;
    AttentionForward state =
        attention_forward(model, population, size, grid);
    const auto target = normalized_fitness_targets(fitness);
    std::vector<double> prediction_gradient(
        static_cast<std::size_t>(size), 0.0
    );
    for (int row = 0; row < size; ++row) {
        prediction_gradient[static_cast<std::size_t>(row)] =
            2.0 * (
                state.prediction[static_cast<std::size_t>(row)]
                - target[static_cast<std::size_t>(row)]
            ) / static_cast<double>(size);
    }
    Matrix query_gradient(model.query.size(), 0.0);
    Matrix key_gradient(model.key.size(), 0.0);
    Matrix value_gradient(model.value.size(), 0.0);
    std::vector<double> output_gradient(
        model.output.size(), 0.0
    );
    // Match the scalar key width used by attention_forward(): d_k = 1.
    constexpr double scale = 1.0;
    for (int head = 0; head < kAttentionHeads; ++head) {
        std::vector<double> q_gradient(
            static_cast<std::size_t>(size), 0.0
        );
        std::vector<double> k_gradient(
            static_cast<std::size_t>(size), 0.0
        );
        std::vector<double> v_gradient(
            static_cast<std::size_t>(size), 0.0
        );
        for (int row = 0; row < size; ++row) {
            output_gradient[
                index_of(head, 0, model.hidden_width)
            ] +=
                prediction_gradient[static_cast<std::size_t>(row)]
                * state.attended[index_of(head, row, size)];
            double attended_gradient =
                prediction_gradient[static_cast<std::size_t>(row)]
                * model.output[index_of(head, 0, model.hidden_width)];
            if (model.hidden_width == 2) {
                const double attended =
                    state.attended[index_of(head, row, size)];
                output_gradient[
                    index_of(head, 1, model.hidden_width)
                ] += prediction_gradient[static_cast<std::size_t>(row)]
                    * std::max(0.0, attended);
                if (attended > 0.0) {
                    attended_gradient += prediction_gradient[
                        static_cast<std::size_t>(row)
                    ] * model.output[
                        index_of(head, 1, model.hidden_width)
                    ];
                }
            }
            double weighted_attention_gradient = 0.0;
            for (int column = 0; column < size; ++column) {
                const std::size_t offset = static_cast<std::size_t>(
                    (head * size + row) * size + column
                );
                const double attention_gradient =
                    attended_gradient
                    * state.value[index_of(head, column, size)];
                weighted_attention_gradient +=
                    state.weights[offset] * attention_gradient;
                v_gradient[static_cast<std::size_t>(column)] +=
                    attended_gradient * state.weights[offset];
            }
            for (int column = 0; column < size; ++column) {
                const std::size_t offset = static_cast<std::size_t>(
                    (head * size + row) * size + column
                );
                const double attention_gradient =
                    attended_gradient
                    * state.value[index_of(head, column, size)];
                const double score_gradient =
                    state.weights[offset]
                    * (attention_gradient - weighted_attention_gradient);
                q_gradient[static_cast<std::size_t>(row)] +=
                    score_gradient
                    * state.key[index_of(head, column, size)]
                    * scale;
                k_gradient[static_cast<std::size_t>(column)] +=
                    score_gradient
                    * state.query[index_of(head, row, size)]
                    * scale;
            }
        }
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            double query = 0.0;
            double key = 0.0;
            double value = 0.0;
            for (int row = 0; row < size; ++row) {
                const double input =
                    state.input[index_of(row, coordinate, dimension)];
                query += q_gradient[static_cast<std::size_t>(row)] * input;
                key += k_gradient[static_cast<std::size_t>(row)] * input;
                value += v_gradient[static_cast<std::size_t>(row)] * input;
            }
            const std::size_t offset =
                index_of(head, coordinate, dimension);
            query_gradient[offset] = query;
            key_gradient[offset] = key;
            value_gradient[offset] = value;
        }
    }
    const double bias_gradient = std::accumulate(
        prediction_gradient.begin(), prediction_gradient.end(), 0.0
    );
    for (std::size_t index = 0; index < model.query.size(); ++index) {
        model.query[index] -= kLearningRate * query_gradient[index];
        model.key[index] -= kLearningRate * key_gradient[index];
        model.value[index] -= kLearningRate * value_gradient[index];
    }
    for (std::size_t head = 0; head < model.output.size(); ++head) {
        model.output[head] -=
            kLearningRate * output_gradient[head];
    }
    model.bias -= kLearningRate * bias_gradient;
    return attention_forward(model, population, size, grid);
}

std::vector<double> cell_attention_scores(
    const AttentionForward& attention,
    const Matrix& population,
    const std::vector<double>& fitness,
    int dimension,
    int grid
) {
    const int size = static_cast<int>(fitness.size());
    const auto target = normalized_fitness_targets(fitness);
    std::vector<double> received(static_cast<std::size_t>(size), 0.0);
    for (int head = 0; head < kAttentionHeads; ++head) {
        for (int row = 0; row < size; ++row) {
            for (int column = 0; column < size; ++column) {
                received[static_cast<std::size_t>(column)] +=
                    attention.weights[static_cast<std::size_t>(
                        (head * size + row) * size + column
                    )];
            }
        }
    }
    const double denominator =
        static_cast<double>(kAttentionHeads * size);
    std::vector<double> score(static_cast<std::size_t>(grid), 0.0);
    std::vector<int> count(static_cast<std::size_t>(grid), 0);
    for (int row = 0; row < size; ++row) {
        const double weight = 0.5
            * target[static_cast<std::size_t>(row)]
            + 0.5 * received[static_cast<std::size_t>(row)]
                / denominator;
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            const int cell = static_cast<int>(std::llround(
                population[index_of(row, coordinate, dimension)]
            ));
            score[static_cast<std::size_t>(cell - 1)] += weight;
            ++count[static_cast<std::size_t>(cell - 1)];
        }
    }
    for (int cell = 0; cell < grid; ++cell) {
        if (count[static_cast<std::size_t>(cell)] > 0) {
            score[static_cast<std::size_t>(cell)] /=
                static_cast<double>(
                    count[static_cast<std::size_t>(cell)]
                );
        }
    }
    return score;
}

std::string model_hash(const AttentionModel& model) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](double value) {
        std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= bits & 0xffULL;
            hash *= 1099511628211ULL;
            bits >>= 8;
        }
    };
    for (const double value : model.query) {
        mix(value);
    }
    for (const double value : model.key) {
        mix(value);
    }
    for (const double value : model.value) {
        mix(value);
    }
    for (const double value : model.output) {
        mix(value);
    }
    mix(model.bias);
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

}  // namespace

RunResult optimize_alga_attention_declared_reconstruction(
    const fode::CaseData& data,
    const RunConfig& config
) {
    if (config.compute_backend != "cpu") {
        throw std::invalid_argument(
            "ALGA reconstruction backend '" + config.compute_backend
            + "' is interface-only in this build; no hidden CPU fallback"
        );
    }
    if (
        config.alga_attention_hidden_width != 1
        && config.alga_attention_hidden_width != 2
    ) {
        throw std::invalid_argument(
            "ALGA attention hidden width must be 1 or 2"
        );
    }
    if (config.problem_id == "alga_guishan_planar_transfer") {
        const std::string observed_hash = transfer_case_hash(data);
        if (observed_hash != kGuishanTransferCaseHash) {
            throw std::invalid_argument(
                "ALGA Guishan planar transfer manifest semantics do not "
                "match the frozen profile: observed " + observed_hash
            );
        }
    }
    if (config.problem_id == "alga_guishan_3d_declared_proxy_v1") {
        const bool valid_native_contract =
            data.case_id.rfind("ALGA_Guishan3D_", 0) == 0
            && data.rows == 12
            && data.cols == 12
            && data.cell_width == 500.0
            && (data.turbine_count == 20
                || data.turbine_count == 30
                || data.turbine_count == 40)
            && data.wake_model == "terrain_gaussian_rss"
            && data.power_curve_model == "cutin_shifted_cubic"
            && data.terrain_elevation_m.size() == 144
            && data.rotor_diameter == 135.0
            && data.hub_height == 100.0
            && data.power_curve_rated_kw == 3000.0;
        if (!valid_native_contract) {
            throw std::invalid_argument(
                "ALGA Guishan 3D manifest semantics do not match the "
                "declared native contract"
            );
        }
    }
    if (config.physical_fes_budget
        < static_cast<std::uint64_t>(kPopulationSize)) {
        throw std::invalid_argument(
            "ALGA reconstruction budget is below its 30-layout initialization"
        );
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    fode::CounterRng rng(
        config.seed ^ 0x414c47415f4d3355ULL
    );
    const int dimension = data.turbine_count;
    const int grid = data.rows * data.cols;
    Matrix population = initialize_population(data, rng, executor);
    AttentionModel model(
        dimension,
        config.alga_attention_hidden_width,
        rng
    );
    std::uint64_t physical_fes = 0;
    std::uint64_t generations = 0;
    double evaluator_seconds = 0.0;
    double best = -std::numeric_limits<double>::infinity();
    std::vector<int> best_layout;

    auto evaluate = [&](const Matrix& layouts, int count) {
        const auto evaluated = fode::evaluate_population_hpc(
            layouts,
            count,
            data,
            executor,
            fode::EvaluationDetail::TotalOnly
        );
        evaluator_seconds += evaluated.elapsed_seconds;
        for (int row = 0; row < count; ++row) {
            const double value =
                evaluated.fitness[static_cast<std::size_t>(row)];
            if (value > best) {
                best = value;
                best_layout.resize(static_cast<std::size_t>(dimension));
                for (int coordinate = 0;
                     coordinate < dimension;
                     ++coordinate) {
                    best_layout[static_cast<std::size_t>(coordinate)] =
                        static_cast<int>(std::llround(
                            layouts[index_of(
                                row, coordinate, dimension
                            )]
                        ));
                }
            }
        }
        physical_fes += static_cast<std::uint64_t>(count);
        return evaluated.fitness;
    };

    std::vector<double> fitness =
        evaluate(population, kPopulationSize);
    while (physical_fes < config.physical_fes_budget) {
        ++generations;
        const auto attention = train_one_full_batch_step(
            model, population, fitness, grid
        );
        const auto potential = cell_attention_scores(
            attention, population, fitness, dimension, grid
        );
        const auto ranking = stable_rank_descending(fitness);
        const int offspring_count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(
                    kPopulationSize - kEliteCount
                ),
                config.physical_fes_budget - physical_fes
            )
        );
        Matrix offspring(
            static_cast<std::size_t>(offspring_count * dimension),
            0.0
        );
        executor.parallel_for(0, offspring_count, [&](int child) {
            auto tournament = [&](std::uint64_t event) {
                const int first = rng.integer(
                    0, kPopulationSize,
                    generations, event,
                    static_cast<std::uint64_t>(child), 0
                );
                const int second = rng.integer(
                    0, kPopulationSize,
                    generations, event,
                    static_cast<std::uint64_t>(child), 1
                );
                return fitness[static_cast<std::size_t>(first)]
                    >= fitness[static_cast<std::size_t>(second)]
                    ? first : second;
            };
            const int first = tournament(8120);
            int second = tournament(8121);
            if (second == first) {
                second = (second + 1) % kPopulationSize;
            }
            copy_row(offspring, child, population, first, dimension);
            if (dimension > 1 && rng.uniform(
                    generations, 8122,
                    static_cast<std::uint64_t>(child)
                ) < kCrossoverRate) {
                const int cut = rng.integer(
                    1, dimension,
                    generations, 8123,
                    static_cast<std::uint64_t>(child)
                );
                for (int coordinate = cut;
                     coordinate < dimension;
                     ++coordinate) {
                    offspring[index_of(child, coordinate, dimension)] =
                        population[index_of(
                            second, coordinate, dimension
                        )];
                }
            }
            if (rng.uniform(
                    generations, 8124,
                    static_cast<std::uint64_t>(child)
                ) < kMutationRate) {
                const int coordinate = rng.integer(
                    0, dimension,
                    generations, 8125,
                    static_cast<std::uint64_t>(child)
                );
                offspring[index_of(child, coordinate, dimension)] =
                    static_cast<double>(rng.integer(
                        1, grid + 1,
                        generations, 8126,
                        static_cast<std::uint64_t>(child)
                    ));
            }
            repair_row(offspring, child, data, rng, generations);

            std::vector<char> occupied(
                static_cast<std::size_t>(grid), 0
            );
            std::vector<int> low_coordinates(
                static_cast<std::size_t>(dimension)
            );
            std::iota(
                low_coordinates.begin(), low_coordinates.end(), 0
            );
            for (int coordinate = 0;
                 coordinate < dimension;
                 ++coordinate) {
                const int cell = static_cast<int>(std::llround(
                    offspring[index_of(child, coordinate, dimension)]
                ));
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
            }
            std::stable_sort(
                low_coordinates.begin(),
                low_coordinates.end(),
                [&](int left, int right) {
                    const int left_cell =
                        static_cast<int>(std::llround(
                            offspring[index_of(
                                child, left, dimension
                            )]
                        ));
                    const int right_cell =
                        static_cast<int>(std::llround(
                            offspring[index_of(
                                child, right, dimension
                            )]
                        ));
                    if (potential[
                            static_cast<std::size_t>(left_cell - 1)
                        ] != potential[
                            static_cast<std::size_t>(right_cell - 1)
                        ]) {
                        return potential[
                            static_cast<std::size_t>(left_cell - 1)
                        ] < potential[
                            static_cast<std::size_t>(right_cell - 1)
                        ];
                    }
                    return left_cell < right_cell;
                }
            );
            std::vector<int> high_cells;
            high_cells.reserve(static_cast<std::size_t>(
                kEliteCount * dimension
            ));
            for (int elite = 0; elite < kEliteCount; ++elite) {
                const int source =
                    ranking[static_cast<std::size_t>(elite)];
                for (int coordinate = 0;
                     coordinate < dimension;
                     ++coordinate) {
                    high_cells.push_back(
                        static_cast<int>(std::llround(
                            population[index_of(
                                source, coordinate, dimension
                            )]
                        ))
                    );
                }
            }
            std::stable_sort(
                high_cells.begin(), high_cells.end(),
                [&](int left, int right) {
                    if (potential[static_cast<std::size_t>(left - 1)]
                        != potential[
                            static_cast<std::size_t>(right - 1)
                        ]) {
                        return potential[
                            static_cast<std::size_t>(left - 1)
                        ] > potential[
                            static_cast<std::size_t>(right - 1)
                        ];
                    }
                    return left < right;
                }
            );
            high_cells.erase(
                std::unique(high_cells.begin(), high_cells.end()),
                high_cells.end()
            );
            const int replacements = std::max(
                1,
                static_cast<int>(std::ceil(
                    kMaskFraction * static_cast<double>(dimension)
                ))
            );
            int replaced = 0;
            for (const int cell : high_cells) {
                if (replaced >= replacements) {
                    break;
                }
                if (occupied[static_cast<std::size_t>(cell - 1)] != 0) {
                    continue;
                }
                const int coordinate = low_coordinates[
                    static_cast<std::size_t>(replaced)
                ];
                const int removed = static_cast<int>(std::llround(
                    offspring[index_of(child, coordinate, dimension)]
                ));
                occupied[static_cast<std::size_t>(removed - 1)] = 0;
                occupied[static_cast<std::size_t>(cell - 1)] = 1;
                offspring[index_of(child, coordinate, dimension)] =
                    static_cast<double>(cell);
                ++replaced;
            }
            std::sort(
                offspring.begin()
                    + static_cast<std::ptrdiff_t>(child * dimension),
                offspring.begin()
                    + static_cast<std::ptrdiff_t>(
                        (child + 1) * dimension
                    )
            );
        });
        const auto offspring_fitness =
            evaluate(offspring, offspring_count);
        if (offspring_count < kPopulationSize - kEliteCount) {
            break;
        }
        Matrix next(population.size(), 0.0);
        std::vector<double> next_fitness(
            static_cast<std::size_t>(kPopulationSize), 0.0
        );
        for (int elite = 0; elite < kEliteCount; ++elite) {
            const int source = ranking[static_cast<std::size_t>(elite)];
            copy_row(next, elite, population, source, dimension);
            next_fitness[static_cast<std::size_t>(elite)] =
                fitness[static_cast<std::size_t>(source)];
        }
        const auto offspring_ranking =
            stable_rank_descending(offspring_fitness);
        for (int row = kEliteCount; row < kPopulationSize; ++row) {
            const int source = offspring_ranking[
                static_cast<std::size_t>(row - kEliteCount)
            ];
            copy_row(next, row, offspring, source, dimension);
            next_fitness[static_cast<std::size_t>(row)] =
                offspring_fitness[static_cast<std::size_t>(source)];
        }
        population = std::move(next);
        fitness = std::move(next_fitness);
    }

    RunResult result;
    result.algorithm_id = config.algorithm_id;
    result.method_id =
        config.alga_attention_hidden_width == 1
        ? "ALGA_ATTENTION_DECLARED_RECONSTRUCTION_V1"
        : "alga_attention_width2_sensitivity_v1";
    const auto& identity = algorithm_descriptor(config.algorithm_id);
    const auto& problem = problem_descriptor(config.problem_id);
    result.algorithm_provenance = identity.provenance;
    result.effective_semantics_id =
        config.alga_attention_hidden_width == 1
        ? identity.semantics_id
        : "alga_attention_width2_sensitivity_v1";
    result.problem_id = problem.id;
    result.problem_semantics_id = problem.semantics_id;
    result.case_id = data.case_id;
    result.seed = config.seed;
    result.physical_fes = physical_fes;
    result.inference_physical_fes = physical_fes;
    result.alga_attention_hidden_width =
        config.alga_attention_hidden_width;
    result.generations = generations;
    result.initial_population = kPopulationSize;
    result.final_population = kPopulationSize;
    result.requested_workers = config.workers;
    result.observed_workers = executor.thread_count();
    result.best_expected_power_kw = best;
    result.best_layout_1based = std::move(best_layout);
    result.total_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, result.total_seconds - evaluator_seconds);
    result.learned_state_hash = model_hash(model);
    return result;
}

}  // namespace wflop
