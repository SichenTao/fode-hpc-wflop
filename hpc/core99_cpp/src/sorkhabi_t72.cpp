/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T72 problem, discrete CHCP repair, NSGA-II, and CPU-HPC
Paper/DOI: Constrained Multi-Objective Wind Farm Layout Optimization:
Novel Constraint Handling Approach Based on Constraint Programming;
10.1016/j.renene.2018.03.053
Public source: no author source was located. Related public source:
PyWake ISO-noise implementation at
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git revision
5b07481ec9b3633a74844651648f266ba82a8b32, file
py_wake/noise_models/iso.py SHA256
1986931ae0a78f04a5b7c7adf33c43336d574215ddd3f1477241241079ea24c9
(MIT); formulas are independently expressed here
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/sorkhabi_t72.hpp
Reconstruction: deterministic jittered-Voronoi maps replace unavailable native
maps; a joint branch-and-bound solver replaces proprietary IBM CP Optimizer
while retaining 150 coordinate bins, ten-second call boundary, squared-distance
objective in discrete-bin coordinates, maximum-distance values, and
dynamic-penalty fallback; an m2 interpretation was rejected because it makes
the paper's MD=50/100 repair percentages mathematically unattainable; the open
replacement retains 4096 nearest legal candidates per variable and a
deterministic 2000-node feasibility fallback, with truncations reported
Method/problem semantic IDs: t72_chcp_nsga2_declared_reconstruction_v1;
t72_energy_noise_voronoi9_declared_reconstruction_v1
Controlling contract: shared/contracts/core99_t72_sorkhabi_2018.json
HPC design: counter-keyed offspring events, persistent population-parallel
repair/evaluation, parallel dominance construction, fixed-order environmental
selection, and outer-run parallelism supplied by the command-line driver
Claim boundary: academic declared flexible reproduction, not author code,
IBM CP state, native maps, raw wind matrix, or exact numerical replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sorkhabi_t72.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t72 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kDomainM = 3000.0;
constexpr int kMapAxis = 15;
constexpr int kMapCells = kMapAxis * kMapAxis;
constexpr int kCpBins = 150;
constexpr double kCpStep = kDomainM / static_cast<double>(kCpBins - 1);
constexpr double kHubHeightM = 80.0;
constexpr double kReceiverHeightM = 1.5;
constexpr double kRotorRadiusM = 38.5;
constexpr double kDiameterM = 2.0 * kRotorRadiusM;
constexpr double kMinimumSpacingM = 5.0 * kDiameterM;
constexpr double kThrustCoefficient = 0.8;
constexpr double kTerrainRoughnessM = 0.1;
constexpr double kWakeExpansion =
    0.5 / std::log(kHubHeightM / kTerrainRoughnessM);
constexpr double kWakeDeficit =
    1.0 - std::sqrt(1.0 - kThrustCoefficient);
constexpr double kCrossoverProbability = 0.95;
constexpr double kMutationProbability = 0.05;
constexpr double kSbxIndex = 20.0;
constexpr double kMutationIndex = 20.0;
constexpr std::size_t kRepairCandidateLimit = 4096;
constexpr std::uint64_t kRepairFeasibilityNodeLimit = 2000;
constexpr double kAtmosphericPressurePa = 101325.0;
constexpr double kTemperatureC = 20.0;
constexpr double kRelativeHumidityPercent = 80.0;
constexpr double kGroundFactor = 1.0;
constexpr std::array<double, 8> kOctaveFrequenciesHz = {
    63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0,
};
constexpr std::array<double, 8> kAWeightingDb = {
    -26.2, -16.1, -8.6, -3.2, 0.0, 1.2, 1.0, -1.1,
};

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double squared_distance(const Point& left, const Point& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
}

double clamp_coordinate(double value) {
    return std::clamp(value, 0.0, kDomainM);
}

double power_kw(double speed_mps) {
    if (speed_mps < 4.0 || speed_mps > 25.0) {
        return 0.0;
    }
    if (speed_mps < 15.0) {
        return std::max(0.0, 140.86 * speed_mps - 500.0);
    }
    return 1500.0;
}

double sound_power_db(double speed_mps) {
    constexpr std::array<double, 9> speed = {
        3.0, 7.2, 7.9, 8.6, 9.3, 10.0, 11.5, 12.9, 25.0,
    };
    constexpr std::array<double, 9> level = {
        97.1, 97.1, 99.7, 102.0, 103.4, 104.0, 104.0, 104.0, 104.0,
    };
    if (speed_mps <= speed.front()) {
        return level.front();
    }
    if (speed_mps >= speed.back()) {
        return level.back();
    }
    for (std::size_t upper = 1; upper < speed.size(); ++upper) {
        if (speed_mps <= speed[upper]) {
            const double fraction = (speed_mps - speed[upper - 1])
                / (speed[upper] - speed[upper - 1]);
            return level[upper - 1]
                + fraction * (level[upper] - level[upper - 1]);
        }
    }
    return level.back();
}

double weibull_cdf(double speed, double scale) {
    if (speed <= 0.0) {
        return 0.0;
    }
    return 1.0 - std::exp(-std::pow(speed / scale, 2.0));
}

double atmospheric_absorption_db_per_m(double frequency_hz) {
    const double reference_temperature_k = 293.15;
    const double triple_point_k = 273.16;
    const double temperature_k = kTemperatureC + 273.15;
    const double pressure_atm = kAtmosphericPressurePa / 101325.0;
    const double log10_saturation =
        -6.8346 * std::pow(triple_point_k / temperature_k, 1.261)
        + 4.6151;
    const double saturation = std::pow(10.0, log10_saturation);
    const double humidity =
        kRelativeHumidityPercent / pressure_atm * saturation;
    const double normalized_frequency = frequency_hz / pressure_atm;
    const double frequency_squared =
        normalized_frequency * normalized_frequency;
    const double oxygen_relaxation =
        24.0
        + 4.04e4 * humidity * (0.02 + humidity)
            / (0.391 + humidity);
    const double nitrogen_relaxation =
        std::sqrt(reference_temperature_k / temperature_k)
        * (
            9.0
            + 2.8e2 * humidity
                * std::exp(
                    -4.17
                    * (
                        std::cbrt(reference_temperature_k / temperature_k)
                        - 1.0
                    )
                )
        );
    const double alpha = frequency_squared
        * (
            1.84e-11
                * std::sqrt(temperature_k / reference_temperature_k)
            + std::pow(
                temperature_k / reference_temperature_k,
                -2.5
            )
                * (
                    1.275e-2 * std::exp(-2239.1 / temperature_k)
                        / (
                            oxygen_relaxation
                            + frequency_squared / oxygen_relaxation
                        )
                    + 0.1068 * std::exp(-3352.0 / temperature_k)
                        / (
                            nitrogen_relaxation
                            + frequency_squared / nitrogen_relaxation
                        )
                )
        );
    return alpha * pressure_atm * 20.0 / std::log(10.0);
}

double ground_effect_db(
    double frequency_hz,
    double horizontal_distance_m
) {
    const double source_height = kHubHeightM;
    const double receiver_height = kReceiverHeightM;
    const double q = horizontal_distance_m
            <= 30.0 * (source_height + receiver_height)
        ? 0.0
        : 1.0
            - 30.0 * (source_height + receiver_height)
                / horizontal_distance_m;
    auto a_value = [&](double height) {
        return 1.5
            + 3.0 * std::exp(-0.12 * std::pow(height - 5.0, 2.0))
                * (1.0 - std::exp(-horizontal_distance_m / 50.0))
            + 5.7 * std::exp(-0.09 * height * height)
                * (
                    1.0
                    - std::exp(
                        -2.8e-6
                        * horizontal_distance_m * horizontal_distance_m
                    )
                );
    };
    auto b_value = [&](double height) {
        return 1.5
            + 8.6 * std::exp(-0.09 * height * height)
                * (1.0 - std::exp(-horizontal_distance_m / 50.0));
    };
    auto c_value = [&](double height) {
        return 1.5
            + 14.0 * std::exp(-0.46 * height * height)
                * (1.0 - std::exp(-horizontal_distance_m / 50.0));
    };
    auto d_value = [&](double height) {
        return 1.5
            + 5.0 * std::exp(-0.9 * height * height)
                * (1.0 - std::exp(-horizontal_distance_m / 50.0));
    };
    auto endpoint = [&](double frequency, double height) {
        if (frequency == 63.0) {
            return -1.5;
        }
        if (frequency == 125.0) {
            return -1.5 + kGroundFactor * a_value(height);
        }
        if (frequency == 250.0) {
            return -1.5 + kGroundFactor * b_value(height);
        }
        if (frequency == 500.0) {
            return -1.5 + kGroundFactor * c_value(height);
        }
        if (frequency == 1000.0) {
            return -1.5 + kGroundFactor * d_value(height);
        }
        return -1.5 * (1.0 - kGroundFactor);
    };
    const double middle = frequency_hz == 63.0
        ? -3.0 * q
        : -3.0 * q * (1.0 - kGroundFactor);
    if (frequency_hz != 4000.0) {
        return endpoint(frequency_hz, source_height)
            + endpoint(frequency_hz, receiver_height)
            + middle;
    }
    const double low = endpoint(2000.0, source_height)
        + endpoint(2000.0, receiver_height)
        + middle;
    const double high = endpoint(8000.0, source_height)
        + endpoint(8000.0, receiver_height)
        + middle;
    return low + (4000.0 - 2000.0) / (8000.0 - 2000.0)
        * (high - low);
}

double transmission_delta_db(
    const Point& source,
    const Point& receiver,
    double frequency_hz
) {
    const double dx = source.x_m - receiver.x_m;
    const double dy = source.y_m - receiver.y_m;
    const double horizontal = std::max(1.0, std::hypot(dx, dy));
    const double vertical = kHubHeightM - kReceiverHeightM;
    const double distance = std::sqrt(
        horizontal * horizontal + vertical * vertical
    );
    const double divergence = 20.0 * std::log10(distance) + 11.0;
    const double atmosphere =
        atmospheric_absorption_db_per_m(frequency_hz) * distance;
    return -divergence
        - ground_effect_db(frequency_hz, horizontal)
        - atmosphere;
}

bool dominates_values(
    double left_one,
    double left_two,
    double right_one,
    double right_two
) {
    return left_one <= right_one
        && left_two <= right_two
        && (left_one < right_one || left_two < right_two);
}

struct Individual {
    std::vector<Point> layout;
    Evaluation evaluation;
    double objective_one = 0.0;
    double objective_two = 0.0;
    int rank = 0;
    double crowding = 0.0;
};

std::vector<std::vector<int>> assign_rank(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    const int count = static_cast<int>(population.size());
    std::vector<std::vector<int>> dominates(
        static_cast<std::size_t>(count)
    );
    std::vector<int> dominated_by(static_cast<std::size_t>(count), 0);
    executor.parallel_for(0, count, [&](int left) {
        auto& outgoing = dominates[static_cast<std::size_t>(left)];
        for (int right = 0; right < count; ++right) {
            if (left == right) {
                continue;
            }
            const auto& a = population[static_cast<std::size_t>(left)];
            const auto& b = population[static_cast<std::size_t>(right)];
            if (
                dominates_values(
                    a.objective_one,
                    a.objective_two,
                    b.objective_one,
                    b.objective_two
                )
            ) {
                outgoing.push_back(right);
            } else if (
                dominates_values(
                    b.objective_one,
                    b.objective_two,
                    a.objective_one,
                    a.objective_two
                )
            ) {
                ++dominated_by[static_cast<std::size_t>(left)];
            }
        }
    });
    std::vector<std::vector<int>> fronts;
    fronts.emplace_back();
    for (int index = 0; index < count; ++index) {
        if (dominated_by[static_cast<std::size_t>(index)] == 0) {
            population[static_cast<std::size_t>(index)].rank = 1;
            fronts.front().push_back(index);
        }
    }
    int front_index = 0;
    while (
        front_index < static_cast<int>(fronts.size())
        && !fronts[static_cast<std::size_t>(front_index)].empty()
    ) {
        std::vector<int> next;
        for (int source : fronts[static_cast<std::size_t>(front_index)]) {
            for (int target : dominates[static_cast<std::size_t>(source)]) {
                int& degree = dominated_by[static_cast<std::size_t>(target)];
                --degree;
                if (degree == 0) {
                    population[static_cast<std::size_t>(target)].rank =
                        front_index + 2;
                    next.push_back(target);
                }
            }
        }
        if (!next.empty()) {
            std::sort(next.begin(), next.end());
            fronts.push_back(std::move(next));
        }
        ++front_index;
    }
    return fronts;
}

void assign_crowding(
    std::vector<Individual>& population,
    const std::vector<int>& front
) {
    for (int index : front) {
        population[static_cast<std::size_t>(index)].crowding = 0.0;
    }
    if (front.empty()) {
        return;
    }
    if (front.size() <= 2) {
        for (int index : front) {
            population[static_cast<std::size_t>(index)].crowding =
                std::numeric_limits<double>::infinity();
        }
        return;
    }
    for (int objective = 0; objective < 2; ++objective) {
        std::vector<int> order = front;
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](int left, int right) {
                const auto& a = population[static_cast<std::size_t>(left)];
                const auto& b = population[static_cast<std::size_t>(right)];
                const double av = objective == 0
                    ? a.objective_one
                    : a.objective_two;
                const double bv = objective == 0
                    ? b.objective_one
                    : b.objective_two;
                if (av != bv) {
                    return av < bv;
                }
                return left < right;
            }
        );
        auto value = [&](int index) {
            const auto& item = population[static_cast<std::size_t>(index)];
            return objective == 0
                ? item.objective_one
                : item.objective_two;
        };
        population[static_cast<std::size_t>(order.front())].crowding =
            std::numeric_limits<double>::infinity();
        population[static_cast<std::size_t>(order.back())].crowding =
            std::numeric_limits<double>::infinity();
        const double range = value(order.back()) - value(order.front());
        if (!(range > 0.0)) {
            continue;
        }
        for (std::size_t position = 1; position + 1 < order.size(); ++position) {
            auto& item =
                population[static_cast<std::size_t>(order[position])];
            if (!std::isinf(item.crowding)) {
                item.crowding += (
                    value(order[position + 1])
                    - value(order[position - 1])
                ) / range;
            }
        }
    }
}

void rank_and_crowding(
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor
) {
    const auto fronts = assign_rank(population, executor);
    for (const auto& front : fronts) {
        assign_crowding(population, front);
    }
}

bool crowded_better(
    const Individual& left,
    const Individual& right,
    int left_index,
    int right_index
) {
    if (left.rank != right.rank) {
        return left.rank < right.rank;
    }
    if (left.crowding != right.crowding) {
        return left.crowding > right.crowding;
    }
    return left_index < right_index;
}

double sbx_value(
    double left,
    double right,
    double random,
    bool second_child
) {
    if (std::abs(left - right) <= 1.0e-14) {
        return left;
    }
    const double low = std::min(left, right);
    const double high = std::max(left, right);
    const double beta =
        1.0 + 2.0 * (low - 0.0) / (high - low);
    const double alpha =
        2.0 - std::pow(beta, -(kSbxIndex + 1.0));
    const double beta_q = random <= 1.0 / alpha
        ? std::pow(random * alpha, 1.0 / (kSbxIndex + 1.0))
        : std::pow(
            1.0 / (2.0 - random * alpha),
            1.0 / (kSbxIndex + 1.0)
        );
    const double child_low =
        0.5 * ((low + high) - beta_q * (high - low));
    const double upper_beta =
        1.0 + 2.0 * (kDomainM - high) / (high - low);
    const double upper_alpha =
        2.0 - std::pow(upper_beta, -(kSbxIndex + 1.0));
    const double upper_q = random <= 1.0 / upper_alpha
        ? std::pow(random * upper_alpha, 1.0 / (kSbxIndex + 1.0))
        : std::pow(
            1.0 / (2.0 - random * upper_alpha),
            1.0 / (kSbxIndex + 1.0)
        );
    const double child_high =
        0.5 * ((low + high) + upper_q * (high - low));
    return clamp_coordinate(second_child ? child_high : child_low);
}

double polynomial_mutation(double value, double random) {
    const double delta_one = value / kDomainM;
    const double delta_two = (kDomainM - value) / kDomainM;
    const double exponent = 1.0 / (kMutationIndex + 1.0);
    double delta_q = 0.0;
    if (random <= 0.5) {
        const double xy = 1.0 - delta_one;
        const double term =
            2.0 * random
            + (1.0 - 2.0 * random)
                * std::pow(xy, kMutationIndex + 1.0);
        delta_q = std::pow(term, exponent) - 1.0;
    } else {
        const double xy = 1.0 - delta_two;
        const double term =
            2.0 * (1.0 - random)
            + 2.0 * (random - 0.5)
                * std::pow(xy, kMutationIndex + 1.0);
        delta_q = 1.0 - std::pow(term, exponent);
    }
    return clamp_coordinate(value + delta_q * kDomainM);
}

std::uint64_t front_hash(const std::vector<FrontPoint>& front) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](double value) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    for (const auto& item : front) {
        mix(item.aep_gwh);
        mix(item.maximum_spl_dba);
        for (const auto& point : item.layout) {
            mix(point.x_m);
            mix(point.y_m);
        }
    }
    return hash;
}

std::vector<FrontPoint> extract_front(
    const std::vector<Individual>& population
) {
    std::vector<FrontPoint> result;
    for (std::size_t candidate = 0; candidate < population.size(); ++candidate) {
        const auto& item = population[candidate];
        if (!item.evaluation.feasible) {
            continue;
        }
        bool dominated = false;
        for (std::size_t other = 0; other < population.size(); ++other) {
            if (
                other != candidate
                && population[other].evaluation.feasible
                && population[other].evaluation.aep_gwh
                    >= item.evaluation.aep_gwh
                && population[other].evaluation.maximum_spl_dba
                    <= item.evaluation.maximum_spl_dba
                && (
                    population[other].evaluation.aep_gwh
                        > item.evaluation.aep_gwh
                    || population[other].evaluation.maximum_spl_dba
                        < item.evaluation.maximum_spl_dba
                )
            ) {
                dominated = true;
                break;
            }
        }
        if (!dominated) {
            result.push_back({
                item.evaluation.aep_gwh,
                item.evaluation.maximum_spl_dba,
                item.layout,
            });
        }
    }
    std::stable_sort(
        result.begin(),
        result.end(),
        [](const FrontPoint& left, const FrontPoint& right) {
            if (left.aep_gwh != right.aep_gwh) {
                return left.aep_gwh < right.aep_gwh;
            }
            if (left.maximum_spl_dba != right.maximum_spl_dba) {
                return left.maximum_spl_dba < right.maximum_spl_dba;
            }
            return left.layout.size() < right.layout.size();
        }
    );
    return result;
}

}  // namespace

Problem::Problem(int land_availability_percent, int turbine_count)
    : id_(
          "t72_phi"
          + std::to_string(land_availability_percent)
          + "_n"
          + std::to_string(turbine_count)
      ),
      land_availability_percent_(land_availability_percent),
      turbine_count_(turbine_count),
      population_size_(
          land_availability_percent == 70
              ? 200
              : (land_availability_percent == 80 ? 150 : 100)
      ) {
    if (
        (land_availability_percent_ != 70
         && land_availability_percent_ != 80
         && land_availability_percent_ != 90)
        || (turbine_count_ != 5
            && turbine_count_ != 10
            && turbine_count_ != 15)
    ) {
        throw std::invalid_argument("T72 case outside paper matrix");
    }
    build_map();
    build_wind();
    build_noise();
}

const std::string& Problem::id() const noexcept { return id_; }
int Problem::land_availability_percent() const noexcept {
    return land_availability_percent_;
}
int Problem::turbine_count() const noexcept { return turbine_count_; }
int Problem::population_size() const noexcept { return population_size_; }
double Problem::measured_land_availability() const noexcept {
    return measured_land_availability_;
}
const std::vector<Point>& Problem::receptors() const noexcept {
    return receptors_;
}

int Problem::nearest_map_seed(const Point& point) const {
    const double macro = kDomainM / static_cast<double>(kMapAxis);
    const int column = std::clamp(
        static_cast<int>(point.x_m / macro),
        0,
        kMapAxis - 1
    );
    const int row = std::clamp(
        static_cast<int>(point.y_m / macro),
        0,
        kMapAxis - 1
    );
    int best = -1;
    double best_distance = std::numeric_limits<double>::infinity();
    for (
        int candidate_row = std::max(0, row - 2);
        candidate_row <= std::min(kMapAxis - 1, row + 2);
        ++candidate_row
    ) {
        for (
            int candidate_column = std::max(0, column - 2);
            candidate_column <= std::min(kMapAxis - 1, column + 2);
            ++candidate_column
        ) {
            const int index =
                candidate_row * kMapAxis + candidate_column;
            const double distance = squared_distance(
                point,
                map_seeds_[static_cast<std::size_t>(index)].point
            );
            if (distance < best_distance) {
                best_distance = distance;
                best = index;
            }
        }
    }
    if (best < 0) {
        throw std::runtime_error("T72 Voronoi lookup failed");
    }
    return best;
}

bool Problem::regulatory_forbidden(const Point& point) const {
    if (
        point.x_m < 0.0
        || point.x_m > kDomainM
        || point.y_m < 0.0
        || point.y_m > kDomainM
    ) {
        return true;
    }
    return map_seeds_[static_cast<std::size_t>(
        nearest_map_seed(point)
    )].forbidden;
}

double Problem::regulatory_distance(const Point& point) const {
    if (!regulatory_forbidden(point)) {
        return 0.0;
    }
    const int x = std::clamp(
        static_cast<int>(std::llround(point.x_m / kCpStep)),
        0,
        kCpBins - 1
    );
    const int y = std::clamp(
        static_cast<int>(std::llround(point.y_m / kCpStep)),
        0,
        kCpBins - 1
    );
    return regulatory_distance_grid_[
        static_cast<std::size_t>(y * kCpBins + x)
    ];
}

void Problem::build_map() {
    const std::uint64_t map_key =
        720000ULL
        + static_cast<std::uint64_t>(land_availability_percent_) * 100ULL
        + static_cast<std::uint64_t>(turbine_count_);
    const fode::CounterRng rng(map_key);
    const double macro = kDomainM / static_cast<double>(kMapAxis);
    map_seeds_.reserve(kMapCells);
    for (int row = 0; row < kMapAxis; ++row) {
        for (int column = 0; column < kMapAxis; ++column) {
            const int index = row * kMapAxis + column;
            const double x_fraction =
                0.25 + 0.5 * rng.uniform(0, 7201, index, 0);
            const double y_fraction =
                0.25 + 0.5 * rng.uniform(0, 7201, index, 1);
            map_seeds_.push_back({
                {
                    (static_cast<double>(column) + x_fraction) * macro,
                    (static_cast<double>(row) + y_fraction) * macro,
                },
                false,
            });
        }
    }
    std::vector<int> map_cell(
        static_cast<std::size_t>(kCpBins * kCpBins),
        0
    );
    std::vector<int> counts(kMapCells, 0);
    for (int y = 0; y < kCpBins; ++y) {
        for (int x = 0; x < kCpBins; ++x) {
            const Point point{
                static_cast<double>(x) * kCpStep,
                static_cast<double>(y) * kCpStep,
            };
            const int cell = nearest_map_seed(point);
            map_cell[static_cast<std::size_t>(y * kCpBins + x)] = cell;
            ++counts[static_cast<std::size_t>(cell)];
        }
    }
    std::vector<int> order(kMapCells);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](int left, int right) {
            const double left_key = rng.uniform(0, 7202, left);
            const double right_key = rng.uniform(0, 7202, right);
            if (left_key != right_key) {
                return left_key < right_key;
            }
            return left < right;
        }
    );
    const int total = kCpBins * kCpBins;
    const int target_unavailable = static_cast<int>(std::llround(
        static_cast<double>(100 - land_availability_percent_)
        * static_cast<double>(total) / 100.0
    ));
    int unavailable = 0;
    for (int cell : order) {
        const int next =
            unavailable + counts[static_cast<std::size_t>(cell)];
        if (
            std::abs(next - target_unavailable)
            <= std::abs(unavailable - target_unavailable)
        ) {
            map_seeds_[static_cast<std::size_t>(cell)].forbidden = true;
            unavailable = next;
        }
        if (unavailable >= target_unavailable) {
            break;
        }
    }
    measured_land_availability_ =
        1.0 - static_cast<double>(unavailable) / static_cast<double>(total);
    for (const auto& seed : map_seeds_) {
        if (seed.forbidden) {
            receptors_.push_back(seed.point);
        }
    }

    regulatory_distance_grid_.assign(
        static_cast<std::size_t>(total),
        0.0
    );
    auto grid_forbidden = [&](int x, int y) {
        const int cell =
            map_cell[static_cast<std::size_t>(y * kCpBins + x)];
        return map_seeds_[static_cast<std::size_t>(cell)].forbidden;
    };
    for (int y = 0; y < kCpBins; ++y) {
        for (int x = 0; x < kCpBins; ++x) {
            if (!grid_forbidden(x, y)) {
                continue;
            }
            double best = std::numeric_limits<double>::infinity();
            for (int radius = 1; radius < kCpBins; ++radius) {
                const int low_x = std::max(0, x - radius);
                const int high_x = std::min(kCpBins - 1, x + radius);
                const int low_y = std::max(0, y - radius);
                const int high_y = std::min(kCpBins - 1, y + radius);
                for (int candidate_x = low_x; candidate_x <= high_x;
                     ++candidate_x) {
                    for (int candidate_y : {low_y, high_y}) {
                        if (!grid_forbidden(candidate_x, candidate_y)) {
                            best = std::min(
                                best,
                                kCpStep * std::hypot(
                                    static_cast<double>(candidate_x - x),
                                    static_cast<double>(candidate_y - y)
                                )
                            );
                        }
                    }
                }
                for (int candidate_y = low_y + 1;
                     candidate_y < high_y; ++candidate_y) {
                    for (int candidate_x : {low_x, high_x}) {
                        if (!grid_forbidden(candidate_x, candidate_y)) {
                            best = std::min(
                                best,
                                kCpStep * std::hypot(
                                    static_cast<double>(candidate_x - x),
                                    static_cast<double>(candidate_y - y)
                                )
                            );
                        }
                    }
                }
                if (
                    std::isfinite(best)
                    && best <= static_cast<double>(radius) * kCpStep
                ) {
                    break;
                }
            }
            regulatory_distance_grid_[
                static_cast<std::size_t>(y * kCpBins + x)
            ] = best;
        }
    }
}

void Problem::build_wind() {
    constexpr std::array<double, 24> scale = {
        7.0, 5.0, 5.0, 5.0, 5.0, 4.0,
        5.0, 6.0, 7.0, 7.0, 8.0, 9.5,
        10.0, 8.5, 8.5, 6.5, 4.6, 2.6,
        8.0, 5.0, 6.4, 5.2, 4.5, 3.9,
    };
    constexpr std::array<double, 24> direction_probability = {
        0.0002, 0.0080, 0.0227, 0.0242, 0.0225, 0.0339,
        0.0423, 0.0290, 0.0617, 0.0813, 0.0994, 0.1394,
        0.1839, 0.1115, 0.0765, 0.0080, 0.0051, 0.0019,
        0.0012, 0.0010, 0.0017, 0.0031, 0.0097, 0.0317,
    };
    const double direction_sum = std::accumulate(
        direction_probability.begin(),
        direction_probability.end(),
        0.0
    );
    direction_degrees_.reserve(24);
    wind_speeds_mps_.reserve(43);
    joint_probabilities_.reserve(24 * 43);
    for (int speed_index = 0; speed_index < 43; ++speed_index) {
        wind_speeds_mps_.push_back(
            4.0 + 0.5 * static_cast<double>(speed_index)
        );
    }
    for (int direction = 0; direction < 24; ++direction) {
        direction_degrees_.push_back(std::fmod(
            97.5 + 15.0 * static_cast<double>(direction),
            360.0
        ));
        std::array<double, 43> conditional{};
        double conditional_sum = 0.0;
        for (int speed_index = 0; speed_index < 43; ++speed_index) {
            const double center = wind_speeds_mps_[
                static_cast<std::size_t>(speed_index)
            ];
            const double lower = center - 0.25;
            const double upper = center + 0.25;
            conditional[static_cast<std::size_t>(speed_index)] =
                weibull_cdf(upper, scale[static_cast<std::size_t>(direction)])
                - weibull_cdf(
                    lower,
                    scale[static_cast<std::size_t>(direction)]
                );
            conditional_sum +=
                conditional[static_cast<std::size_t>(speed_index)];
        }
        for (int speed_index = 0; speed_index < 43; ++speed_index) {
            joint_probabilities_.push_back(
                direction_probability[static_cast<std::size_t>(direction)]
                / direction_sum
                * conditional[static_cast<std::size_t>(speed_index)]
                / conditional_sum
            );
        }
    }
}

void Problem::build_noise() {
    expected_acoustic_source_energy_.assign(8, 0.0);
    for (std::size_t direction = 0; direction < 24; ++direction) {
        for (std::size_t speed_index = 0; speed_index < 43; ++speed_index) {
            const double probability = joint_probabilities_[
                direction * 43 + speed_index
            ];
            const double level = sound_power_db(
                wind_speeds_mps_[speed_index]
            );
            for (std::size_t band = 0; band < 8; ++band) {
                expected_acoustic_source_energy_[band] += probability
                    * std::pow(
                        10.0,
                        0.1 * (level + kAWeightingDb[band])
                    );
            }
        }
    }
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (static_cast<int>(layout.size()) != turbine_count_) {
        throw std::invalid_argument("T72 layout cardinality mismatch");
    }
    Evaluation result;
    for (int left = 0; left < turbine_count_; ++left) {
        if (regulatory_forbidden(layout[static_cast<std::size_t>(left)])) {
            result.regulatory_violation_m += regulatory_distance(
                layout[static_cast<std::size_t>(left)]
            );
        }
        for (int right = left + 1; right < turbine_count_; ++right) {
            const double distance = std::sqrt(squared_distance(
                layout[static_cast<std::size_t>(left)],
                layout[static_cast<std::size_t>(right)]
            ));
            result.proximity_violation_m +=
                std::max(0.0, kMinimumSpacingM - distance);
        }
    }

    double expected_power_kw = 0.0;
    for (std::size_t direction = 0;
         direction < direction_degrees_.size(); ++direction) {
        const double angle =
            (270.0 - direction_degrees_[direction])
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        std::array<double, 15> factor{};
        for (int downstream = 0; downstream < turbine_count_; ++downstream) {
            const Point& target =
                layout[static_cast<std::size_t>(downstream)];
            const double target_along =
                cosine * target.x_m + sine * target.y_m;
            const double target_across =
                -sine * target.x_m + cosine * target.y_m;
            double squared_deficit = 0.0;
            for (int upstream = 0; upstream < turbine_count_; ++upstream) {
                if (upstream == downstream) {
                    continue;
                }
                const Point& source =
                    layout[static_cast<std::size_t>(upstream)];
                const double source_along =
                    cosine * source.x_m + sine * source.y_m;
                const double distance = target_along - source_along;
                if (distance <= 0.0) {
                    continue;
                }
                const double source_across =
                    -sine * source.x_m + cosine * source.y_m;
                const double radius =
                    kRotorRadiusM + kWakeExpansion * distance;
                if (std::abs(target_across - source_across) > radius) {
                    continue;
                }
                const double deficit =
                    kWakeDeficit
                    * kRotorRadiusM * kRotorRadiusM
                    / (radius * radius);
                squared_deficit += deficit * deficit;
            }
            factor[static_cast<std::size_t>(downstream)] =
                std::max(0.0, 1.0 - std::sqrt(squared_deficit));
        }
        for (std::size_t speed_index = 0; speed_index < 43; ++speed_index) {
            const double probability = joint_probabilities_[
                direction * 43 + speed_index
            ];
            const double free_speed = wind_speeds_mps_[speed_index];
            for (int turbine = 0; turbine < turbine_count_; ++turbine) {
                expected_power_kw += probability * power_kw(
                    free_speed * factor[static_cast<std::size_t>(turbine)]
                );
            }
        }
    }
    result.aep_gwh = 8760.0 * expected_power_kw / 1.0e6;

    double maximum_spl = -std::numeric_limits<double>::infinity();
    for (const Point& receptor : receptors_) {
        double acoustic_energy = 0.0;
        for (const Point& source : layout) {
            for (std::size_t band = 0; band < 8; ++band) {
                acoustic_energy += expected_acoustic_source_energy_[band]
                    * std::pow(
                        10.0,
                        0.1 * transmission_delta_db(
                            source,
                            receptor,
                            kOctaveFrequenciesHz[band]
                        )
                    );
            }
        }
        maximum_spl = std::max(
            maximum_spl,
            10.0 * std::log10(
                std::max(acoustic_energy, 1.0e-300)
            )
        );
    }
    result.maximum_spl_dba = maximum_spl;
    result.feasible =
        result.proximity_violation_m <= 1.0e-9
        && result.regulatory_violation_m <= 1.0e-9;
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<Point>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(layouts.size());
    executor.parallel_for(
        0,
        static_cast<int>(layouts.size()),
        [&](int index) {
            result[static_cast<std::size_t>(index)] =
                evaluate(layouts[static_cast<std::size_t>(index)]);
        }
    );
    return result;
}

RepairReceipt Problem::repair(
    std::vector<Point>& layout,
    double maximum_squared_displacement_bin2,
    double time_limit_seconds
) const {
    if (
        static_cast<int>(layout.size()) != turbine_count_
        || maximum_squared_displacement_bin2 < 0.0
        || time_limit_seconds <= 0.0
    ) {
        throw std::invalid_argument("T72 repair configuration mismatch");
    }
    RepairReceipt receipt;
    std::vector<bool> bad(static_cast<std::size_t>(turbine_count_), false);
    for (int left = 0; left < turbine_count_; ++left) {
        if (regulatory_forbidden(layout[static_cast<std::size_t>(left)])) {
            bad[static_cast<std::size_t>(left)] = true;
        }
        for (int right = left + 1; right < turbine_count_; ++right) {
            if (
                squared_distance(
                    layout[static_cast<std::size_t>(left)],
                    layout[static_cast<std::size_t>(right)]
                )
                < kMinimumSpacingM * kMinimumSpacingM
            ) {
                bad[static_cast<std::size_t>(left)] = true;
                bad[static_cast<std::size_t>(right)] = true;
            }
        }
    }
    std::vector<int> variables;
    for (int index = 0; index < turbine_count_; ++index) {
        if (bad[static_cast<std::size_t>(index)]) {
            variables.push_back(index);
        }
    }
    receipt.infeasible_turbines = static_cast<int>(variables.size());
    if (variables.empty()) {
        return receipt;
    }
    receipt.attempted = true;
    const std::vector<Point> unrepaired_layout = layout;
    const double maximum_pair_separation_gain =
        kCpStep * std::sqrt(2.0 * maximum_squared_displacement_bin2);
    for (std::size_t left = 0; left < variables.size(); ++left) {
        for (std::size_t right = left + 1; right < variables.size(); ++right) {
            const Point& first = layout[static_cast<std::size_t>(
                variables[left]
            )];
            const Point& second = layout[static_cast<std::size_t>(
                variables[right]
            )];
            const double distance = std::sqrt(
                squared_distance(first, second)
            );
            if (
                distance < kMinimumSpacingM
                && distance + maximum_pair_separation_gain
                    < kMinimumSpacingM - 1.0e-9
            ) {
                return receipt;
            }
        }
    }

    struct Candidate {
        Point point;
        double cost = 0.0;
    };
    std::vector<std::vector<Candidate>> candidates(variables.size());
    const double radius =
        kCpStep * std::sqrt(maximum_squared_displacement_bin2);
    for (std::size_t slot = 0; slot < variables.size(); ++slot) {
        const int variable = variables[slot];
        const Point original = layout[static_cast<std::size_t>(variable)];
        const int center_x = static_cast<int>(
            std::llround(original.x_m / kCpStep)
        );
        const int center_y = static_cast<int>(
            std::llround(original.y_m / kCpStep)
        );
        const int bins = static_cast<int>(std::ceil(radius / kCpStep)) + 1;
        for (
            int y = std::max(0, center_y - bins);
            y <= std::min(kCpBins - 1, center_y + bins);
            ++y
        ) {
            for (
                int x = std::max(0, center_x - bins);
                x <= std::min(kCpBins - 1, center_x + bins);
                ++x
            ) {
                const Point point{
                    static_cast<double>(x) * kCpStep,
                    static_cast<double>(y) * kCpStep,
                };
                const double cost =
                    squared_distance(point, original)
                    / (kCpStep * kCpStep);
                if (
                    cost > maximum_squared_displacement_bin2 + 1.0e-9
                    || regulatory_forbidden(point)
                ) {
                    continue;
                }
                bool valid = true;
                for (int fixed = 0; fixed < turbine_count_; ++fixed) {
                    if (
                        fixed != variable
                        && !bad[static_cast<std::size_t>(fixed)]
                        && squared_distance(
                            point,
                            layout[static_cast<std::size_t>(fixed)]
                        )
                            < kMinimumSpacingM * kMinimumSpacingM
                    ) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    candidates[slot].push_back({point, cost});
                }
            }
        }
        auto candidate_less =
            [](const Candidate& left, const Candidate& right) {
                if (left.cost != right.cost) {
                    return left.cost < right.cost;
                }
                if (left.point.x_m != right.point.x_m) {
                    return left.point.x_m < right.point.x_m;
                }
                return left.point.y_m < right.point.y_m;
            };
        if (candidates[slot].size() > kRepairCandidateLimit) {
            std::nth_element(
                candidates[slot].begin(),
                candidates[slot].begin()
                    + static_cast<std::ptrdiff_t>(kRepairCandidateLimit),
                candidates[slot].end(),
                candidate_less
            );
            candidates[slot].resize(kRepairCandidateLimit);
        }
        std::stable_sort(
            candidates[slot].begin(),
            candidates[slot].end(),
            candidate_less
        );
        if (candidates[slot].empty()) {
            return receipt;
        }
    }

    std::vector<int> search_order(variables.size());
    std::iota(search_order.begin(), search_order.end(), 0);
    std::stable_sort(
        search_order.begin(),
        search_order.end(),
        [&](int left, int right) {
            const auto left_size =
                candidates[static_cast<std::size_t>(left)].size();
            const auto right_size =
                candidates[static_cast<std::size_t>(right)].size();
            if (left_size != right_size) {
                return left_size < right_size;
            }
            return variables[static_cast<std::size_t>(left)]
                < variables[static_cast<std::size_t>(right)];
        }
    );
    std::vector<Point> selected(variables.size());
    std::vector<bool> assigned(variables.size(), false);
    std::vector<Point> best;
    double best_cost = std::numeric_limits<double>::infinity();
    auto try_greedy = [&](const std::vector<int>& order) {
        std::vector<Point> greedy_selected(variables.size());
        std::vector<bool> greedy_assigned(variables.size(), false);
        double greedy_cost = 0.0;
        for (int slot : order) {
            bool placed = false;
            for (
                const Candidate& candidate :
                candidates[static_cast<std::size_t>(slot)]
            ) {
                if (
                    greedy_cost + candidate.cost
                    > maximum_squared_displacement_bin2 + 1.0e-9
                ) {
                    continue;
                }
                bool compatible = true;
                for (
                    std::size_t other = 0;
                    other < greedy_assigned.size();
                    ++other
                ) {
                    if (
                        greedy_assigned[other]
                        && squared_distance(
                            candidate.point,
                            greedy_selected[other]
                        ) < kMinimumSpacingM * kMinimumSpacingM
                    ) {
                        compatible = false;
                        break;
                    }
                }
                if (compatible) {
                    greedy_selected[static_cast<std::size_t>(slot)] =
                        candidate.point;
                    greedy_assigned[static_cast<std::size_t>(slot)] = true;
                    greedy_cost += candidate.cost;
                    placed = true;
                    break;
                }
            }
            if (!placed) {
                return;
            }
        }
        if (greedy_cost < best_cost) {
            best_cost = greedy_cost;
            best = std::move(greedy_selected);
        }
    };
    try_greedy(search_order);
    std::vector<int> alternative_order = search_order;
    std::reverse(alternative_order.begin(), alternative_order.end());
    try_greedy(alternative_order);
    for (
        std::size_t rotation = 1;
        rotation < search_order.size()
            && rotation < static_cast<std::size_t>(8);
        ++rotation
    ) {
        alternative_order = search_order;
        std::rotate(
            alternative_order.begin(),
            alternative_order.begin()
                + static_cast<std::ptrdiff_t>(rotation),
            alternative_order.end()
        );
        try_greedy(alternative_order);
    }
    std::vector<double> suffix_lower_bound(
        search_order.size() + 1,
        0.0
    );
    for (std::size_t depth = search_order.size(); depth-- > 0;) {
        const int slot = search_order[depth];
        suffix_lower_bound[depth] =
            suffix_lower_bound[depth + 1]
            + candidates[static_cast<std::size_t>(slot)].front().cost;
    }
    const double global_lower_bound = suffix_lower_bound.front();
    bool proven_optimal =
        best_cost <= global_lower_bound + 1.0e-9;
    const auto started = Clock::now();
    std::function<void(std::size_t, double)> search =
        [&](std::size_t depth, double cost) {
            if (receipt.timed_out || proven_optimal) {
                return;
            }
            ++receipt.search_nodes;
            if (receipt.search_nodes > kRepairFeasibilityNodeLimit) {
                receipt.node_limit_hit = true;
                return;
            }
            if (
                (receipt.search_nodes & 1023ULL) == 0ULL
                && elapsed_seconds(started) > time_limit_seconds
            ) {
                receipt.timed_out = true;
                return;
            }
            if (
                cost + suffix_lower_bound[depth]
                    >= best_cost - 1.0e-12
            ) {
                return;
            }
            double compatible_lower_bound = cost;
            for (
                std::size_t future_depth = depth;
                future_depth < search_order.size();
                ++future_depth
            ) {
                const int future_slot = search_order[future_depth];
                double least_compatible_cost =
                    std::numeric_limits<double>::infinity();
                for (
                    const Candidate& candidate :
                    candidates[static_cast<std::size_t>(future_slot)]
                ) {
                    bool compatible = true;
                    for (
                        std::size_t other = 0;
                        other < assigned.size();
                        ++other
                    ) {
                        if (
                            assigned[other]
                            && squared_distance(
                                candidate.point,
                                selected[other]
                            ) < kMinimumSpacingM * kMinimumSpacingM
                        ) {
                            compatible = false;
                            break;
                        }
                    }
                    if (compatible) {
                        least_compatible_cost = candidate.cost;
                        break;
                    }
                }
                if (!std::isfinite(least_compatible_cost)) {
                    return;
                }
                compatible_lower_bound += least_compatible_cost;
            }
            if (compatible_lower_bound >= best_cost - 1.0e-12) {
                return;
            }
            if (depth == search_order.size()) {
                if (cost < best_cost) {
                    best_cost = cost;
                    best = selected;
                    proven_optimal =
                        best_cost <= global_lower_bound + 1.0e-9;
                }
                return;
            }
            const int slot = search_order[depth];
            for (const Candidate& candidate :
                 candidates[static_cast<std::size_t>(slot)]) {
                const double next_cost = cost + candidate.cost;
                if (
                    next_cost > maximum_squared_displacement_bin2 + 1.0e-9
                    || next_cost >= best_cost
                ) {
                    continue;
                }
                bool valid = true;
                for (std::size_t other = 0; other < assigned.size(); ++other) {
                    if (
                        assigned[other]
                        && squared_distance(candidate.point, selected[other])
                            < kMinimumSpacingM * kMinimumSpacingM
                    ) {
                        valid = false;
                        break;
                    }
                }
                if (!valid) {
                    continue;
                }
                selected[static_cast<std::size_t>(slot)] = candidate.point;
                assigned[static_cast<std::size_t>(slot)] = true;
                search(depth + 1, next_cost);
                assigned[static_cast<std::size_t>(slot)] = false;
                if (
                    receipt.timed_out
                    || receipt.node_limit_hit
                    || proven_optimal
                ) {
                    break;
                }
            }
        };
    if (best.empty()) {
        search(0, 0.0);
    }
    if (!best.empty()) {
        for (std::size_t slot = 0; slot < variables.size(); ++slot) {
            layout[static_cast<std::size_t>(variables[slot])] = best[slot];
        }
        bool feasible = true;
        for (int left = 0; left < turbine_count_ && feasible; ++left) {
            if (regulatory_forbidden(layout[static_cast<std::size_t>(left)])) {
                feasible = false;
                break;
            }
            for (int right = left + 1; right < turbine_count_; ++right) {
                if (
                    squared_distance(
                        layout[static_cast<std::size_t>(left)],
                        layout[static_cast<std::size_t>(right)]
                    )
                    < kMinimumSpacingM * kMinimumSpacingM
                ) {
                    feasible = false;
                    break;
                }
            }
        }
        if (feasible) {
            receipt.repaired = true;
            receipt.squared_displacement_bin2 = best_cost;
        } else {
            layout = unrepaired_layout;
        }
    }
    return receipt;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers <= 0
        || config.physical_fes < static_cast<std::uint64_t>(
            problem.population_size()
        )
        || config.maximum_repair_distance_bin2 < 0.0
        || config.penalty_coefficient <= 0.0
    ) {
        throw std::invalid_argument("T72 run configuration invalid");
    }
    const auto run_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int population_size = problem.population_size();
    const int turbines = problem.turbine_count();
    const int maximum_generations = std::max(
        1,
        static_cast<int>(config.physical_fes / population_size) - 1
    );
    std::vector<Individual> population(
        static_cast<std::size_t>(population_size)
    );
    for (int individual = 0; individual < population_size; ++individual) {
        auto& layout =
            population[static_cast<std::size_t>(individual)].layout;
        layout.resize(static_cast<std::size_t>(turbines));
        for (int turbine = 0; turbine < turbines; ++turbine) {
            layout[static_cast<std::size_t>(turbine)] = {
                kDomainM * rng.uniform(0, 7203, individual, turbine, 0),
                kDomainM * rng.uniform(0, 7203, individual, turbine, 1),
            };
        }
    }

    std::uint64_t repair_attempts = 0;
    std::uint64_t repair_successes = 0;
    std::uint64_t repair_timeouts = 0;
    std::uint64_t repair_node_limit_hits = 0;
    std::uint64_t repair_nodes = 0;
    double repair_seconds = 0.0;
    double evaluator_seconds = 0.0;
    auto evaluate_batch = [&](
        std::vector<Individual>& batch,
        int generation,
        std::uint64_t& physical_fes
    ) {
        std::vector<RepairReceipt> repairs(batch.size());
        const auto repair_started = Clock::now();
        executor.parallel_for(
            0,
            static_cast<int>(batch.size()),
            [&](int index) {
                auto& individual = batch[static_cast<std::size_t>(index)];
                repairs[static_cast<std::size_t>(index)] = problem.repair(
                    individual.layout,
                    config.maximum_repair_distance_bin2
                );
            }
        );
        repair_seconds += elapsed_seconds(repair_started);
        const auto evaluator_started = Clock::now();
        executor.parallel_for(
            0,
            static_cast<int>(batch.size()),
            [&](int index) {
                auto& individual = batch[static_cast<std::size_t>(index)];
                individual.evaluation = problem.evaluate(individual.layout);
            }
        );
        evaluator_seconds += elapsed_seconds(evaluator_started);
        physical_fes += batch.size();
        const double progress = std::min(
            1.0,
            static_cast<double>(generation + 1)
                / static_cast<double>(maximum_generations + 1)
        );
        const double penalty_scale =
            config.penalty_coefficient * progress * progress;
        for (std::size_t index = 0; index < batch.size(); ++index) {
            const auto& repair = repairs[index];
            repair_attempts += repair.attempted ? 1U : 0U;
            repair_successes += repair.repaired ? 1U : 0U;
            repair_timeouts += repair.timed_out ? 1U : 0U;
            repair_node_limit_hits += repair.node_limit_hit ? 1U : 0U;
            repair_nodes += repair.search_nodes;
            auto& individual = batch[index];
            const double violation =
                individual.evaluation.proximity_violation_m
                * individual.evaluation.proximity_violation_m
                + individual.evaluation.regulatory_violation_m
                    * individual.evaluation.regulatory_violation_m;
            individual.objective_one =
                -individual.evaluation.aep_gwh
                + penalty_scale * violation;
            individual.objective_two =
                individual.evaluation.maximum_spl_dba
                + penalty_scale * violation;
        }
    };

    std::uint64_t physical_fes = 0;
    evaluate_batch(population, 0, physical_fes);
    rank_and_crowding(population, executor);
    std::vector<double> convergence_history;
    int generations = 0;
    bool converged = false;
    while (physical_fes < config.physical_fes && !converged) {
        const int offspring_count = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(population_size),
            config.physical_fes - physical_fes
        ));
        std::vector<Individual> offspring(
            static_cast<std::size_t>(offspring_count)
        );
        executor.parallel_for(0, offspring_count, [&](int child) {
            auto tournament = [&](int draw) {
                const int left = rng.integer(
                    0,
                    population_size,
                    static_cast<std::uint64_t>(generations + 1),
                    7204,
                    static_cast<std::uint64_t>(child),
                    static_cast<std::uint64_t>(draw),
                    0
                );
                const int right = rng.integer(
                    0,
                    population_size,
                    static_cast<std::uint64_t>(generations + 1),
                    7204,
                    static_cast<std::uint64_t>(child),
                    static_cast<std::uint64_t>(draw),
                    1
                );
                return crowded_better(
                    population[static_cast<std::size_t>(left)],
                    population[static_cast<std::size_t>(right)],
                    left,
                    right
                ) ? left : right;
            };
            const int parent_one = tournament(0);
            const int parent_two = tournament(1);
            auto& layout =
                offspring[static_cast<std::size_t>(child)].layout;
            layout.resize(static_cast<std::size_t>(turbines));
            const bool crossover = rng.uniform(
                static_cast<std::uint64_t>(generations + 1),
                7205,
                static_cast<std::uint64_t>(child)
            ) < kCrossoverProbability;
            for (int turbine = 0; turbine < turbines; ++turbine) {
                const Point& first =
                    population[static_cast<std::size_t>(parent_one)]
                        .layout[static_cast<std::size_t>(turbine)];
                const Point& second =
                    population[static_cast<std::size_t>(parent_two)]
                        .layout[static_cast<std::size_t>(turbine)];
                Point candidate = first;
                for (int coordinate = 0; coordinate < 2; ++coordinate) {
                    const double first_value =
                        coordinate == 0 ? first.x_m : first.y_m;
                    const double second_value =
                        coordinate == 0 ? second.x_m : second.y_m;
                    double value = first_value;
                    if (crossover) {
                        value = sbx_value(
                            first_value,
                            second_value,
                            rng.uniform(
                                static_cast<std::uint64_t>(generations + 1),
                                7206,
                                static_cast<std::uint64_t>(child),
                                static_cast<std::uint64_t>(
                                    2 * turbine + coordinate
                                )
                            ),
                            (child & 1) != 0
                        );
                    }
                    if (
                        rng.uniform(
                            static_cast<std::uint64_t>(generations + 1),
                            7207,
                            static_cast<std::uint64_t>(child),
                            static_cast<std::uint64_t>(
                                2 * turbine + coordinate
                            )
                        ) < kMutationProbability
                    ) {
                        value = polynomial_mutation(
                            value,
                            rng.uniform(
                                static_cast<std::uint64_t>(generations + 1),
                                7208,
                                static_cast<std::uint64_t>(child),
                                static_cast<std::uint64_t>(
                                    2 * turbine + coordinate
                                )
                            )
                        );
                    }
                    if (coordinate == 0) {
                        candidate.x_m = value;
                    } else {
                        candidate.y_m = value;
                    }
                }
                layout[static_cast<std::size_t>(turbine)] = candidate;
            }
        });
        evaluate_batch(
            offspring,
            generations + 1,
            physical_fes
        );
        std::vector<Individual> combined;
        combined.reserve(population.size() + offspring.size());
        for (auto& item : population) {
            combined.push_back(std::move(item));
        }
        for (auto& item : offspring) {
            combined.push_back(std::move(item));
        }
        rank_and_crowding(combined, executor);
        std::stable_sort(
            combined.begin(),
            combined.end(),
            [](const Individual& left, const Individual& right) {
                if (left.rank != right.rank) {
                    return left.rank < right.rank;
                }
                if (left.crowding != right.crowding) {
                    return left.crowding > right.crowding;
                }
                if (left.objective_one != right.objective_one) {
                    return left.objective_one < right.objective_one;
                }
                return left.objective_two < right.objective_two;
            }
        );
        if (combined.size() > static_cast<std::size_t>(population_size)) {
            combined.resize(static_cast<std::size_t>(population_size));
        }
        population = std::move(combined);
        rank_and_crowding(population, executor);
        ++generations;

        std::vector<double> finite_crowding;
        for (const auto& item : population) {
            if (item.rank == 1 && std::isfinite(item.crowding)) {
                finite_crowding.push_back(item.crowding);
            }
        }
        if (finite_crowding.size() >= 3) {
            const double mean = std::accumulate(
                finite_crowding.begin(),
                finite_crowding.end(),
                0.0
            ) / static_cast<double>(finite_crowding.size());
            double variance = 0.0;
            for (double value : finite_crowding) {
                variance += (value - mean) * (value - mean);
            }
            variance /= static_cast<double>(finite_crowding.size() - 1);
            convergence_history.push_back(variance);
        } else {
            convergence_history.push_back(
                std::numeric_limits<double>::infinity()
            );
        }
        if (convergence_history.size() >= 100) {
            converged = std::all_of(
                convergence_history.end() - 100,
                convergence_history.end(),
                [](double value) {
                    return value < 0.005;
                }
            );
        }
    }

    RunResult result;
    result.problem_id = problem.id();
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers =
        executor.work_receipt().distinct_participants;
    result.physical_fes = physical_fes;
    result.generations = generations;
    result.population_size = population_size;
    result.maximum_repair_distance_bin2 =
        config.maximum_repair_distance_bin2;
    result.penalty_coefficient = config.penalty_coefficient;
    result.repair_attempts = repair_attempts;
    result.repair_successes = repair_successes;
    result.repair_timeouts = repair_timeouts;
    result.repair_node_limit_hits = repair_node_limit_hits;
    result.repair_search_nodes = repair_nodes;
    result.repair_seconds = repair_seconds;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(run_started);
    result.algorithm_seconds = std::max(
        0.0,
        result.end_to_end_seconds
            - result.repair_seconds
            - result.evaluator_seconds
    );
    result.converged = converged;
    result.measured_land_availability =
        problem.measured_land_availability();
    result.front = extract_front(population);
    result.scientific_hash = front_hash(result.front);
    return result;
}

}  // namespace core99::t72
