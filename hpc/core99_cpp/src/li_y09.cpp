/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y09 pure-C++ multi-type wake/LCOE evaluator and GA
Paper/DOI: Li et al.; 10.1016/j.renene.2025.124386
Public source/data, missing information, paper/patent conflicts, deterministic
completion, semantic IDs, production backend, controlling contract and claim
boundary: include/core99/li_y09.hpp
HPC analysis: evidence/development/Y09_H0_H4_mathematical_hpc_analysis_20260731.md
Claim boundary: source-backed flexible academic reproduction, not author code
or numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/li_y09.hpp"

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

namespace core99::y09 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kGridSide = 10;
constexpr int kCandidates = kGridSide * kGridSide;
constexpr double kSiteSideM = 5000.0;
constexpr double kCellM = 500.0;
constexpr double kReferenceHeightM = 150.0;
constexpr double kReferenceSpeedMps = 12.0;
constexpr double kRoughnessExponent = 0.12;
constexpr double kIref = 0.12;
constexpr double kAirDensity = 1.225;
constexpr double kMaintenanceCompensation = 0.5;
constexpr double kTurbulenceFatigueEquivalent = 0.7;
constexpr double kPreventiveReduction = 0.8;
constexpr int kPreventiveCycles = 40;
constexpr double kPreventiveCostFraction = 0.0001;
constexpr double kReplacementCostFraction = 0.05;

struct Point {
    double x_m = 0.0;
    double y_m = 0.0;
};

struct TurbineSpec {
    int code = 0;
    double rated_power_mw = 0.0;
    double hub_height_m = 0.0;
    double rotor_diameter_m = 0.0;
};

constexpr TurbineSpec kFive{1, 5.0, 90.0, 126.0};
constexpr TurbineSpec kFifteen{2, 15.0, 150.0, 240.0};

constexpr std::array<double, 48> kFiveSpeeds{
    2.0,2.5,3.0,3.5,4.0,4.5,5.0,5.5,6.0,6.5,7.0,7.5,
    8.0,8.5,9.0,9.5,10.0,10.5,11.0,11.5,12.0,12.5,13.0,13.5,
    14.0,14.5,15.0,15.5,16.0,16.5,17.0,17.5,18.0,18.5,19.0,19.5,
    20.0,20.5,21.0,21.5,22.0,22.5,23.0,23.5,24.0,24.5,25.0,25.5
};
constexpr std::array<double, 48> kFiveCp{
    0.0,0.0,0.1780851,0.28907459,0.34902166,0.3847278,
    0.40605878,0.4202279,0.42882274,0.43387274,0.43622267,
    0.43684468,0.43657497,0.43651053,0.4365612,0.43651728,
    0.43590309,0.43467276,0.43322955,0.43003137,0.37655587,
    0.33328466,0.29700574,0.26420779,0.23839379,0.21459275,
    0.19382354,0.1756635,0.15970926,0.14561785,0.13287856,
    0.12130194,0.11219941,0.10311631,0.09545392,0.08813781,
    0.08186763,0.07585005,0.07071926,0.06557558,0.06148104,
    0.05755207,0.05413366,0.05097969,0.04806545,0.04536883,
    0.04287006,0.04055141
};
constexpr std::array<double, 48> kFiveCt{
    0.999,0.999,0.999,0.999,0.97373036,0.92826162,0.89210543,
    0.86100905,0.835423,0.81237673,0.79225789,0.77584769,
    0.7629228,0.76156073,0.76261984,0.76169723,0.75232027,
    0.74026851,0.72987175,0.70701647,0.54054532,0.45509459,
    0.39343381,0.34250785,0.30487242,0.27164979,0.24361964,
    0.21973831,0.19918151,0.18131868,0.16537679,0.15103727,
    0.13998636,0.1289037,0.11970413,0.11087113,0.10339901,
    0.09617888,0.09009926,0.08395078,0.0791188,0.07448356,
    0.07050731,0.06684119,0.06345518,0.06032267,0.05741999,
    0.05472609
};

struct CurvePoint {
    double speed_mps;
    double power_mw;
    double ct;
};

constexpr std::array<CurvePoint, 26> kFifteenCurve{{
    {3.0,0.070021377,0.819748943}, {3.5,0.3019937,0.801112031},
    {4.0,0.595088475,0.808268424}, {4.5,0.964887394,0.821910918},
    {5.0,1.429216889,0.823265981}, {5.5,2.0,0.832},
    {6.0,2.656263808,0.834932456}, {6.5,3.442669566,0.829011103},
    {7.0,4.339296326,0.806651158}, {7.5,5.33882324,0.805469658},
    {8.0,6.481116995,0.804571567}, {8.5,7.774570984,0.803949121},
    {9.0,9.229227024,0.803904895}, {9.5,10.85504374,0.803708734},
    {10.0,12.66125448,0.80345211}, {10.5,14.66065727,0.801777393},
    {10.6,14.99484635,0.768657554}, {10.8,14.99453984,0.667639697},
    {11.0,14.99426629,0.607277698}, {12.0,14.99417331,0.425965654},
    {13.0,14.99476256,0.32116631}, {14.0,14.99476121,0.2511023},
    {15.0,14.99475771,0.201415182}, {17.5,14.99482665,0.125653944},
    {20.0,14.99482754,0.08506697}, {25.0,14.99762687,0.045814967}
}};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

const TurbineSpec& spec(const int code) {
    if (code == 1) return kFive;
    if (code == 2) return kFifteen;
    throw std::invalid_argument("Y09 invalid turbine code");
}

template <std::size_t N>
double linear_interpolate(
    const double speed,
    const std::array<double, N>& x,
    const std::array<double, N>& y
) {
    if (speed <= x.front()) return y.front();
    if (speed >= x.back()) return y.back();
    const auto upper = std::upper_bound(x.begin(), x.end(), speed);
    const std::size_t right = static_cast<std::size_t>(upper - x.begin());
    const std::size_t left = right - 1U;
    const double fraction = (speed - x[left]) / (x[right] - x[left]);
    return y[left] + fraction * (y[right] - y[left]);
}

double five_power(const double speed) {
    if (speed < 3.0 || speed > 25.0) return 0.0;
    const double cp = linear_interpolate(speed, kFiveSpeeds, kFiveCp);
    const double area = std::numbers::pi * 63.0 * 63.0;
    return std::min(
        5.0,
        0.5 * kAirDensity * area * cp * speed * speed * speed / 1.0e6
    );
}

double five_ct(const double speed) {
    if (speed < 3.0 || speed > 25.0) return 0.0;
    return std::clamp(
        linear_interpolate(speed, kFiveSpeeds, kFiveCt), 1.0e-4, 0.999
    );
}

double fifteen_curve_value(const double speed, const bool thrust) {
    if (speed < 3.0 || speed > 25.0) return 0.0;
    const auto upper = std::lower_bound(
        kFifteenCurve.begin(), kFifteenCurve.end(), speed,
        [](const CurvePoint& value, const double key) {
            return value.speed_mps < key;
        }
    );
    if (upper == kFifteenCurve.begin()) return thrust ? upper->ct : upper->power_mw;
    if (upper == kFifteenCurve.end()) {
        return thrust ? kFifteenCurve.back().ct : kFifteenCurve.back().power_mw;
    }
    const auto& high = *upper;
    const auto& low = *(upper - 1);
    const double fraction = (speed - low.speed_mps)
        / (high.speed_mps - low.speed_mps);
    const double a = thrust ? low.ct : low.power_mw;
    const double b = thrust ? high.ct : high.power_mw;
    return a + fraction * (b - a);
}

double turbine_power(const int code, const double speed) {
    return code == 1 ? five_power(speed) : fifteen_curve_value(speed, false);
}

double turbine_ct(const int code, const double speed) {
    return code == 1 ? five_ct(speed)
                     : std::clamp(fifteen_curve_value(speed, true), 1.0e-4, 0.999);
}

bool code_allowed(const int code, const Composition composition) {
    if (code == 0) return true;
    if (composition == Composition::five_only) return code == 1;
    if (composition == Composition::fifteen_only) return code == 2;
    return code == 1 || code == 2;
}

std::vector<int> allowed_codes(const Composition composition) {
    if (composition == Composition::five_only) return {0, 1};
    if (composition == Composition::fifteen_only) return {0, 2};
    return {0, 1, 2};
}

double square(const double value) { return value * value; }

struct WakeTerm {
    double cross_center_m = 0.0;
    double hub_height_m = 0.0;
    double wake_width_m = 0.0;
    double additional_sigma_mps = 0.0;
};

struct Individual {
    std::vector<int> layout;
    Evaluation evaluation;
};

double objective(const Evaluation& value) {
    if (!value.feasible || !std::isfinite(value.lcoe_units_per_mw)) {
        return std::numeric_limits<double>::infinity();
    }
    return value.lcoe_units_per_mw;
}

std::uint64_t hash_mix(std::uint64_t state, const std::uint64_t value) {
    state ^= value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2);
    state ^= state >> 30;
    state *= 0xbf58476d1ce4e5b9ULL;
    state ^= state >> 27;
    return state;
}

std::uint64_t scientific_hash(
    const std::vector<int>& layout,
    const Evaluation& value,
    const std::uint64_t fes
) {
    std::uint64_t state = 0x5930395f57464c4fULL;
    for (const int code : layout) state = hash_mix(state, static_cast<std::uint64_t>(code));
    for (const double metric : {
             value.total_power_mw, value.construction_cost_units,
             value.maintenance_cost_units, value.lcoe_units_per_mw,
             value.fatigue_standard_deviation}) {
        state = hash_mix(state, std::bit_cast<std::uint64_t>(metric));
    }
    return hash_mix(state, fes);
}

std::size_t best_index(const std::vector<Individual>& population) {
    return static_cast<std::size_t>(std::min_element(
        population.begin(), population.end(),
        [](const Individual& first, const Individual& second) {
            return objective(first.evaluation) < objective(second.evaluation);
        }
    ) - population.begin());
}

std::vector<double> roulette_cumulative(const std::vector<Individual>& population) {
    std::vector<double> result(population.size(), 0.0);
    double total = 0.0;
    for (std::size_t index = 0; index < population.size(); ++index) {
        const double value = objective(population[index].evaluation);
        const double weight = std::isfinite(value) && value > 0.0 ? 1.0 / value : 0.0;
        total += weight;
        result[index] = total;
    }
    if (!(total > 0.0)) {
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = static_cast<double>(index + 1U);
        }
    }
    return result;
}

int select_parent(
    const std::vector<double>& cumulative,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const int child,
    const int draw
) {
    const double target = cumulative.back()
        * random.uniform(generation, 9091, child, draw);
    return static_cast<int>(std::lower_bound(
        cumulative.begin(), cumulative.end(), target
    ) - cumulative.begin());
}

void enforce_nonempty(
    std::vector<int>& layout,
    const Composition composition,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const int individual,
    const std::uint64_t phase
) {
    if (std::any_of(layout.begin(), layout.end(), [](const int code) { return code != 0; })) {
        return;
    }
    const int position = random.integer(0, kCandidates, generation, phase, individual);
    int code = 1;
    if (composition == Composition::fifteen_only) code = 2;
    else if (composition == Composition::multi_type) {
        code = 1 + random.integer(0, 2, generation, phase, individual, 1);
    }
    layout[static_cast<std::size_t>(position)] = code;
}

std::vector<int> random_layout(
    const Composition composition,
    const fode::CounterRng& random,
    const int individual
) {
    const auto allowed = allowed_codes(composition);
    std::vector<int> layout(kCandidates, 0);
    for (int cell = 0; cell < kCandidates; ++cell) {
        const int choice = random.integer(
            0, static_cast<int>(allowed.size()), 0, 9090, individual, cell
        );
        layout[static_cast<std::size_t>(cell)] = allowed[static_cast<std::size_t>(choice)];
    }
    enforce_nonempty(layout, composition, random, 0, individual, 90901);
    return layout;
}

MutationProbabilities population_mutation_probabilities(
    const std::vector<Individual>& population,
    const double total_rate,
    const Composition composition
) {
    double five = 0.0;
    double fifteen = 0.0;
    for (const auto& individual : population) {
        five += static_cast<double>(std::count(
            individual.layout.begin(), individual.layout.end(), 1
        ));
        fifteen += static_cast<double>(std::count(
            individual.layout.begin(), individual.layout.end(), 2
        ));
    }
    five /= static_cast<double>(population.size());
    fifteen /= static_cast<double>(population.size());
    return mutation_probabilities(five, fifteen, total_rate, composition);
}

void mutate_layout(
    std::vector<int>& layout,
    const MutationProbabilities probabilities,
    const Composition composition,
    const fode::CounterRng& random,
    const int generation,
    const int child
) {
    const auto allowed = allowed_codes(composition);
    for (int cell = 0; cell < kCandidates; ++cell) {
        const int current = layout[static_cast<std::size_t>(cell)];
        const double probability = current == 0 ? probabilities.zero
            : (current == 1 ? probabilities.five : probabilities.fifteen);
        if (random.uniform(generation, 9093, child, cell) >= probability) continue;
        std::vector<int> alternatives;
        for (const int code : allowed) if (code != current) alternatives.push_back(code);
        const int selected = random.integer(
            0, static_cast<int>(alternatives.size()),
            generation, 9093, child, cell, 1
        );
        layout[static_cast<std::size_t>(cell)] =
            alternatives[static_cast<std::size_t>(selected)];
    }
    enforce_nonempty(layout, composition, random, generation, child, 9094);
}

void evaluate_batch(
    const Problem& problem,
    const Scenario& scenario,
    std::vector<Individual>& population,
    fode::PersistentExecutor& executor,
    double& evaluator_seconds,
    std::uint64_t& physical_fes
) {
    const auto started = Clock::now();
    executor.parallel_for(0, static_cast<int>(population.size()), [&](const int index) {
        auto& value = population[static_cast<std::size_t>(index)];
        value.evaluation = problem.evaluate(value.layout, scenario);
    });
    evaluator_seconds += elapsed_seconds(started);
    physical_fes += static_cast<std::uint64_t>(population.size());
}

}  // namespace

struct Problem::Impl {
    std::array<Point, kCandidates> positions{};
    std::vector<Scenario> scenarios;

    Impl() {
        for (int row = 0; row < kGridSide; ++row) {
            for (int column = 0; column < kGridSide; ++column) {
                positions[static_cast<std::size_t>(row * kGridSide + column)] = {
                    (static_cast<double>(column) + 0.5) * kCellM,
                    (static_cast<double>(row) + 0.5) * kCellM
                };
            }
        }
        scenarios = {
            {"Y09_west_five_only", Composition::five_only, 0.0, 0.10, 1.0/3.0},
            {"Y09_west_multi", Composition::multi_type, 0.0, 0.10, 1.0/3.0},
            {"Y09_west_fifteen_only", Composition::fifteen_only, 0.0, 0.10, 1.0/3.0},
            {"Y09_northwest_multi", Composition::multi_type, 45.0, 0.10, 1.0/3.0},
            {"Y09_southwest_multi", Composition::multi_type, -45.0, 0.10, 1.0/3.0},
            {"Y09_fatigue_008_multi", Composition::multi_type, 0.0, 0.08, 1.0/3.0},
            {"Y09_fatigue_012_multi", Composition::multi_type, 0.0, 0.12, 1.0/3.0},
            {"Y09_fatigue_016_multi", Composition::multi_type, 0.0, 0.16, 1.0/3.0},
            {"Y09_cost_020_multi", Composition::multi_type, 0.0, 0.10, 0.20},
            {"Y09_cost_030_multi", Composition::multi_type, 0.0, 0.10, 0.30},
            {"Y09_cost_040_multi", Composition::multi_type, 0.0, 0.10, 0.40},
            {"Y09_cost_050_multi", Composition::multi_type, 0.0, 0.10, 0.50}
        };
    }

    [[nodiscard]] double ambient_speed(const double height_m) const noexcept {
        return kReferenceSpeedMps
            * std::pow(height_m / kReferenceHeightM, kRoughnessExponent);
    }

    [[nodiscard]] double ambient_ti(const double height_m) const noexcept {
        const double speed = ambient_speed(height_m);
        return kIref * (0.75 * speed + 5.6) / speed;
    }

    [[nodiscard]] Evaluation evaluate(
        const std::vector<int>& layout,
        const Scenario& scenario
    ) const {
        Evaluation result;
        if (layout.size() != kCandidates) return result;
        for (const int code : layout) if (!code_allowed(code, scenario.composition)) return result;

        std::vector<int> occupied;
        occupied.reserve(kCandidates);
        for (int index = 0; index < kCandidates; ++index) {
            const int code = layout[static_cast<std::size_t>(index)];
            if (code != 0) {
                occupied.push_back(index);
                result.five_mw_turbines += code == 1 ? 1 : 0;
                result.fifteen_mw_turbines += code == 2 ? 1 : 0;
            }
        }
        if (occupied.empty()) return result;

        const double angle = scenario.flow_direction_degrees
            * std::numbers::pi / 180.0;
        const double flow_x = std::cos(angle);
        const double flow_y = std::sin(angle);
        const double cross_x = -flow_y;
        const double cross_y = flow_x;
        std::stable_sort(occupied.begin(), occupied.end(), [&](const int first, const int second) {
            const auto& a = positions[static_cast<std::size_t>(first)];
            const auto& b = positions[static_cast<std::size_t>(second)];
            const double pa = a.x_m * flow_x + a.y_m * flow_y;
            const double pb = b.x_m * flow_x + b.y_m * flow_y;
            return pa == pb ? first < second : pa < pb;
        });

        result.turbines.reserve(occupied.size());
        for (const int grid_index : occupied) {
            const int code = layout[static_cast<std::size_t>(grid_index)];
            const auto& target_spec = spec(code);
            const Point target = positions[static_cast<std::size_t>(grid_index)];
            const double ambient_speed_value = ambient_speed(target_spec.hub_height_m);
            const double ambient_ti_value = ambient_ti(target_spec.hub_height_m);
            double deficit_sum = 0.0;
            std::vector<WakeTerm> wake_terms;
            wake_terms.reserve(result.turbines.size());

            for (const auto& source : result.turbines) {
                const auto& source_spec = spec(source.type_code);
                const Point source_point{source.x_m, source.y_m};
                const double dx = target.x_m - source_point.x_m;
                const double dy = target.y_m - source_point.y_m;
                const double downstream = dx * flow_x + dy * flow_y;
                if (downstream <= 0.0
                    || downstream >= 15.0 * source_spec.rotor_diameter_m) continue;
                const double cross = dx * cross_x + dy * cross_y;
                const double vertical = target_spec.hub_height_m - source_spec.hub_height_m;
                const double radius = std::hypot(cross, vertical);
                const double ct = turbine_ct(source.type_code, source.effective_speed_mps);
                const double ti = std::clamp(source.turbulence_intensity, 1.0e-3, 0.5);
                const double x_over_d = downstream / source_spec.rotor_diameter_m;
                const double kstar = 0.11 * std::pow(ct, 1.07) * std::pow(ti, 0.20);
                const double epsilon = 0.23 * std::pow(ct, -0.25) * std::pow(ti, 0.17);
                const double sigma = downstream * kstar
                    + source_spec.rotor_diameter_m * epsilon;
                const double b1 = 1.2 * std::pow(ct, -0.75) * std::pow(ti, 0.17);
                const double b2 = 0.28 * std::pow(ct, 0.6) * std::pow(ti, 0.2);
                const double b3 = 0.15 * std::pow(ct, -0.25) * std::pow(ti, -0.7);
                const double f = 1.0 / square(
                    b1 + b2 * x_over_d + b3 / std::sqrt(1.0 + x_over_d)
                );
                const double phi = std::exp(-square(radius) / (2.0 * square(sigma)));
                deficit_sum += source.effective_speed_mps * f * phi;

                const double b4 = 4.3 * std::pow(ct, -1.2);
                const double b5 = 0.5 * std::pow(ti, 0.1);
                const double b6 = 0.7 * std::pow(ct, -3.2) * std::pow(ti, -0.45);
                const double g = 1.0 /
                    (b4 + b5 * x_over_d + b6 / square(1.0 + x_over_d));
                const double half_d = 0.5 * source_spec.rotor_diameter_m;
                const double normalized = radius / source_spec.rotor_diameter_m;
                const double k1 = normalized <= 0.5
                    ? square(std::cos(0.5 * std::numbers::pi * (normalized - 0.5)))
                    : 1.0;
                const double k2 = normalized <= 0.5
                    ? square(std::cos(0.5 * std::numbers::pi * (normalized + 0.5)))
                    : 0.0;
                const double phi_turbulence =
                    k1 * std::exp(-square(radius - half_d) / (2.0 * square(sigma)))
                    + k2 * std::exp(-square(radius + half_d) / (2.0 * square(sigma)));
                const double k3 = target_spec.hub_height_m < source_spec.hub_height_m
                    ? target_spec.hub_height_m / source_spec.hub_height_m
                    : (target_spec.hub_height_m - source_spec.hub_height_m)
                        / (1.0 + 3.0 * (target_spec.hub_height_m
                            - source_spec.hub_height_m) / source_spec.hub_height_m)
                        + 1.0;
                const double k4 = normalized <= 0.5
                    ? square(std::cos(std::numbers::pi * normalized))
                        / (x_over_d + 0.01)
                    : 0.0;
                const double extra_sigma = std::max(
                    0.0,
                    source.effective_speed_mps * g * (k3 * phi_turbulence + k4)
                );
                wake_terms.push_back({
                    source_point.x_m * cross_x + source_point.y_m * cross_y,
                    source_spec.hub_height_m,
                    std::sqrt(8.0 * std::log(2.0)) * sigma,
                    extra_sigma
                });
            }

            const double effective_speed = std::clamp(
                ambient_speed_value - deficit_sum, 0.0, ambient_speed_value
            );
            double added_variance = 0.0;
            for (std::size_t first = 0; first < wake_terms.size(); ++first) {
                double corrected = wake_terms[first].additional_sigma_mps;
                for (std::size_t second = 0; second < wake_terms.size(); ++second) {
                    if (first == second) continue;
                    const double center_distance = std::hypot(
                        wake_terms[first].cross_center_m - wake_terms[second].cross_center_m,
                        wake_terms[first].hub_height_m - wake_terms[second].hub_height_m
                    );
                    const double first_radius = 0.5 * wake_terms[first].wake_width_m;
                    const double second_radius = 0.5 * wake_terms[second].wake_width_m;
                    const double inner = std::abs(first_radius - second_radius);
                    const double outer = first_radius + second_radius;
                    if (center_distance <= inner) {
                        corrected += 0.25 * wake_terms[second].additional_sigma_mps;
                    } else if (center_distance < outer && outer > inner) {
                        const double fraction = (outer - center_distance) / (outer - inner);
                        corrected -= 0.5 * std::max(
                            wake_terms[first].additional_sigma_mps,
                            wake_terms[second].additional_sigma_mps
                        ) * square(std::sin(std::numbers::pi * fraction));
                    }
                }
                added_variance += square(std::max(0.0, corrected));
            }
            const double ambient_sigma = ambient_speed_value * ambient_ti_value;
            const double turbulence = std::sqrt(square(ambient_sigma) + added_variance)
                / std::max(effective_speed, 1.0e-6);
            const double power = turbine_power(code, effective_speed);
            result.turbines.push_back({
                grid_index, code, target.x_m, target.y_m,
                effective_speed, turbulence, power, 0.0, 0.0
            });
            result.total_power_mw += power;
        }

        const int count = static_cast<int>(result.turbines.size());
        const double discount = 2.0 / 3.0
            + std::exp(-0.00174 * square(static_cast<double>(count))) / 3.0;
        result.construction_cost_units = (
            static_cast<double>(result.fifteen_mw_turbines)
            + scenario.five_to_fifteen_cost_ratio
                * static_cast<double>(result.five_mw_turbines)
        ) * discount;

        double fatigue_mean = 0.0;
        for (auto& turbine : result.turbines) {
            const auto& turbine_spec = spec(turbine.type_code);
            turbine.fatigue_coefficient = (
                turbine.power_mw / turbine_spec.rated_power_mw
                + kTurbulenceFatigueEquivalent * turbine.turbulence_intensity
            ) / (1.0 + kMaintenanceCompensation);
            fatigue_mean += turbine.fatigue_coefficient;
            const double argument = 1.0
                - (1.0 - kPreventiveReduction) * scenario.fatigue_threshold
                    * static_cast<double>(kPreventiveCycles)
                    / std::max(turbine.fatigue_coefficient, 1.0e-12);
            int replacements = 0;
            if (argument > 0.0 && argument < 1.0) {
                const int intervals = std::max(
                    1,
                    static_cast<int>(std::ceil(
                        std::log(argument) / std::log(kPreventiveReduction)
                    ))
                );
                replacements = kPreventiveCycles / intervals;
            }
            const double price = turbine.type_code == 1
                ? scenario.five_to_fifteen_cost_ratio : 1.0;
            turbine.maintenance_cost_units = price * (
                kPreventiveCostFraction * static_cast<double>(kPreventiveCycles)
                + kReplacementCostFraction * static_cast<double>(replacements)
            );
            result.maintenance_cost_units += turbine.maintenance_cost_units;
        }
        fatigue_mean /= static_cast<double>(count);
        double fatigue_variance = 0.0;
        for (const auto& turbine : result.turbines) {
            fatigue_variance += square(turbine.fatigue_coefficient - fatigue_mean);
        }
        result.fatigue_standard_deviation = std::sqrt(
            fatigue_variance / static_cast<double>(count)
        );
        result.average_maintenance_cost_units =
            result.maintenance_cost_units / static_cast<double>(count);
        if (result.total_power_mw > 0.0) {
            result.lcoe_units_per_mw = (
                result.construction_cost_units + result.maintenance_cost_units
            ) / result.total_power_mw;
            result.feasible = std::isfinite(result.lcoe_units_per_mw);
        }
        return result;
    }
};

Problem::Problem() : impl_(std::make_unique<Impl>()) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

int Problem::candidate_count() const noexcept { return kCandidates; }
double Problem::side_length_m() const noexcept { return kSiteSideM; }
int Problem::native_case_count() const noexcept {
    return static_cast<int>(impl_->scenarios.size());
}
const std::vector<Scenario>& Problem::native_scenarios() const noexcept {
    return impl_->scenarios;
}
Evaluation Problem::evaluate(
    const std::vector<int>& layout,
    const Scenario& scenario
) const {
    return impl_->evaluate(layout, scenario);
}
double Problem::ambient_speed_mps(const double height_m) const noexcept {
    return impl_->ambient_speed(height_m);
}
double Problem::ambient_turbulence(const double height_m) const noexcept {
    return impl_->ambient_ti(height_m);
}

MutationProbabilities mutation_probabilities(
    const double average_five_count,
    const double average_fifteen_count,
    const double total_mutation_rate,
    const Composition composition
) {
    if (!(total_mutation_rate >= 0.0)) {
        throw std::invalid_argument("Y09 negative mutation rate");
    }
    const double zeros = static_cast<double>(kCandidates)
        - average_five_count - average_fifteen_count;
    MutationProbabilities result;
    if (composition == Composition::multi_type) {
        if (zeros > 0.0) result.zero = kCandidates * total_mutation_rate / (3.0 * zeros);
        if (average_five_count > 0.0) {
            result.five = kCandidates * total_mutation_rate
                / (3.0 * average_five_count);
        }
        if (average_fifteen_count > 0.0) {
            result.fifteen = kCandidates * total_mutation_rate
                / (3.0 * average_fifteen_count);
        }
    } else {
        const double active = composition == Composition::five_only
            ? average_five_count : average_fifteen_count;
        if (zeros > 0.0) result.zero = kCandidates * total_mutation_rate / (2.0 * zeros);
        const double active_probability = active > 0.0
            ? kCandidates * total_mutation_rate / (2.0 * active) : 0.0;
        if (composition == Composition::five_only) result.five = active_probability;
        else result.fifteen = active_probability;
    }
    result.zero = std::clamp(result.zero, 0.0, 1.0);
    result.five = std::clamp(result.five, 0.0, 1.0);
    result.fifteen = std::clamp(result.fifteen, 0.0, 1.0);
    return result;
}

std::string to_string(const Composition value) {
    if (value == Composition::five_only) return "five_only";
    if (value == Composition::fifteen_only) return "fifteen_only";
    return "multi_type";
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.population < 2 || config.maximum_generations < 0
        || config.workers < 1 || config.no_improvement_generations < 1
        || config.crossover_rate < 0.0 || config.crossover_rate > 1.0
        || config.total_mutation_rate < 0.0) {
        throw std::invalid_argument("Y09 invalid run configuration");
    }
    const auto run_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng random(config.seed);
    std::vector<Individual> population(static_cast<std::size_t>(config.population));
    executor.parallel_for(0, config.population, [&](const int individual) {
        population[static_cast<std::size_t>(individual)].layout = random_layout(
            config.scenario.composition, random, individual
        );
    });
    double evaluator_seconds = 0.0;
    std::uint64_t physical_fes = 0;
    evaluate_batch(
        problem, config.scenario, population, executor,
        evaluator_seconds, physical_fes
    );
    Individual incumbent = population[best_index(population)];
    double incumbent_value = objective(incumbent.evaluation);
    int stagnation = 0;
    int generations = 0;
    std::string convergence_reason = "maximum_generations";
    MutationProbabilities probabilities = population_mutation_probabilities(
        population, config.total_mutation_rate, config.scenario.composition
    );

    for (int generation = 1; generation <= config.maximum_generations; ++generation) {
        const std::vector<double> cumulative = roulette_cumulative(population);
        probabilities = population_mutation_probabilities(
            population, config.total_mutation_rate, config.scenario.composition
        );
        std::vector<Individual> next(static_cast<std::size_t>(config.population));
        executor.parallel_for(0, config.population, [&](const int child) {
            const int first = select_parent(cumulative, random, generation, child, 0);
            const int second = select_parent(cumulative, random, generation, child, 1);
            auto& layout = next[static_cast<std::size_t>(child)].layout;
            layout = population[static_cast<std::size_t>(first)].layout;
            if (random.uniform(generation, 9092, child) < config.crossover_rate) {
                const int cut = random.integer(1, kCandidates, generation, 9092, child, 1);
                std::copy(
                    population[static_cast<std::size_t>(second)].layout.begin() + cut,
                    population[static_cast<std::size_t>(second)].layout.end(),
                    layout.begin() + cut
                );
            }
            mutate_layout(
                layout, probabilities, config.scenario.composition,
                random, generation, child
            );
        });
        evaluate_batch(
            problem, config.scenario, next, executor,
            evaluator_seconds, physical_fes
        );
        population = std::move(next);
        generations = generation;
        const Individual& current = population[best_index(population)];
        const double current_value = objective(current.evaluation);
        if (current_value + 1.0e-14 < incumbent_value) {
            incumbent = current;
            incumbent_value = current_value;
            stagnation = 0;
        } else {
            ++stagnation;
        }
        if (config.enable_convergence
            && stagnation >= config.no_improvement_generations) {
            convergence_reason = "declared_no_improvement_limit";
            break;
        }
    }

    const auto receipt = executor.work_receipt();
    RunResult result;
    result.case_id = config.scenario.case_id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    result.population = config.population;
    result.generations = generations;
    result.physical_fes = physical_fes;
    result.convergence_reason = convergence_reason;
    result.final_mutation_probabilities = probabilities;
    result.best_evaluation = incumbent.evaluation;
    result.best_layout = incumbent.layout;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(run_started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    result.scientific_hash = scientific_hash(
        result.best_layout, result.best_evaluation, result.physical_fes
    );
    return result;
}

}  // namespace core99::y09
