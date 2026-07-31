/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T24 Gaussian-wake/Markov evaluator, NSGA-III, and
pure-C++ CPU-HPC path
Paper/DOI: Optimization of a Wind Farm Layout to Mitigate the Wind Power
Intermittency; 10.1016/j.apenergy.2024.123383
Public source, missing assets, paper-internal data conflict, reconstruction
completion, semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/kim_t24.hpp
HPC design: immutable curves, rotor points, stationary probabilities, sparse
Markov weights, and reference lines are precomputed; wake geometry is sorted
once per direction and reused over nine speeds. Population initialization,
offspring construction, complete-layout evaluation, dominance rows, and
reference association use one persistent all-core worker team. Each layout
is serial internally to avoid nested oversubscription. Counter-keyed random
events and fixed reductions preserve one/all-core scientific trajectories.
Controlling contract: shared/contracts/core99_t24_kim_2024.json
Claim boundary: academic flexible declared reconstruction, not author code,
original Marado wind data, private arrays, random states, or numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/kim_t24.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t24 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kTurbines = 25;
constexpr int kSpeedCount = 9;
constexpr int kDirectionCount = 16;
constexpr int kRotorSamples = 8;
constexpr double kDiameterM = 112.0;
constexpr double kHubHeightM = 84.0;
constexpr double kSideM = 20.0 * kDiameterM;
constexpr double kMinimumSpacingM = 3.0 * kDiameterM;
constexpr double kRatedPowerMw = 3.0;
constexpr double kFarmRatedPowerMw =
    static_cast<double>(kTurbines) * kRatedPowerMw;
constexpr double kRoughnessM = 0.0001;
constexpr double kWakeGrowth = 0.0256;
constexpr double kMutationIndex = 20.0;

const std::array<double, kSpeedCount> kSpeeds = {
    2.0, 5.0, 7.0, 9.0, 11.0, 13.0, 15.0, 18.0, 23.0,
};
const std::array<double, kSpeedCount> kSpeedProbability = {
    0.07, 0.21, 0.19, 0.16, 0.13, 0.10, 0.07, 0.05, 0.02,
};
const std::array<double, kDirectionCount> kRealDirectionProbability = {
    0.025, 0.020, 0.020, 0.020,
    0.025, 0.030, 0.040, 0.060,
    0.180, 0.120, 0.080, 0.060,
    0.080, 0.160, 0.060, 0.040,
};
const std::array<double, 26> kPowerMw = {
    0.0, 0.0, 0.0, 0.0, 0.20, 0.38, 0.66, 1.02, 1.50,
    2.10, 2.58, 2.88, 2.98, 3.00, 3.00, 3.00, 3.00,
    3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00,
};
const std::array<double, 26> kThrust = {
    0.0, 0.0, 0.0, 0.0, 0.86, 0.83, 0.81, 0.82, 0.80,
    0.78, 0.70, 0.49, 0.31, 0.24, 0.18, 0.15, 0.13,
    0.115, 0.10, 0.087, 0.076, 0.066, 0.058, 0.051, 0.045,
    0.040,
};

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::array<std::pair<double, double>, kRotorSamples>
unit_rotor_samples() {
    std::array<std::pair<double, double>, kRotorSamples> result{};
    for (int index = 0; index < kRotorSamples; ++index) {
        const int ring = index / 4;
        const int angle_index = index % 4;
        const double radius =
            std::sqrt((static_cast<double>(ring) + 0.5) / 2.0);
        const double angle =
            (static_cast<double>(angle_index) + 0.5 * ring)
            * 0.5 * std::numbers::pi;
        result[static_cast<std::size_t>(index)] = {
            radius * std::cos(angle),
            radius * std::sin(angle),
        };
    }
    return result;
}

const auto kRotorUnitSamples = unit_rotor_samples();

template <std::size_t N>
double interpolate(
    const std::array<double, N>& table,
    const double speed
) {
    if (!(speed > 0.0)) {
        return table.front();
    }
    if (speed >= static_cast<double>(N - 1U)) {
        return table.back();
    }
    const auto lower = static_cast<std::size_t>(std::floor(speed));
    const double fraction = speed - static_cast<double>(lower);
    return table[lower]
        + fraction * (table[lower + 1U] - table[lower]);
}

std::vector<std::vector<double>> reversible_kernel(
    const std::vector<double>& target,
    const std::vector<std::vector<double>>& proposal
) {
    const int count = static_cast<int>(target.size());
    std::vector<std::vector<double>> result(
        static_cast<std::size_t>(count),
        std::vector<double>(static_cast<std::size_t>(count), 0.0)
    );
    for (int from = 0; from < count; ++from) {
        double outgoing = 0.0;
        for (int to = 0; to < count; ++to) {
            if (from == to) {
                continue;
            }
            const double q_forward =
                proposal[static_cast<std::size_t>(from)]
                    [static_cast<std::size_t>(to)];
            const double q_reverse =
                proposal[static_cast<std::size_t>(to)]
                    [static_cast<std::size_t>(from)];
            if (!(q_forward > 0.0) || !(q_reverse > 0.0)) {
                continue;
            }
            const double ratio =
                target[static_cast<std::size_t>(to)] * q_reverse
                / (
                    target[static_cast<std::size_t>(from)] * q_forward
                );
            const double accepted = q_forward * std::min(1.0, ratio);
            result[static_cast<std::size_t>(from)]
                [static_cast<std::size_t>(to)] = accepted;
            outgoing += accepted;
        }
        result[static_cast<std::size_t>(from)]
            [static_cast<std::size_t>(from)] =
            std::max(0.0, 1.0 - outgoing);
    }
    return result;
}

std::vector<std::vector<double>> speed_kernel() {
    std::vector<std::vector<double>> proposal(
        kSpeedCount,
        std::vector<double>(kSpeedCount, 0.0)
    );
    for (int from = 0; from < kSpeedCount; ++from) {
        double sum = 0.0;
        for (int to = 0; to < kSpeedCount; ++to) {
            const int difference = std::abs(to - from);
            if (difference > 2) {
                continue;
            }
            const double value = std::exp(
                -0.5 * static_cast<double>(difference * difference)
            );
            proposal[static_cast<std::size_t>(from)]
                [static_cast<std::size_t>(to)] = value;
            sum += value;
        }
        for (double& value : proposal[static_cast<std::size_t>(from)]) {
            value /= sum;
        }
    }
    return reversible_kernel(
        std::vector<double>(
            kSpeedProbability.begin(), kSpeedProbability.end()
        ),
        proposal
    );
}

std::vector<std::vector<double>> direction_kernel(
    const std::array<double, kDirectionCount>& target
) {
    std::vector<std::vector<double>> proposal(
        kDirectionCount,
        std::vector<double>(kDirectionCount, 0.0)
    );
    for (int from = 0; from < kDirectionCount; ++from) {
        double sum = 0.0;
        for (int offset = -3; offset <= 3; ++offset) {
            const int to =
                (from + offset + kDirectionCount) % kDirectionCount;
            const double angle =
                22.5 * static_cast<double>(offset) / 24.8;
            const double value = std::exp(-0.5 * angle * angle);
            proposal[static_cast<std::size_t>(from)]
                [static_cast<std::size_t>(to)] += value;
            sum += value;
        }
        for (double& value : proposal[static_cast<std::size_t>(from)]) {
            value /= sum;
        }
    }
    return reversible_kernel(
        std::vector<double>(target.begin(), target.end()),
        proposal
    );
}

bool dominates(const Evaluation& left, const Evaluation& right) {
    if (left.feasible != right.feasible) {
        return left.feasible;
    }
    if (!left.feasible) {
        const double left_violation =
            left.spacing_violation_m + left.boundary_violation_m;
        const double right_violation =
            right.spacing_violation_m + right.boundary_violation_m;
        return left_violation < right_violation;
    }
    const bool no_worse =
        left.mean_power_mw >= right.mean_power_mw
        && left.intermittency_mw <= right.intermittency_mw;
    const bool strict =
        left.mean_power_mw > right.mean_power_mw
        || left.intermittency_mw < right.intermittency_mw;
    return no_worse && strict;
}

struct Individual {
    std::vector<Turbine> layout;
    Evaluation evaluation;
    int rank = 0;
    int reference = 0;
    double reference_distance = 0.0;
};

std::vector<std::vector<int>> assign_rank(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    const int count = static_cast<int>(population.size());
    std::vector<std::vector<int>> outgoing(
        static_cast<std::size_t>(count)
    );
    std::vector<int> incoming(static_cast<std::size_t>(count), 0);
    executor.parallel_for(0, count, [&](const int left) {
        int degree = 0;
        auto& row = outgoing[static_cast<std::size_t>(left)];
        for (int right = 0; right < count; ++right) {
            if (left == right) {
                continue;
            }
            if (
                dominates(
                    population[static_cast<std::size_t>(left)].evaluation,
                    population[static_cast<std::size_t>(right)].evaluation
                )
            ) {
                row.push_back(right);
            } else if (
                dominates(
                    population[static_cast<std::size_t>(right)].evaluation,
                    population[static_cast<std::size_t>(left)].evaluation
                )
            ) {
                ++degree;
            }
        }
        incoming[static_cast<std::size_t>(left)] = degree;
    });
    std::vector<std::vector<int>> fronts(1);
    for (int index = 0; index < count; ++index) {
        if (incoming[static_cast<std::size_t>(index)] == 0) {
            population[static_cast<std::size_t>(index)].rank = 1;
            fronts.front().push_back(index);
        }
    }
    std::size_t current = 0;
    while (current < fronts.size() && !fronts[current].empty()) {
        std::vector<int> next;
        for (const int source : fronts[current]) {
            for (const int target :
                 outgoing[static_cast<std::size_t>(source)]) {
                int& degree = incoming[static_cast<std::size_t>(target)];
                --degree;
                if (degree == 0) {
                    population[static_cast<std::size_t>(target)].rank =
                        static_cast<int>(current) + 2;
                    next.push_back(target);
                }
            }
        }
        if (!next.empty()) {
            std::sort(next.begin(), next.end());
            fronts.push_back(std::move(next));
        }
        ++current;
    }
    return fronts;
}

void associate(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    double minimum_power_cost = std::numeric_limits<double>::infinity();
    double maximum_power_cost = -std::numeric_limits<double>::infinity();
    double minimum_intermittency = std::numeric_limits<double>::infinity();
    double maximum_intermittency =
        -std::numeric_limits<double>::infinity();
    for (const auto& item : population) {
        const double power_cost =
            1.0 / std::max(1.0e-12, item.evaluation.mean_power_mw);
        minimum_power_cost = std::min(minimum_power_cost, power_cost);
        maximum_power_cost = std::max(maximum_power_cost, power_cost);
        minimum_intermittency = std::min(
            minimum_intermittency, item.evaluation.intermittency_mw
        );
        maximum_intermittency = std::max(
            maximum_intermittency, item.evaluation.intermittency_mw
        );
    }
    const double power_range = std::max(
        1.0e-15, maximum_power_cost - minimum_power_cost
    );
    const double intermittency_range = std::max(
        1.0e-15, maximum_intermittency - minimum_intermittency
    );
    executor.parallel_for(
        0,
        static_cast<int>(population.size()),
        [&](const int index) {
            auto& item = population[static_cast<std::size_t>(index)];
            const double x =
                (
                    1.0
                        / std::max(
                            1.0e-12, item.evaluation.mean_power_mw
                        )
                    - minimum_power_cost
                ) / power_range;
            const double y =
                (
                    item.evaluation.intermittency_mw
                    - minimum_intermittency
                ) / intermittency_range;
            int best_reference = 0;
            double best_distance =
                std::numeric_limits<double>::infinity();
            for (int reference = 0; reference < 92; ++reference) {
                const double wx =
                    static_cast<double>(reference) / 91.0;
                const double wy = 1.0 - wx;
                const double norm = std::hypot(wx, wy);
                const double projection = (x * wx + y * wy) / norm;
                const double distance = std::hypot(
                    x - projection * wx / norm,
                    y - projection * wy / norm
                );
                if (
                    distance < best_distance
                    || (
                        distance == best_distance
                        && reference < best_reference
                    )
                ) {
                    best_reference = reference;
                    best_distance = distance;
                }
            }
            item.reference = best_reference;
            item.reference_distance = best_distance;
        }
    );
}

int tournament(
    const std::vector<Individual>& population,
    const fode::CounterRng& rng,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    const int count = static_cast<int>(population.size());
    const int left = rng.integer(
        0, count, generation, 20, individual, draw, 0
    );
    const int right = rng.integer(
        0, count, generation, 20, individual, draw, 1
    );
    const auto& a = population[static_cast<std::size_t>(left)];
    const auto& b = population[static_cast<std::size_t>(right)];
    if (a.rank != b.rank) {
        return a.rank < b.rank ? left : right;
    }
    if (a.reference_distance != b.reference_distance) {
        return a.reference_distance < b.reference_distance ? left : right;
    }
    return std::min(left, right);
}

double polynomial_delta(const double random) {
    return random < 0.5
        ? std::pow(2.0 * random, 1.0 / (kMutationIndex + 1.0)) - 1.0
        : 1.0
            - std::pow(
                2.0 * (1.0 - random),
                1.0 / (kMutationIndex + 1.0)
            );
}

std::uint64_t hash_mix(std::uint64_t state, const std::uint64_t value) {
    state ^= value + 0x9e3779b97f4a7c15ULL
        + (state << 6U) + (state >> 2U);
    return state;
}

std::uint64_t scientific_hash(
    const std::vector<Individual>& population
) {
    std::uint64_t state = 0x24aee2024123383ULL;
    for (const auto& item : population) {
        state = hash_mix(
            state,
            std::bit_cast<std::uint64_t>(
                item.evaluation.mean_power_mw
            )
        );
        state = hash_mix(
            state,
            std::bit_cast<std::uint64_t>(
                item.evaluation.intermittency_mw
            )
        );
        for (const auto& turbine : item.layout) {
            state = hash_mix(
                state, std::bit_cast<std::uint64_t>(turbine.x_m)
            );
            state = hash_mix(
                state, std::bit_cast<std::uint64_t>(turbine.y_m)
            );
        }
    }
    return state;
}

}  // namespace

Problem::Problem(const CaseId id) : case_id_(id) {
    switch (id) {
    case CaseId::uniform_p0:
        id_ = "t24_uniform_threshold_000_n25";
        break;
    case CaseId::uniform_p007:
        id_ = "t24_uniform_threshold_007_n25";
        threshold_fraction_ = 0.07;
        break;
    case CaseId::uniform_p015:
        id_ = "t24_uniform_threshold_015_n25";
        threshold_fraction_ = 0.15;
        break;
    case CaseId::real_p0:
        id_ = "t24_real_threshold_000_n25";
        real_wind_ = true;
        break;
    case CaseId::real_p007:
        id_ = "t24_real_threshold_007_n25";
        real_wind_ = true;
        threshold_fraction_ = 0.07;
        break;
    case CaseId::real_p015:
        id_ = "t24_real_threshold_015_n25";
        real_wind_ = true;
        threshold_fraction_ = 0.15;
        break;
    }
    build_wind_contract();
}

const std::string& Problem::id() const noexcept {
    return id_;
}

CaseId Problem::case_id() const noexcept {
    return case_id_;
}

int Problem::turbine_count() const noexcept {
    return kTurbines;
}

int Problem::wind_state_count() const noexcept {
    return static_cast<int>(winds_.size());
}

int Problem::paper_population() const noexcept {
    return 92;
}

int Problem::paper_reference_intervals() const noexcept {
    return 91;
}

int Problem::paper_minimum_generations() const noexcept {
    return 1000;
}

int Problem::declared_maximum_generations() const noexcept {
    return 2000;
}

int Problem::declared_repeats() const noexcept {
    return 25;
}

double Problem::threshold_fraction() const noexcept {
    return threshold_fraction_;
}

double Problem::side_length_m() const noexcept {
    return kSideM;
}

double Problem::rotor_diameter_m() const noexcept {
    return kDiameterM;
}

bool Problem::real_wind() const noexcept {
    return real_wind_;
}

void Problem::build_wind_contract() {
    const std::array<double, kDirectionCount> direction_probability =
        real_wind_
        ? kRealDirectionProbability
        : std::array<double, kDirectionCount>{
            0.0625, 0.0625, 0.0625, 0.0625,
            0.0625, 0.0625, 0.0625, 0.0625,
            0.0625, 0.0625, 0.0625, 0.0625,
            0.0625, 0.0625, 0.0625, 0.0625,
        };
    winds_.clear();
    winds_.reserve(kDirectionCount * kSpeedCount);
    for (int direction = 0; direction < kDirectionCount; ++direction) {
        for (int speed = 0; speed < kSpeedCount; ++speed) {
            winds_.push_back({
                22.5 * static_cast<double>(direction),
                kSpeeds[static_cast<std::size_t>(speed)],
                direction_probability[static_cast<std::size_t>(direction)]
                    * kSpeedProbability[static_cast<std::size_t>(speed)],
            });
        }
    }
    const auto direction_transition =
        direction_kernel(direction_probability);
    const auto speed_transition = speed_kernel();
    transitions_.clear();
    for (int from_direction = 0;
         from_direction < kDirectionCount;
         ++from_direction) {
        for (int from_speed = 0; from_speed < kSpeedCount; ++from_speed) {
            const int from = from_direction * kSpeedCount + from_speed;
            for (int to_direction = 0;
                 to_direction < kDirectionCount;
                 ++to_direction) {
                const double direction_weight =
                    direction_transition[
                        static_cast<std::size_t>(from_direction)
                    ][static_cast<std::size_t>(to_direction)];
                if (!(direction_weight > 0.0)) {
                    continue;
                }
                for (int to_speed = 0;
                     to_speed < kSpeedCount;
                     ++to_speed) {
                    const double speed_weight =
                        speed_transition[
                            static_cast<std::size_t>(from_speed)
                        ][static_cast<std::size_t>(to_speed)];
                    const double conditional =
                        direction_weight * speed_weight;
                    if (!(conditional > 1.0e-18)) {
                        continue;
                    }
                    const int to =
                        to_direction * kSpeedCount + to_speed;
                    transitions_.push_back({
                        from,
                        to,
                        winds_[static_cast<std::size_t>(from)].probability
                            * conditional,
                    });
                }
            }
        }
    }
}

double Problem::power_mw(const double speed_mps) const {
    if (speed_mps < 4.0 || speed_mps > 25.0) {
        return 0.0;
    }
    return std::clamp(
        interpolate(kPowerMw, speed_mps), 0.0, kRatedPowerMw
    );
}

double Problem::thrust(const double speed_mps) const {
    if (speed_mps < 4.0 || speed_mps > 25.0) {
        return 0.0;
    }
    return std::clamp(interpolate(kThrust, speed_mps), 0.0, 0.95);
}

std::vector<double> Problem::state_powers(
    const std::vector<Turbine>& layout
) const {
    if (layout.size() != static_cast<std::size_t>(kTurbines)) {
        throw std::invalid_argument("T24 layout cardinality mismatch");
    }
    std::vector<double> state_power(
        static_cast<std::size_t>(kDirectionCount * kSpeedCount), 0.0
    );
    std::array<double, kTurbines> along{};
    std::array<double, kTurbines> across{};
    std::array<double, kTurbines> inflow{};
    std::array<int, kTurbines> order{};
    std::iota(order.begin(), order.end(), 0);

    for (int direction = 0; direction < kDirectionCount; ++direction) {
        const double angle =
            22.5 * static_cast<double>(direction)
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            const auto& item = layout[static_cast<std::size_t>(turbine)];
            along[static_cast<std::size_t>(turbine)] =
                cosine * item.x_m + sine * item.y_m;
            across[static_cast<std::size_t>(turbine)] =
                -sine * item.x_m + cosine * item.y_m;
        }
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int left, const int right) {
                const double a = along[static_cast<std::size_t>(left)];
                const double b = along[static_cast<std::size_t>(right)];
                return a != b ? a < b : left < right;
            }
        );

        for (int speed_index = 0;
             speed_index < kSpeedCount;
             ++speed_index) {
            const double reference_speed =
                kSpeeds[static_cast<std::size_t>(speed_index)];
            double farm_power = 0.0;
            for (int downstream_position = 0;
                 downstream_position < kTurbines;
                 ++downstream_position) {
                const int downstream =
                    order[static_cast<std::size_t>(downstream_position)];
                double rotor_sum = 0.0;
                for (const auto& sample : kRotorUnitSamples) {
                    const double sample_cross =
                        across[static_cast<std::size_t>(downstream)]
                        + 0.5 * kDiameterM * sample.first;
                    const double sample_height =
                        kHubHeightM + 0.5 * kDiameterM * sample.second;
                    const double free_speed =
                        reference_speed
                        * std::log(
                            (std::max(0.0, sample_height) + kRoughnessM)
                            / kRoughnessM
                        )
                        / std::log(
                            (kHubHeightM + kRoughnessM) / kRoughnessM
                        );
                    double deficit_speed = 0.0;
                    for (int upstream_position = 0;
                         upstream_position < downstream_position;
                         ++upstream_position) {
                        const int upstream =
                            order[
                                static_cast<std::size_t>(upstream_position)
                            ];
                        const double distance =
                            along[static_cast<std::size_t>(downstream)]
                            - along[static_cast<std::size_t>(upstream)];
                        if (!(distance > 0.0)) {
                            continue;
                        }
                        const double ct =
                            thrust(inflow[static_cast<std::size_t>(upstream)]);
                        if (!(ct > 0.0)) {
                            continue;
                        }
                        const double root = std::sqrt(
                            std::max(1.0e-12, 1.0 - ct)
                        );
                        const double beta =
                            (1.0 + root) / (2.0 * root);
                        const double width =
                            kWakeGrowth * distance / kDiameterM
                            + 0.2 * std::sqrt(beta);
                        const double radical = std::clamp(
                            1.0 - ct / (8.0 * width * width),
                            0.0,
                            1.0
                        );
                        const double cross =
                            (
                                sample_cross
                                - across[
                                    static_cast<std::size_t>(upstream)
                                ]
                            ) / kDiameterM;
                        const double vertical =
                            (sample_height - kHubHeightM) / kDiameterM;
                        const double fraction =
                            (1.0 - std::sqrt(radical))
                            * std::exp(
                                -(cross * cross + vertical * vertical)
                                / (2.0 * width * width)
                            );
                        deficit_speed +=
                            inflow[static_cast<std::size_t>(upstream)]
                            * fraction;
                    }
                    rotor_sum += std::max(0.0, free_speed - deficit_speed);
                }
                inflow[static_cast<std::size_t>(downstream)] =
                    rotor_sum / static_cast<double>(kRotorSamples);
                farm_power += power_mw(
                    inflow[static_cast<std::size_t>(downstream)]
                );
            }
            const int state = direction * kSpeedCount + speed_index;
            state_power[static_cast<std::size_t>(state)] = farm_power;
        }
    }
    return state_power;
}

Evaluation Problem::evaluate(
    const std::vector<Turbine>& layout
) const {
    if (layout.size() != static_cast<std::size_t>(kTurbines)) {
        throw std::invalid_argument("T24 layout cardinality mismatch");
    }
    Evaluation result;
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        const auto& item = layout[turbine];
        result.boundary_violation_m +=
            std::max(0.0, -item.x_m)
            + std::max(0.0, item.x_m - kSideM)
            + std::max(0.0, -item.y_m)
            + std::max(0.0, item.y_m - kSideM);
        for (std::size_t other = 0; other < turbine; ++other) {
            const double distance = std::hypot(
                item.x_m - layout[other].x_m,
                item.y_m - layout[other].y_m
            );
            result.spacing_violation_m +=
                std::max(0.0, kMinimumSpacingM - distance);
        }
    }
    result.feasible =
        result.spacing_violation_m <= 1.0e-9
        && result.boundary_violation_m <= 1.0e-9;
    const std::vector<double> powers = state_powers(layout);
    for (std::size_t state = 0; state < winds_.size(); ++state) {
        result.mean_power_mw +=
            winds_[state].probability * powers[state];
    }
    const double threshold = threshold_fraction_ * kFarmRatedPowerMw;
    for (const auto& transition : transitions_) {
        result.intermittency_mw +=
            transition.joint_probability
            * std::max(
                0.0,
                std::abs(
                    powers[static_cast<std::size_t>(transition.from)]
                    - powers[static_cast<std::size_t>(transition.to)]
                ) - threshold
            );
    }
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<Turbine>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(layouts.size());
    executor.parallel_for(
        0,
        static_cast<int>(layouts.size()),
        [&](const int index) {
            result[static_cast<std::size_t>(index)] =
                evaluate(layouts[static_cast<std::size_t>(index)]);
        }
    );
    return result;
}

std::vector<Turbine> Problem::reference_layout() const {
    std::vector<Turbine> result;
    result.reserve(kTurbines);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            result.push_back({
                5.0 * kDiameterM * static_cast<double>(column),
                5.0 * kDiameterM * static_cast<double>(row),
            });
        }
    }
    return result;
}

void Problem::repair(
    std::vector<Turbine>& layout,
    const std::uint64_t seed,
    const std::uint64_t generation,
    const std::uint64_t individual
) const {
    if (layout.size() != static_cast<std::size_t>(kTurbines)) {
        throw std::invalid_argument("T24 repair cardinality mismatch");
    }
    const fode::CounterRng rng(seed);
    for (auto& item : layout) {
        item.x_m = std::clamp(item.x_m, 0.0, kSideM);
        item.y_m = std::clamp(item.y_m, 0.0, kSideM);
    }
    for (std::size_t current = 0; current < layout.size(); ++current) {
        auto separated = [&]() {
            for (std::size_t other = 0; other < current; ++other) {
                if (
                    std::hypot(
                        layout[current].x_m - layout[other].x_m,
                        layout[current].y_m - layout[other].y_m
                    ) <= kMinimumSpacingM
                ) {
                    return false;
                }
            }
            return true;
        };
        if (separated()) {
            continue;
        }
        bool placed = false;
        for (std::uint64_t attempt = 0; attempt < 1024U; ++attempt) {
            layout[current].x_m =
                kSideM
                * rng.uniform(
                    generation,
                    30,
                    individual,
                    current,
                    2U * attempt
                );
            layout[current].y_m =
                kSideM
                * rng.uniform(
                    generation,
                    30,
                    individual,
                    current,
                    2U * attempt + 1U
                );
            if (separated()) {
                placed = true;
                break;
            }
        }
        if (!placed) {
            layout = reference_layout();
            return;
        }
    }
}

double Problem::model_problem_power_mw(
    const double upstream_y_over_d,
    const double speed_mps,
    const double direction_deg
) const {
    std::vector<Turbine> layout = reference_layout();
    layout.resize(3);
    layout[0] = {0.0, upstream_y_over_d * kDiameterM};
    layout[1] = {5.0 * kDiameterM, 5.0 * kDiameterM};
    layout[2] = {10.0 * kDiameterM, 5.0 * kDiameterM};

    const double angle = direction_deg * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    std::array<double, 3> along{};
    std::array<double, 3> across{};
    std::array<double, 3> inflow{};
    std::array<int, 3> order = {0, 1, 2};
    for (int index = 0; index < 3; ++index) {
        along[static_cast<std::size_t>(index)] =
            cosine * layout[static_cast<std::size_t>(index)].x_m
            + sine * layout[static_cast<std::size_t>(index)].y_m;
        across[static_cast<std::size_t>(index)] =
            -sine * layout[static_cast<std::size_t>(index)].x_m
            + cosine * layout[static_cast<std::size_t>(index)].y_m;
    }
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](const int left, const int right) {
            const double a = along[static_cast<std::size_t>(left)];
            const double b = along[static_cast<std::size_t>(right)];
            return a != b ? a < b : left < right;
        }
    );
    double farm_power = 0.0;
    for (int position = 0; position < 3; ++position) {
        const int downstream = order[static_cast<std::size_t>(position)];
        double rotor_sum = 0.0;
        for (const auto& sample : kRotorUnitSamples) {
            const double sample_cross =
                across[static_cast<std::size_t>(downstream)]
                + 0.5 * kDiameterM * sample.first;
            const double sample_height =
                kHubHeightM + 0.5 * kDiameterM * sample.second;
            const double free_speed =
                speed_mps
                * std::log(
                    (sample_height + kRoughnessM) / kRoughnessM
                )
                / std::log(
                    (kHubHeightM + kRoughnessM) / kRoughnessM
                );
            double deficit = 0.0;
            for (int prior = 0; prior < position; ++prior) {
                const int upstream =
                    order[static_cast<std::size_t>(prior)];
                const double distance =
                    along[static_cast<std::size_t>(downstream)]
                    - along[static_cast<std::size_t>(upstream)];
                const double ct =
                    thrust(inflow[static_cast<std::size_t>(upstream)]);
                if (!(distance > 0.0) || !(ct > 0.0)) {
                    continue;
                }
                const double root =
                    std::sqrt(std::max(1.0e-12, 1.0 - ct));
                const double beta = (1.0 + root) / (2.0 * root);
                const double width =
                    kWakeGrowth * distance / kDiameterM
                    + 0.2 * std::sqrt(beta);
                const double radical = std::clamp(
                    1.0 - ct / (8.0 * width * width), 0.0, 1.0
                );
                const double cross =
                    (
                        sample_cross
                        - across[static_cast<std::size_t>(upstream)]
                    ) / kDiameterM;
                const double vertical =
                    (sample_height - kHubHeightM) / kDiameterM;
                deficit +=
                    inflow[static_cast<std::size_t>(upstream)]
                    * (1.0 - std::sqrt(radical))
                    * std::exp(
                        -(cross * cross + vertical * vertical)
                        / (2.0 * width * width)
                    );
            }
            rotor_sum += std::max(0.0, free_speed - deficit);
        }
        inflow[static_cast<std::size_t>(downstream)] =
            rotor_sum / static_cast<double>(kRotorSamples);
        farm_power += power_mw(
            inflow[static_cast<std::size_t>(downstream)]
        );
    }
    return farm_power;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers <= 0
        || config.population < 4
        || config.generations < 0
        || config.generations > problem.declared_maximum_generations()
    ) {
        throw std::invalid_argument("invalid T24 run configuration");
    }
    const auto total_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const std::vector<Turbine> reference = problem.reference_layout();
    std::vector<Individual> population(
        static_cast<std::size_t>(config.population)
    );
    executor.parallel_for(0, config.population, [&](const int index) {
        auto layout = reference;
        if (index != 0) {
            for (std::size_t turbine = 0;
                 turbine < layout.size();
                 ++turbine) {
                layout[turbine].x_m =
                    problem.side_length_m()
                    * rng.uniform(
                        0,
                        1,
                        static_cast<std::uint64_t>(index),
                        turbine,
                        0
                    );
                layout[turbine].y_m =
                    problem.side_length_m()
                    * rng.uniform(
                        0,
                        1,
                        static_cast<std::uint64_t>(index),
                        turbine,
                        1
                    );
            }
            problem.repair(
                layout,
                config.seed,
                0,
                static_cast<std::uint64_t>(index)
            );
        }
        population[static_cast<std::size_t>(index)].layout =
            std::move(layout);
    });
    double evaluator_seconds = 0.0;
    auto evaluate_individuals = [&](std::vector<Individual>& individuals) {
        std::vector<std::vector<Turbine>> layouts;
        layouts.reserve(individuals.size());
        for (const auto& item : individuals) {
            layouts.push_back(item.layout);
        }
        const auto started = Clock::now();
        const auto evaluations =
            problem.evaluate_population(layouts, executor);
        evaluator_seconds += elapsed_seconds(started);
        for (std::size_t index = 0; index < individuals.size(); ++index) {
            individuals[index].evaluation = evaluations[index];
        }
    };
    evaluate_individuals(population);
    assign_rank(population, executor);
    associate(population, executor);
    double algorithm_seconds =
        elapsed_seconds(total_started) - evaluator_seconds;

    for (int generation = 0; generation < config.generations;
         ++generation) {
        const auto generation_key =
            static_cast<std::uint64_t>(generation + 1);
        auto algorithm_started = Clock::now();
        std::vector<Individual> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](const int index) {
            const auto individual_key =
                static_cast<std::uint64_t>(index);
            const int first = tournament(
                population, rng, generation_key, individual_key, 0
            );
            const int second = tournament(
                population, rng, generation_key, individual_key, 1
            );
            auto child =
                population[static_cast<std::size_t>(first)].layout;
            const auto& mate =
                population[static_cast<std::size_t>(second)].layout;
            for (std::size_t turbine = 0;
                 turbine < child.size();
                 ++turbine) {
                const double rx = rng.uniform(
                    generation_key, 21, individual_key, turbine, 0
                );
                const double ry = rng.uniform(
                    generation_key, 21, individual_key, turbine, 1
                );
                child[turbine].x_m =
                    rx * child[turbine].x_m
                    + (1.0 - rx) * mate[turbine].x_m;
                child[turbine].y_m =
                    ry * child[turbine].y_m
                    + (1.0 - ry) * mate[turbine].y_m;
            }
            const double mutation_probability =
                1.0 / (2.0 * static_cast<double>(child.size()));
            for (std::size_t turbine = 0;
                 turbine < child.size();
                 ++turbine) {
                if (
                    rng.uniform(
                        generation_key,
                        22,
                        individual_key,
                        turbine,
                        0
                    ) < mutation_probability
                ) {
                    child[turbine].x_m +=
                        problem.side_length_m()
                        * polynomial_delta(
                            rng.uniform(
                                generation_key,
                                22,
                                individual_key,
                                turbine,
                                1
                            )
                        );
                }
                if (
                    rng.uniform(
                        generation_key,
                        22,
                        individual_key,
                        turbine,
                        2
                    ) < mutation_probability
                ) {
                    child[turbine].y_m +=
                        problem.side_length_m()
                        * polynomial_delta(
                            rng.uniform(
                                generation_key,
                                22,
                                individual_key,
                                turbine,
                                3
                            )
                        );
                }
            }
            problem.repair(
                child,
                config.seed,
                generation_key,
                individual_key
            );
            offspring[static_cast<std::size_t>(index)].layout =
                std::move(child);
        });
        algorithm_seconds += elapsed_seconds(algorithm_started);
        evaluate_individuals(offspring);

        algorithm_started = Clock::now();
        std::vector<Individual> merged;
        merged.reserve(population.size() + offspring.size());
        for (auto& item : population) {
            merged.push_back(std::move(item));
        }
        for (auto& item : offspring) {
            merged.push_back(std::move(item));
        }
        const auto fronts = assign_rank(merged, executor);
        associate(merged, executor);
        std::vector<int> selected;
        std::vector<int> last_front;
        selected.reserve(static_cast<std::size_t>(config.population));
        for (const auto& front : fronts) {
            if (
                selected.size() + front.size()
                <= static_cast<std::size_t>(config.population)
            ) {
                selected.insert(
                    selected.end(), front.begin(), front.end()
                );
            } else {
                last_front = front;
                break;
            }
        }
        std::array<int, 92> niche_count{};
        for (const int index : selected) {
            const int reference_index =
                merged[static_cast<std::size_t>(index)].reference;
            ++niche_count[static_cast<std::size_t>(reference_index)];
        }
        std::array<std::vector<int>, 92> candidates;
        for (const int index : last_front) {
            candidates[
                static_cast<std::size_t>(
                    merged[static_cast<std::size_t>(index)].reference
                )
            ].push_back(index);
        }
        std::uint64_t selection_step = 0;
        while (
            selected.size()
            < static_cast<std::size_t>(config.population)
        ) {
            int chosen_reference = -1;
            int minimum_niche = std::numeric_limits<int>::max();
            for (int reference_index = 0;
                 reference_index < 92;
                 ++reference_index) {
                if (
                    candidates[
                        static_cast<std::size_t>(reference_index)
                    ].empty()
                ) {
                    continue;
                }
                const int count =
                    niche_count[static_cast<std::size_t>(reference_index)];
                if (count < minimum_niche) {
                    minimum_niche = count;
                    chosen_reference = reference_index;
                }
            }
            if (chosen_reference < 0) {
                throw std::runtime_error("T24 NSGA-III niching exhausted");
            }
            auto& bucket =
                candidates[static_cast<std::size_t>(chosen_reference)];
            std::size_t chosen_position = 0;
            if (minimum_niche == 0) {
                for (std::size_t position = 1;
                     position < bucket.size();
                     ++position) {
                    const double candidate_distance =
                        merged[
                            static_cast<std::size_t>(bucket[position])
                        ].reference_distance;
                    const double selected_distance =
                        merged[
                            static_cast<std::size_t>(
                                bucket[chosen_position]
                            )
                        ].reference_distance;
                    if (
                        candidate_distance < selected_distance
                        || (
                            candidate_distance == selected_distance
                            && bucket[position] < bucket[chosen_position]
                        )
                    ) {
                        chosen_position = position;
                    }
                }
            } else {
                chosen_position = static_cast<std::size_t>(
                    rng.integer(
                        0,
                        static_cast<int>(bucket.size()),
                        generation_key,
                        40,
                        selection_step
                    )
                );
            }
            selected.push_back(bucket[chosen_position]);
            bucket.erase(
                bucket.begin()
                + static_cast<std::ptrdiff_t>(chosen_position)
            );
            ++niche_count[static_cast<std::size_t>(chosen_reference)];
            ++selection_step;
        }
        population.clear();
        population.reserve(static_cast<std::size_t>(config.population));
        for (const int index : selected) {
            population.push_back(
                std::move(merged[static_cast<std::size_t>(index)])
            );
        }
        assign_rank(population, executor);
        associate(population, executor);
        algorithm_seconds += elapsed_seconds(algorithm_started);
    }

    std::stable_sort(
        population.begin(),
        population.end(),
        [](const Individual& left, const Individual& right) {
            if (left.rank != right.rank) {
                return left.rank < right.rank;
            }
            if (
                left.evaluation.intermittency_mw
                != right.evaluation.intermittency_mw
            ) {
                return left.evaluation.intermittency_mw
                    < right.evaluation.intermittency_mw;
            }
            return left.evaluation.mean_power_mw
                > right.evaluation.mean_power_mw;
        }
    );
    RunResult result;
    result.problem_id = problem.id();
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers =
        executor.work_receipt().distinct_participants;
    result.population = config.population;
    result.generations = config.generations;
    result.physical_fes =
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1);
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds = algorithm_seconds;
    result.end_to_end_seconds = elapsed_seconds(total_started);
    result.scientific_hash = scientific_hash(population);
    for (const auto& item : population) {
        if (item.rank != 1 || !item.evaluation.feasible) {
            continue;
        }
        result.front.push_back({
            item.evaluation.mean_power_mw,
            item.evaluation.intermittency_mw,
            item.layout,
        });
    }
    return result;
}

}  // namespace core99::t24
