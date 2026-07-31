/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0368 pure-C++ seabed evaluator and real-coded GA
Paper/DOI: Liu et al.; 10.1016/j.enconman.2021.114610
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, production backend and claim boundary:
include/core99/liu_l0368.hpp
HPC analysis: evidence/development/L0368_H0_H4_mathematical_hpc_analysis_20260801.md
Controlling contract: shared/contracts/core99_l0368_liu_2021.json.
Claim boundary: flexible academic reconstruction, not author target code,
private Nanao arrays, exact MATLAB defaults/random trajectory or replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/liu_l0368.hpp"

#include "fode/executor.hpp"
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

namespace core99::l0368 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kMaximumTurbines = 25;
constexpr double kFarmSideM = 2000.0;
constexpr double kRotorDiameterM = 100.0;
constexpr double kMinimumSpacingM = 500.0;
constexpr double kHubHeightM = 80.0;
constexpr double kThrustCoefficient = 0.8888;
constexpr double kPowerCoefficient = 0.4;
constexpr double kCutInMps = 4.0;
constexpr double kCutOutMps = 20.0;
constexpr double kAirDensity = 1.225;
constexpr double kAmbientTurbulence = 0.08;
constexpr double kRatedPowerMwCompletion = 2.3;
constexpr double kTargetTerrainMeanM = 28.4394;
constexpr double kTargetTerrainStdM =
    (40.27 - 16.60) / 3.4641016151377545871;
constexpr double kPi = std::numbers::pi_v<double>;

double square(const double value) { return value * value; }

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t quantized(const double value, const double scale = 1.0e6) {
    const auto integer = static_cast<std::int64_t>(std::llround(value * scale));
    return std::bit_cast<std::uint64_t>(integer);
}

double raw_s4_depth(const Point& point) {
    return 28.4394 - 13.645
        * std::sin(-0.5 * kPi + point.x_m / 500.0)
        * std::sin(0.5 * kPi + point.y_m / 500.0);
}

double raw_s5_surface(const Point& point) {
    const double x = point.x_m / kFarmSideM;
    const double y = point.y_m / kFarmSideM;
    return 0.75 * std::sin(2.0 * kPi * x + 0.35)
        + 0.55 * std::cos(2.0 * kPi * y - 0.4)
        + 0.38 * std::sin(3.0 * kPi * x + 1.3 * kPi * y)
        - 0.27 * std::cos(4.0 * kPi * y - 1.7 * kPi * x)
        + 0.22 * std::sin(5.0 * kPi * x - 2.0 * kPi * y);
}

struct Normalization {
    double mean = 0.0;
    double standard_deviation = 1.0;
};

template <class Function>
Normalization sampled_normalization(Function function) {
    constexpr int samples = 201;
    double sum = 0.0;
    double squared_sum = 0.0;
    for (int row = 0; row < samples; ++row) {
        for (int column = 0; column < samples; ++column) {
            const Point point{
                kFarmSideM * static_cast<double>(column) / (samples - 1),
                kFarmSideM * static_cast<double>(row) / (samples - 1),
            };
            const double value = function(point);
            sum += value;
            squared_sum += value * value;
        }
    }
    const double count = static_cast<double>(samples * samples);
    const double mean = sum / count;
    return {mean, std::sqrt(std::max(0.0, squared_sum / count - mean * mean))};
}

std::vector<WindState> make_wind_states(const WindKind kind) {
    if (kind == WindKind::w1_single) return {{0.0, 12.0, 1.0}};
    std::vector<WindState> states;
    constexpr int directions = 36;
    if (kind == WindKind::w2_directions) {
        for (int direction = 0; direction < directions; ++direction) {
            states.push_back({10.0 * direction, 12.0, 1.0 / directions});
        }
        return states;
    }
    std::array<double, directions> direction_weights{};
    double weight_sum = 0.0;
    for (int direction = 0; direction < directions; ++direction) {
        const double angle = 10.0 * direction * kPi / 180.0;
        double weight = 1.0;
        if (kind == WindKind::w3_speed_direction) {
            weight = 1.0 + 0.82 * std::cos(angle - 0.10);
        } else {
            weight = std::exp(2.8 * std::cos(angle - 0.10))
                + 0.35 * std::exp(1.8 * std::cos(angle - 1.95))
                + 0.25 * std::exp(1.5 * std::cos(angle - 4.15));
        }
        direction_weights[static_cast<std::size_t>(direction)] = weight;
        weight_sum += weight;
    }
    if (kind == WindKind::w3_speed_direction) {
        constexpr std::array<double, 2> speeds{12.0, 16.0};
        constexpr std::array<double, 2> speed_weights{0.30, 0.70};
        for (int direction = 0; direction < directions; ++direction) {
            for (std::size_t speed = 0; speed < speeds.size(); ++speed) {
                states.push_back({
                    10.0 * direction,
                    speeds[speed],
                    direction_weights[static_cast<std::size_t>(direction)]
                        / weight_sum * speed_weights[speed],
                });
            }
        }
    } else {
        constexpr std::array<double, 5> speeds{4.0, 6.0, 8.0, 10.0, 12.0};
        constexpr std::array<double, 5> speed_weights{
            0.30, 0.35, 0.25, 0.08, 0.02
        };
        for (int direction = 0; direction < directions; ++direction) {
            for (std::size_t speed = 0; speed < speeds.size(); ++speed) {
                states.push_back({
                    10.0 * direction,
                    speeds[speed],
                    direction_weights[static_cast<std::size_t>(direction)]
                        / weight_sum * speed_weights[speed],
                });
            }
        }
    }
    return states;
}

double power_mw(const double speed_mps) {
    if (speed_mps < kCutInMps || speed_mps > kCutOutMps) return 0.0;
    const double area = 0.25 * kPi * square(kRotorDiameterM);
    return 0.5 * kAirDensity * area * speed_mps * speed_mps * speed_mps
        * kPowerCoefficient / 1.0e6;
}

struct FlowTurbine {
    Point point;
    double speed_mps = 0.0;
    double turbulence = kAmbientTurbulence;
};

double distance(const Point& first, const Point& second) {
    return std::hypot(first.x_m - second.x_m, first.y_m - second.y_m);
}

bool spaced_from(
    const std::vector<Point>& layout,
    const Point& candidate,
    const SpacingKind kind
) {
    for (const Point& point : layout) {
        const double dx = std::abs(point.x_m - candidate.x_m);
        const double dy = std::abs(point.y_m - candidate.y_m);
        const bool valid = kind == SpacingKind::euclidean_corrected
            ? std::hypot(dx, dy) >= kMinimumSpacingM - 1.0e-9
            : std::max(dx, dy) >= kMinimumSpacingM - 1.0e-9;
        if (!valid) return false;
    }
    return true;
}

void canonicalize(std::vector<Point>& layout) {
    std::sort(layout.begin(), layout.end(), [](const Point& left, const Point& right) {
        return left.x_m == right.x_m ? left.y_m < right.y_m : left.x_m < right.x_m;
    });
}

std::array<Point, kMaximumTurbines> lattice() {
    std::array<Point, kMaximumTurbines> result{};
    for (int index = 0; index < kMaximumTurbines; ++index) {
        result[static_cast<std::size_t>(index)] = {
            kMinimumSpacingM * static_cast<double>(index % 5),
            kMinimumSpacingM * static_cast<double>(index / 5),
        };
    }
    return result;
}

Point random_point(
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual,
    const std::uint64_t coordinate,
    const std::uint64_t draw
) {
    return {
        kFarmSideM * random.uniform(
            generation, phase, individual, coordinate, 2 * draw
        ),
        kFarmSideM * random.uniform(
            generation, phase, individual, coordinate, 2 * draw + 1
        ),
    };
}

std::vector<Point> make_random_layout(
    const fode::CounterRng& random,
    const std::uint64_t individual,
    const SpacingKind spacing
) {
    const int count = random.integer(
        1, kMaximumTurbines + 1, 0, 100, individual
    );
    const auto grid = lattice();
    std::array<int, kMaximumTurbines> order{};
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](const int first, const int second) {
        return random.uniform(0, 101, individual, first)
            < random.uniform(0, 101, individual, second);
    });
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(count));
    for (int slot = 0; slot < count; ++slot) {
        layout.push_back(grid[static_cast<std::size_t>(order[slot])]);
    }
    // Start from a count-preserving feasible lattice, then admit only local
    // continuous perturbations that keep the whole layout feasible.
    for (int slot = 0; slot < count; ++slot) {
        const Point original = layout[static_cast<std::size_t>(slot)];
        std::vector<Point> other = layout;
        other.erase(other.begin() + slot);
        for (int attempt = 0; attempt < 16; ++attempt) {
            const Point candidate{
                std::clamp(
                    original.x_m + 120.0 * random.normal(
                        0, 102, individual, slot, 2 * attempt
                    ),
                    0.0, kFarmSideM
                ),
                std::clamp(
                    original.y_m + 120.0 * random.normal(
                        0, 102, individual, slot, 2 * attempt + 1
                    ),
                    0.0, kFarmSideM
                ),
            };
            if (spaced_from(other, candidate, spacing)) {
                layout[static_cast<std::size_t>(slot)] = candidate;
                break;
            }
        }
    }
    canonicalize(layout);
    return layout;
}

std::vector<Point> repair(
    const std::vector<Point>& proposed,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const SpacingKind spacing
) {
    const std::size_t target_size = std::clamp<std::size_t>(
        proposed.size(), 1U, kMaximumTurbines
    );
    std::vector<Point> result;
    result.reserve(target_size);
    for (std::size_t slot = 0;
         slot < proposed.size() && slot < kMaximumTurbines;
         ++slot) {
        Point candidate{
            std::clamp(proposed[slot].x_m, 0.0, kFarmSideM),
            std::clamp(proposed[slot].y_m, 0.0, kFarmSideM),
        };
        bool accepted = spaced_from(result, candidate, spacing);
        for (int attempt = 0; !accepted && attempt < 64; ++attempt) {
            candidate = random_point(
                random, generation, 310, individual, slot, attempt
            );
            accepted = spaced_from(result, candidate, spacing);
        }
        if (accepted) result.push_back(candidate);
    }
    if (result.size() != target_size) {
        const auto grid = lattice();
        std::array<int, kMaximumTurbines> order{};
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](const int first, const int second) {
            return random.uniform(generation, 311, individual, first)
                < random.uniform(generation, 311, individual, second);
        });
        result.clear();
        for (std::size_t slot = 0; slot < target_size; ++slot) {
            result.push_back(grid[static_cast<std::size_t>(order[slot])]);
        }
    }
    canonicalize(result);
    return result;
}

std::vector<Point> make_child(
    const std::vector<Point>& first,
    const std::vector<Point>& second,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t child,
    const double crossover_fraction,
    const SpacingKind spacing
) {
    std::vector<Point> proposed = first;
    if (random.uniform(generation, 300, child) < crossover_fraction) {
        const std::size_t target = static_cast<std::size_t>(std::clamp(
            static_cast<int>(std::llround(0.5 * (
                static_cast<double>(first.size())
                + static_cast<double>(second.size())
            ))), 1, kMaximumTurbines
        ));
        proposed.resize(target);
        for (std::size_t slot = 0; slot < target; ++slot) {
            const bool take_second = slot < second.size()
                && random.uniform(generation, 301, child, slot) < 0.5;
            if (take_second) proposed[slot] = second[slot];
            else if (slot >= first.size()) {
                proposed[slot] = random_point(
                    random, generation, 302, child, slot, 0
                );
            }
        }
    }
    const double progress = std::min(1.0, static_cast<double>(generation) / 500.0);
    const double sigma = 180.0 * (1.0 - 0.8 * progress);
    if (random.uniform(generation, 303, child) < 0.20) {
        if (random.uniform(generation, 304, child) < 0.5
            && proposed.size() > 1U) {
            const int removed = random.integer(
                0, static_cast<int>(proposed.size()), generation, 305, child
            );
            proposed.erase(proposed.begin() + removed);
        } else if (proposed.size() < kMaximumTurbines) {
            proposed.push_back(random_point(
                random, generation, 306, child, proposed.size(), 0
            ));
        }
    }
    if (!proposed.empty()) {
        const int slot = random.integer(
            0, static_cast<int>(proposed.size()), generation, 307, child
        );
        proposed[static_cast<std::size_t>(slot)].x_m += sigma * random.normal(
            generation, 308, child, slot, 0
        );
        proposed[static_cast<std::size_t>(slot)].y_m += sigma * random.normal(
            generation, 309, child, slot, 1
        );
    }
    return repair(proposed, random, generation, child, spacing);
}

}  // namespace

struct Problem::Impl {
    Scenario scenario;
    std::vector<WindState> winds;
    Normalization s4;
    Normalization s5;

    explicit Impl(Scenario input)
        : scenario(std::move(input)),
          winds(make_wind_states(scenario.wind)),
          s4(sampled_normalization(raw_s4_depth)),
          s5(sampled_normalization(raw_s5_surface)) {}

    [[nodiscard]] double depth(const Point& point) const noexcept {
        switch (scenario.terrain) {
            case TerrainKind::s1_zero:
                return 0.0;
            case TerrainKind::s2_slope:
                return 16.60 + (40.27 - 16.60) * point.x_m / kFarmSideM;
            case TerrainKind::s3_flat:
                return 28.44;
            case TerrainKind::s4_wave:
                return kTargetTerrainMeanM + kTargetTerrainStdM
                    * (raw_s4_depth(point) - s4.mean)
                    / std::max(s4.standard_deviation, 1.0e-12);
            case TerrainKind::s5_nanao_proxy:
                return kTargetTerrainMeanM + kTargetTerrainStdM
                    * (raw_s5_surface(point) - s5.mean)
                    / std::max(s5.standard_deviation, 1.0e-12);
        }
        return 0.0;
    }

    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Point>& layout
    ) const {
        Evaluation result;
        result.turbine_count = static_cast<int>(layout.size());
        if (layout.empty() || layout.size() > kMaximumTurbines) return result;
        result.minimum_distance_m = std::numeric_limits<double>::infinity();
        for (std::size_t first = 0; first < layout.size(); ++first) {
            if (layout[first].x_m < 0.0 || layout[first].x_m > kFarmSideM
                || layout[first].y_m < 0.0 || layout[first].y_m > kFarmSideM) {
                return result;
            }
            result.mean_water_depth_m += depth(layout[first]);
            for (std::size_t second = first + 1; second < layout.size(); ++second) {
                const double physical_distance = distance(layout[first], layout[second]);
                result.minimum_distance_m = std::min(
                    result.minimum_distance_m, physical_distance
                );
                const double dx = std::abs(layout[first].x_m - layout[second].x_m);
                const double dy = std::abs(layout[first].y_m - layout[second].y_m);
                const bool valid = scenario.spacing == SpacingKind::euclidean_corrected
                    ? physical_distance >= kMinimumSpacingM - 1.0e-7
                    : std::max(dx, dy) >= kMinimumSpacingM - 1.0e-7;
                if (!valid) return result;
            }
        }
        if (layout.size() == 1U) result.minimum_distance_m = 0.0;
        result.mean_water_depth_m /= static_cast<double>(layout.size());

        for (const WindState& wind : winds) {
            const double angle = wind.direction_degrees * kPi / 180.0;
            const double flow_x = std::cos(angle);
            const double flow_y = std::sin(angle);
            const double cross_x = -flow_y;
            const double cross_y = flow_x;
            std::vector<int> order(layout.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
                const double first = layout[static_cast<std::size_t>(left)].x_m * flow_x
                    + layout[static_cast<std::size_t>(left)].y_m * flow_y;
                const double second = layout[static_cast<std::size_t>(right)].x_m * flow_x
                    + layout[static_cast<std::size_t>(right)].y_m * flow_y;
                return first == second ? left < right : first < second;
            });
            std::vector<FlowTurbine> evaluated;
            evaluated.reserve(layout.size());
            double farm_power = 0.0;
            for (const int index : order) {
                const Point target = layout[static_cast<std::size_t>(index)];
                double deficit_sum = 0.0;
                double added_variance = 0.0;
                for (const FlowTurbine& source : evaluated) {
                    const double dx = target.x_m - source.point.x_m;
                    const double dy = target.y_m - source.point.y_m;
                    const double downstream = dx * flow_x + dy * flow_y;
                    if (downstream <= 0.0) continue;
                    const double crosswind = std::abs(dx * cross_x + dy * cross_y);
                    const double x_over_d = downstream / kRotorDiameterM;
                    const double ti = std::clamp(source.turbulence, 1.0e-3, 0.5);
                    const double kstar = 0.11 * std::pow(kThrustCoefficient, 1.07)
                        * std::pow(ti, 0.20);
                    const double epsilon = 0.23
                        * std::pow(kThrustCoefficient, -0.25)
                        * std::pow(ti, 0.17);
                    const double sigma = downstream * kstar
                        + kRotorDiameterM * epsilon;
                    const double b1 = 1.2
                        * std::pow(kThrustCoefficient, -0.75)
                        * std::pow(ti, 0.17);
                    const double b2 = 0.28
                        * std::pow(kThrustCoefficient, 0.6)
                        * std::pow(ti, 0.2);
                    const double b3 = 0.15
                        * std::pow(kThrustCoefficient, -0.25)
                        * std::pow(ti, -0.7);
                    const double f = 1.0 / square(
                        b1 + b2 * x_over_d + b3 / std::sqrt(1.0 + x_over_d)
                    );
                    const double gaussian = std::exp(
                        -square(crosswind) / (2.0 * square(sigma))
                    );
                    deficit_sum += source.speed_mps * f * gaussian;
                    const double b4 = 4.3 * std::pow(kThrustCoefficient, -1.2);
                    const double b5 = 0.5 * std::pow(ti, 0.1);
                    const double b6 = 0.7 * std::pow(kThrustCoefficient, -3.2)
                        * std::pow(ti, -0.45);
                    const double g = 1.0 /
                        (b4 + b5 * x_over_d + b6 / square(1.0 + x_over_d));
                    const double normalized = crosswind / kRotorDiameterM;
                    const double k1 = normalized <= 0.5
                        ? square(std::cos(0.5 * kPi * (normalized - 0.5))) : 1.0;
                    const double k2 = normalized <= 0.5
                        ? square(std::cos(0.5 * kPi * (normalized + 0.5))) : 0.0;
                    const double half_d = 0.5 * kRotorDiameterM;
                    const double turbulence_shape = k1 * std::exp(
                        -square(crosswind - half_d) / (2.0 * square(sigma))
                    ) + k2 * std::exp(
                        -square(crosswind + half_d) / (2.0 * square(sigma))
                    );
                    const double near_wake = normalized <= 0.5
                        ? square(std::cos(kPi * normalized)) / (x_over_d + 0.01)
                        : 0.0;
                    added_variance += square(std::max(
                        0.0, source.speed_mps * g * (turbulence_shape + near_wake)
                    ));
                }
                const double speed = std::clamp(
                    wind.speed_mps - deficit_sum, 0.0, wind.speed_mps
                );
                const double ambient_sigma = wind.speed_mps * kAmbientTurbulence;
                const double turbulence = std::sqrt(
                    square(ambient_sigma) + added_variance
                ) / std::max(speed, 1.0e-9);
                evaluated.push_back({target, speed, turbulence});
                farm_power += power_mw(speed);
            }
            result.expected_power_mw += wind.probability * farm_power;
            result.no_wake_power_mw += wind.probability
                * static_cast<double>(layout.size()) * power_mw(wind.speed_mps);
        }
        if (!(result.expected_power_mw > 0.0)) return result;
        result.efficiency_percent = 100.0 * result.expected_power_mw
            / std::max(result.no_wake_power_mw, 1.0e-12);

        const double count = static_cast<double>(layout.size());
        result.wind_turbine_cost_gbp = (
            518.0 * kRatedPowerMwCompletion + 929.0
        ) * kRatedPowerMwCompletion * 1000.0 * count;
        for (const Point& point : layout) {
            const double water_depth = std::max(0.0, depth(point));
            const double diameter = std::max(
                4.0, 4.32e-4 * water_depth * water_depth
                    + 6.13e-2 * water_depth + 3.954
            );
            const double thickness = 0.015 * water_depth;
            const double length = 2.0 * water_depth + 20.0;
            result.support_structure_cost_gbp += kPi * diameter * thickness
                * length * 1992.5848;
        }
        const double array_cable = 200000.0 * 0.5 * count;
        const double transport_cable = 375000.0 * 20.0 * 2.0;
        const double transformer = (80000.0 + 100000.0)
            * kRatedPowerMwCompletion * count;
        const double port = 130000.0 * count;
        result.cable_substation_port_cost_gbp =
            array_cable + transport_cable + transformer + port;
        const double direct = result.wind_turbine_cost_gbp
            + result.support_structure_cost_gbp
            + result.cable_substation_port_cost_gbp;
        result.initial_capital_cost_gbp = direct / (1.0 - 0.043 - 0.174);
        result.capital_power_proxy_gbp_per_mw =
            result.initial_capital_cost_gbp / result.expected_power_mw;
        result.feasible = std::isfinite(result.capital_power_proxy_gbp_per_mw);
        return result;
    }
};

Problem::Problem(Scenario scenario)
    : impl_(std::make_unique<Impl>(std::move(scenario))) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
const Scenario& Problem::scenario() const noexcept { return impl_->scenario; }
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return impl_->winds;
}
double Problem::water_depth_m(const Point& point) const noexcept {
    return impl_->depth(point);
}
Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    return impl_->evaluate_layout(layout);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.population < 8 || config.generations < 0
        || config.elite_count < 1 || config.elite_count >= config.population
        || !(config.crossover_fraction >= 0.0
             && config.crossover_fraction <= 1.0)) {
        throw std::invalid_argument("L0368 invalid run configuration");
    }
    struct Individual {
        std::vector<Point> layout;
        Evaluation evaluation;
    };
    auto better = [](const Individual& left, const Individual& right) {
        if (left.evaluation.feasible != right.evaluation.feasible) {
            return left.evaluation.feasible;
        }
        if (left.evaluation.capital_power_proxy_gbp_per_mw
            != right.evaluation.capital_power_proxy_gbp_per_mw) {
            return left.evaluation.capital_power_proxy_gbp_per_mw
                < right.evaluation.capital_power_proxy_gbp_per_mw;
        }
        if (left.layout.size() != right.layout.size()) {
            return left.layout.size() < right.layout.size();
        }
        for (std::size_t index = 0; index < left.layout.size(); ++index) {
            if (left.layout[index].x_m != right.layout[index].x_m) {
                return left.layout[index].x_m < right.layout[index].x_m;
            }
            if (left.layout[index].y_m != right.layout[index].y_m) {
                return left.layout[index].y_m < right.layout[index].y_m;
            }
        }
        return false;
    };

    const auto started = Clock::now();
    const fode::CounterRng random(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::vector<Individual> population(static_cast<std::size_t>(config.population));
    executor.parallel_for(0, config.population, [&](const int individual) {
        population[static_cast<std::size_t>(individual)].layout = make_random_layout(
            random, static_cast<std::uint64_t>(individual),
            problem.impl_->scenario.spacing
        );
    });
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&](std::vector<Individual>& values) {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, static_cast<int>(values.size()), [&](const int index) {
            values[static_cast<std::size_t>(index)].evaluation =
                problem.impl_->evaluate_layout(
                    values[static_cast<std::size_t>(index)].layout
                );
        });
        evaluator_seconds += elapsed_seconds(evaluation_started);
    };
    evaluate_population(population);
    std::stable_sort(population.begin(), population.end(), better);
    std::uint64_t physical_fes = static_cast<std::uint64_t>(config.population);

    auto rank_parent = [&](const std::uint64_t generation,
                           const std::uint64_t child,
                           const std::uint64_t draw) {
        const int total_weight = config.population * (config.population + 1) / 2;
        int target = random.integer(
            0, total_weight, generation, 400, child, draw
        );
        for (int rank = 0; rank < config.population; ++rank) {
            const int weight = config.population - rank;
            if (target < weight) return rank;
            target -= weight;
        }
        return config.population - 1;
    };

    for (int generation = 1; generation <= config.generations; ++generation) {
        std::vector<Individual> next(static_cast<std::size_t>(config.population));
        for (int elite = 0; elite < config.elite_count; ++elite) {
            next[static_cast<std::size_t>(elite)] =
                population[static_cast<std::size_t>(elite)];
        }
        executor.parallel_for(config.elite_count, config.population, [&](const int child) {
            const int first = rank_parent(generation, child, 0);
            int second = rank_parent(generation, child, 1);
            if (second == first) second = (second + 1) % config.population;
            next[static_cast<std::size_t>(child)].layout = make_child(
                population[static_cast<std::size_t>(first)].layout,
                population[static_cast<std::size_t>(second)].layout,
                random, generation, child, config.crossover_fraction,
                problem.impl_->scenario.spacing
            );
        });
        evaluate_population(next);
        physical_fes += static_cast<std::uint64_t>(config.population);
        std::stable_sort(next.begin(), next.end(), better);
        population = std::move(next);
    }

    RunResult result;
    result.case_id = problem.impl_->scenario.case_id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = config.population;
    result.generations = config.generations;
    result.physical_fes = physical_fes;
    result.best_evaluation = population.front().evaluation;
    result.best_layout = population.front().layout;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.best_layout.size()));
    for (const Point& point : result.best_layout) {
        hash = mix_hash(hash, quantized(point.x_m));
        hash = mix_hash(hash, quantized(point.y_m));
    }
    hash = mix_hash(hash, quantized(
        result.best_evaluation.capital_power_proxy_gbp_per_mw, 1.0e-2
    ));
    hash = mix_hash(hash, quantized(result.best_evaluation.expected_power_mw));
    result.scientific_hash = hash;
    return result;
}

std::vector<Scenario> paper_scenarios() {
    constexpr std::array<int, 20> counts{
        18, 15, 19, 16, 17,
        23, 17, 21, 12, 16,
        18, 16, 17, 12, 15,
        23, 20, 23, 15, 19,
    };
    constexpr std::array<double, 20> coe{
        2.46, 3.21, 3.21, 3.11, 3.22,
        2.57, 3.39, 3.39, 3.23, 3.31,
        2.66, 3.13, 3.12, 2.98, 3.04,
        5.94, 10.14, 10.04, 9.79, 9.94,
    };
    constexpr std::array<double, 20> power{
        56.82, 48.32, 58.93, 50.72, 53.31,
        64.61, 50.01, 60.40, 36.87, 47.72,
        91.85, 82.26, 87.51, 63.22, 78.23,
        12.97, 11.61, 13.04, 8.00, 11.16,
    };
    constexpr std::array<double, 20> efficiency{
        94.90, 96.86, 93.21, 95.30, 94.26,
        84.46, 88.45, 86.47, 92.37, 89.66,
        78.54, 79.14, 79.24, 81.10, 80.28,
        85.51, 88.03, 85.98, 90.88, 89.12,
    };
    constexpr std::array<TerrainKind, 5> terrains{
        TerrainKind::s1_zero, TerrainKind::s2_slope, TerrainKind::s3_flat,
        TerrainKind::s4_wave, TerrainKind::s5_nanao_proxy,
    };
    constexpr std::array<WindKind, 4> winds{
        WindKind::w1_single, WindKind::w2_directions,
        WindKind::w3_speed_direction, WindKind::w4_nanao_proxy,
    };
    std::vector<Scenario> result;
    result.reserve(20);
    for (int wind = 0; wind < 4; ++wind) {
        for (int terrain = 0; terrain < 5; ++terrain) {
            const int index = 5 * wind + terrain;
            result.push_back({
                "L0368_S" + std::to_string(terrain + 1)
                    + "W" + std::to_string(wind + 1),
                terrains[static_cast<std::size_t>(terrain)],
                winds[static_cast<std::size_t>(wind)],
                SpacingKind::euclidean_corrected,
                counts[static_cast<std::size_t>(index)],
                coe[static_cast<std::size_t>(index)],
                power[static_cast<std::size_t>(index)],
                efficiency[static_cast<std::size_t>(index)],
            });
        }
    }
    return result;
}

std::string to_string(const TerrainKind value) {
    switch (value) {
        case TerrainKind::s1_zero: return "s1_zero";
        case TerrainKind::s2_slope: return "s2_slope";
        case TerrainKind::s3_flat: return "s3_flat";
        case TerrainKind::s4_wave: return "s4_wave";
        case TerrainKind::s5_nanao_proxy: return "s5_nanao_proxy";
    }
    throw std::logic_error("L0368 terrain enum");
}

std::string to_string(const WindKind value) {
    switch (value) {
        case WindKind::w1_single: return "w1_single";
        case WindKind::w2_directions: return "w2_directions";
        case WindKind::w3_speed_direction: return "w3_speed_direction";
        case WindKind::w4_nanao_proxy: return "w4_nanao_proxy";
    }
    throw std::logic_error("L0368 wind enum");
}

std::string to_string(const SpacingKind value) {
    return value == SpacingKind::euclidean_corrected
        ? "euclidean_corrected" : "paper_linf_sensitivity";
}

}  // namespace core99::l0368
