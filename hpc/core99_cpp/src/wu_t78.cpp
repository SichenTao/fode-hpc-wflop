/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T78 pure-C++ wake/power/noise evaluator and all-core PSO
Paper DOI: 10.1016/j.apenergy.2020.114896
Public source, missing information, conflicts, declared completions, semantic
IDs, HPC design, controlling contract and claim boundary:
include/core99/wu_t78.hpp
Claim boundary: academic flexible reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wu_t78.hpp"

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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t78 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kTurbines = 80;
constexpr int kDimensions = 2 * kTurbines;
constexpr int kObservationAxis = 5;
constexpr int kObservationPoints = kObservationAxis * kObservationAxis;
constexpr double kFarmWidthM = 10401.0;
constexpr double kFarmLengthM = 12864.0;
constexpr double kObservationCenterX = 5750.0;
constexpr double kObservationCenterY = 5750.0;
constexpr double kObservationSideM = 500.0;
constexpr double kRotorDiameterM = 178.3;
constexpr double kRotorRadiusM = 0.5 * kRotorDiameterM;
constexpr double kMinimumSpacingM = 4.0 * kRotorDiameterM;
constexpr double kWakeDecay = 0.07;
constexpr double kThrustCoefficient = 0.8;
constexpr double kCutInMps = 4.0;
constexpr double kRatedMps = 11.4;
constexpr double kCutOutMps = 25.0;
constexpr double kRatedPowerMw = 10.0;
constexpr double kAnnualHours = 8760.0;
constexpr double kLambdaOpt = 7.5;
constexpr double kRatedRotorRpm = 9.6;
constexpr double kStrictLimitDba = 45.0;
constexpr double kEconomicUpperLimitDba = 50.0;
constexpr double kCompensationGwhPerDba = 0.01;
constexpr double kConstraintPenalty = 1.0e6;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct RawMetrics {
    double annual_energy_gwh = 0.0;
    double maximum_l10_dba = 0.0;
    double minimum_spacing_m = 0.0;
    double spacing_violation_m = 0.0;
};

struct Particle {
    std::vector<double> position;
    std::vector<double> velocity;
    std::vector<double> personal_best;
    Evaluation evaluation;
    double fitness = std::numeric_limits<double>::infinity();
    double personal_best_fitness = std::numeric_limits<double>::infinity();
};

// Deterministic digitization of the 80 turquoise markers in target Figure 4.
// The paper publishes no coordinate array. The plotted axes and Table 1 farm
// bounds control the transform; small boundary overshoots are clamped.
constexpr std::array<Point, kTurbines> kReferenceLayout{{
    {3396.195, 18.677}, {4299.183, 39.162}, {5195.262, 95.822},
    {2556.780, 301.494}, {6032.090, 353.155}, {6925.645, 407.134},
    {7803.300, 559.790}, {1853.092, 845.134}, {8618.630, 948.220},
    {1310.262, 1569.092}, {9180.343, 1645.586}, {3083.460, 1698.647},
    {4533.875, 1957.782}, {819.423, 2297.786}, {9453.204, 2472.552},
    {7450.407, 2784.580}, {6028.970, 2859.723}, {3160.988, 2886.727},
    {313.845, 3018.722}, {4627.092, 3121.220}, {9510.829, 3352.063},
    {1814.343, 3636.825}, {21.640, 3871.918}, {6227.618, 4028.413},
    {3332.589, 4075.863}, {9374.364, 4231.224}, {4843.703, 4304.137},
    {195.751, 4746.310}, {2029.929, 4823.900}, {8008.445, 5082.197},
    {9062.215, 5108.241}, {6537.178, 5188.513}, {3592.877, 5238.984},
    {5136.174, 5441.442}, {685.794, 5494.692}, {8789.074, 5934.809},
    {2322.120, 5986.262}, {1269.223, 6169.962}, {6945.909, 6266.023},
    {3949.032, 6372.857}, {5523.245, 6555.406}, {9159.600, 6762.210},
    {1620.637, 6998.893}, {7470.677, 7333.514}, {9742.033, 7435.745},
    {4375.224, 7457.900}, {6012.226, 7645.018}, {1347.496, 7825.461},
    {3081.812, 8082.657}, {10213.242, 8201.334}, {4882.005, 8539.127},
    {6591.806, 8669.450}, {1036.654, 8677.875}, {10401.0, 9057.227},
    {3595.508, 9144.945}, {8789.731, 9251.421}, {896.338, 9576.857},
    {5483.515, 9577.547}, {7257.944, 9650.107}, {10095.694, 9886.128},
    {2421.851, 9969.323}, {4182.185, 10170.568}, {957.601, 10454.417},
    {6150.495, 10531.148}, {8009.575, 10560.938}, {9587.674, 10635.708},
    {3004.552, 11000.321}, {4843.703, 11159.811}, {1249.368, 11285.312},
    {9086.227, 11359.996}, {6886.638, 11437.866}, {1816.003, 11998.374},
    {8538.329, 12077.873}, {2615.982, 12387.579}, {3512.532, 12516.987},
    {4396.588, 12553.506}, {7823.777, 12619.563}, {5230.876, 12829.169},
    {6087.816, 12864.0}, {6985.432, 12864.0},
}};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double overlap_area(
    const double first_radius,
    const double second_radius,
    const double distance
) {
    if (distance >= first_radius + second_radius) return 0.0;
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first = std::acos(std::clamp(
        (distance * distance + first_radius * first_radius
            - second_radius * second_radius)
            / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second = std::acos(std::clamp(
        (distance * distance + second_radius * second_radius
            - first_radius * first_radius)
            / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double radicand = std::max(
        0.0,
        (-distance + first_radius + second_radius)
            * (distance + first_radius - second_radius)
            * (distance - first_radius + second_radius)
            * (distance + first_radius + second_radius)
    );
    return first_radius * first_radius * first
        + second_radius * second_radius * second
        - 0.5 * std::sqrt(radicand);
}

std::array<Point, kTurbines> decode(const std::vector<double>& decision) {
    if (decision.size() != static_cast<std::size_t>(kDimensions)) {
        throw std::invalid_argument("T78 decision must contain 160 coordinates");
    }
    std::array<Point, kTurbines> result{};
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        result[static_cast<std::size_t>(turbine)] = {
            decision[static_cast<std::size_t>(2 * turbine)],
            decision[static_cast<std::size_t>(2 * turbine + 1)],
        };
    }
    return result;
}

double power_mw(const double wind_mps) {
    if (wind_mps < kCutInMps || wind_mps >= kCutOutMps) return 0.0;
    if (wind_mps >= kRatedMps) return kRatedPowerMw;
    const double numerator = wind_mps * wind_mps * wind_mps
        - kCutInMps * kCutInMps * kCutInMps;
    const double denominator = kRatedMps * kRatedMps * kRatedMps
        - kCutInMps * kCutInMps * kCutInMps;
    return kRatedPowerMw * std::clamp(numerator / denominator, 0.0, 1.0);
}

double a_weighted_source_dba(const double wind_mps) {
    if (wind_mps < 1.0) return 45.0;
    const double omega = std::min(
        kLambdaOpt * wind_mps / kRotorRadiusM,
        kRatedRotorRpm * 2.0 * std::numbers::pi / 60.0
    );
    const double tip_speed = std::max(1.0, omega * kRotorRadiusM);
    // Hubbard's two retained mechanisms scale primarily with the fourth and
    // fifth powers of blade speed. The unavailable Ka/Kb and blade constants
    // are represented by a single additive calibration, never by a fitted
    // layout-dependent term.
    const double inflow = 96.0 + 40.0 * std::log10(tip_speed / 70.0);
    const double boundary = 94.0 + 50.0 * std::log10(tip_speed / 70.0);
    const double broadband = 10.0 * std::log10(
        std::pow(10.0, inflow / 10.0)
        + std::pow(10.0, boundary / 10.0)
    );
    constexpr std::array<double, 8> spectrum{
        -15.0, -10.0, -6.0, -3.0, -2.0, -4.0, -8.0, -14.0,
    };
    constexpr std::array<double, 8> a_weight{
        -26.2, -16.1, -8.6, -3.2, 0.0, 1.2, 1.0, -1.1,
    };
    double intensity = 0.0;
    for (std::size_t band = 0; band < spectrum.size(); ++band) {
        intensity += std::pow(
            10.0,
            (broadband + spectrum[band] + a_weight[band]) / 10.0
        );
    }
    return 10.0 * std::log10(std::max(intensity, 1.0e-300));
}

std::array<double, kTurbines> effective_winds(
    const std::array<Point, kTurbines>& layout,
    const WindState& state
) {
    const double angle = state.direction_deg * std::numbers::pi / 180.0;
    const double along_x = std::cos(angle);
    const double along_y = std::sin(angle);
    const double cross_x = -along_y;
    const double cross_y = along_x;
    std::array<double, kTurbines> along{};
    std::array<double, kTurbines> cross{};
    std::array<double, kTurbines> result{};
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        const auto& point = layout[static_cast<std::size_t>(turbine)];
        along[static_cast<std::size_t>(turbine)] =
            point.x * along_x + point.y * along_y;
        cross[static_cast<std::size_t>(turbine)] =
            point.x * cross_x + point.y * cross_y;
    }
    const double rotor_area = std::numbers::pi
        * kRotorRadiusM * kRotorRadiusM;
    const double induction = 1.0 - std::sqrt(1.0 - kThrustCoefficient);
    for (int downstream = 0; downstream < kTurbines; ++downstream) {
        double summed_deficit = 0.0;
        for (int upstream = 0; upstream < kTurbines; ++upstream) {
            const double distance = along[static_cast<std::size_t>(downstream)]
                - along[static_cast<std::size_t>(upstream)];
            if (distance <= 0.0) continue;
            const double wake_radius = kRotorRadiusM + kWakeDecay * distance;
            const double separation = std::abs(
                cross[static_cast<std::size_t>(downstream)]
                - cross[static_cast<std::size_t>(upstream)]
            );
            const double overlap = overlap_area(
                wake_radius, kRotorRadiusM, separation
            );
            if (overlap <= 0.0) continue;
            const double full_deficit = induction * std::pow(
                kRotorRadiusM / wake_radius, 2.0
            );
            summed_deficit += full_deficit * overlap / rotor_area;
        }
        result[static_cast<std::size_t>(downstream)] = state.speed_mps
            * std::clamp(1.0 - summed_deficit, 0.05, 1.0);
    }
    return result;
}

std::array<Point, kObservationPoints> observation_grid() {
    std::array<Point, kObservationPoints> result{};
    int index = 0;
    for (int row = 0; row < kObservationAxis; ++row) {
        for (int column = 0; column < kObservationAxis; ++column) {
            result[static_cast<std::size_t>(index++)] = {
                kObservationCenterX - 0.5 * kObservationSideM
                    + kObservationSideM * static_cast<double>(column)
                        / static_cast<double>(kObservationAxis - 1),
                kObservationCenterY - 0.5 * kObservationSideM
                    + kObservationSideM * static_cast<double>(row)
                        / static_cast<double>(kObservationAxis - 1),
            };
        }
    }
    return result;
}

double weighted_l10(
    std::vector<std::pair<double, double>> levels
) {
    std::sort(levels.begin(), levels.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    double cumulative = 0.0;
    for (const auto& [level, probability] : levels) {
        cumulative += probability;
        if (cumulative >= 0.10 - 1.0e-15) return level;
    }
    return levels.empty() ? -std::numeric_limits<double>::infinity()
                          : levels.back().first;
}

RawMetrics raw_metrics(
    const std::vector<double>& decision,
    const std::vector<WindState>& states,
    const double power_scale,
    const double noise_offset_dba
) {
    const auto layout = decode(decision);
    double minimum_spacing = std::numeric_limits<double>::infinity();
    double spacing_violation = 0.0;
    for (int left = 0; left < kTurbines; ++left) {
        for (int right = left + 1; right < kTurbines; ++right) {
            const double dx = layout[static_cast<std::size_t>(left)].x
                - layout[static_cast<std::size_t>(right)].x;
            const double dy = layout[static_cast<std::size_t>(left)].y
                - layout[static_cast<std::size_t>(right)].y;
            const double distance = std::hypot(dx, dy);
            minimum_spacing = std::min(minimum_spacing, distance);
            spacing_violation += std::max(0.0, kMinimumSpacingM - distance);
        }
    }
    const auto observation = observation_grid();
    std::array<std::array<double, kTurbines>, kObservationPoints>
        inverse_distance_squared{};
    for (int site = 0; site < kObservationPoints; ++site) {
        const auto& receiver = observation[static_cast<std::size_t>(site)];
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            const auto& source = layout[static_cast<std::size_t>(turbine)];
            const double distance = std::max(
                1.0, std::hypot(source.x - receiver.x, source.y - receiver.y)
            );
            inverse_distance_squared[static_cast<std::size_t>(site)]
                [static_cast<std::size_t>(turbine)] =
                    1.0 / (distance * distance);
        }
    }
    std::array<std::vector<std::pair<double, double>>, kObservationPoints>
        histories{};
    for (auto& history : histories) history.reserve(states.size());
    double annual_energy = 0.0;
    if (states.size() != 60U) {
        throw std::invalid_argument("T78 evaluator requires 12 by 5 wind states");
    }
    for (int direction = 0; direction < 12; ++direction) {
        WindState unit_state = states[static_cast<std::size_t>(5 * direction)];
        unit_state.speed_mps = 1.0;
        // With the paper's fixed Ct, Jensen's geometric deficit fraction is
        // speed-independent. Compute the O(N^2) partial-overlap wake only once
        // per direction and reuse it across that direction's five speed bins.
        const auto wake_factor = effective_winds(layout, unit_state);
        for (int speed = 0; speed < 5; ++speed) {
            const auto& state = states[static_cast<std::size_t>(
                5 * direction + speed
            )];
            std::array<double, kTurbines> wind{};
            std::array<double, kTurbines> source_intensity{};
            double farm_power = 0.0;
            for (int turbine = 0; turbine < kTurbines; ++turbine) {
                const std::size_t index = static_cast<std::size_t>(turbine);
                wind[index] = wake_factor[index] * state.speed_mps;
                farm_power += power_mw(wind[index]);
                source_intensity[index] = std::pow(
                    10.0, a_weighted_source_dba(wind[index]) / 10.0
                );
            }
            annual_energy += farm_power * state.probability
                * kAnnualHours / 1000.0;
            for (int site = 0; site < kObservationPoints; ++site) {
                double intensity = 0.0;
                #pragma omp simd reduction(+:intensity)
                for (int turbine = 0; turbine < kTurbines; ++turbine) {
                    intensity += source_intensity[
                        static_cast<std::size_t>(turbine)
                    ] * inverse_distance_squared[
                        static_cast<std::size_t>(site)
                    ][static_cast<std::size_t>(turbine)];
                }
                histories[static_cast<std::size_t>(site)].push_back({
                    10.0 * std::log10(std::max(intensity, 1.0e-300))
                        + noise_offset_dba,
                    state.probability,
                });
            }
        }
    }
    double maximum_l10 = -std::numeric_limits<double>::infinity();
    for (auto& history : histories) {
        maximum_l10 = std::max(maximum_l10, weighted_l10(std::move(history)));
    }
    return {
        annual_energy * power_scale,
        maximum_l10,
        minimum_spacing,
        spacing_violation,
    };
}

void repair_spacing(std::vector<double>& decision) {
    for (int pass = 0; pass < 12; ++pass) {
        bool changed = false;
        for (int left = 0; left < kTurbines; ++left) {
            for (int right = left + 1; right < kTurbines; ++right) {
                const std::size_t lx = static_cast<std::size_t>(2 * left);
                const std::size_t ly = lx + 1;
                const std::size_t rx = static_cast<std::size_t>(2 * right);
                const std::size_t ry = rx + 1;
                double dx = decision[lx] - decision[rx];
                double dy = decision[ly] - decision[ry];
                double distance = std::hypot(dx, dy);
                if (distance >= kMinimumSpacingM) continue;
                if (distance < 1.0e-12) {
                    const double angle = std::fmod(
                        static_cast<double>(left * 97 + right * 53), 360.0
                    ) * std::numbers::pi / 180.0;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    distance = 1.0;
                }
                const double movement = 0.5 * (kMinimumSpacingM - distance)
                    + 1.0e-6;
                dx /= distance;
                dy /= distance;
                decision[lx] = std::clamp(
                    decision[lx] + movement * dx, 0.0, kFarmWidthM
                );
                decision[ly] = std::clamp(
                    decision[ly] + movement * dy, 0.0, kFarmLengthM
                );
                decision[rx] = std::clamp(
                    decision[rx] - movement * dx, 0.0, kFarmWidthM
                );
                decision[ry] = std::clamp(
                    decision[ry] - movement * dy, 0.0, kFarmLengthM
                );
                changed = true;
            }
        }
        if (!changed) break;
    }
}

std::vector<double> strict_seed(std::vector<double> decision) {
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        const std::size_t x = static_cast<std::size_t>(2 * turbine);
        const std::size_t y = x + 1;
        double dx = decision[x] - kObservationCenterX;
        double dy = decision[y] - kObservationCenterY;
        const double distance = std::hypot(dx, dy);
        if (distance >= 2600.0) continue;
        if (distance < 1.0) {
            dx = 1.0;
            dy = 0.0;
        }
        const double shift = 0.75 * (2600.0 - distance);
        const double denominator = std::max(1.0, distance);
        decision[x] = std::clamp(
            decision[x] + shift * dx / denominator, 0.0, kFarmWidthM
        );
        decision[y] = std::clamp(
            decision[y] + shift * dy / denominator, 0.0, kFarmLengthM
        );
    }
    repair_spacing(decision);
    return decision;
}

std::uint64_t hash_result(
    const Particle& best,
    const int generations,
    const std::uint64_t physical_fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](const std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    for (const double value : best.personal_best) {
        mix(std::bit_cast<std::uint64_t>(value));
    }
    mix(std::bit_cast<std::uint64_t>(best.evaluation.objective_gwh));
    mix(std::bit_cast<std::uint64_t>(best.evaluation.maximum_l10_dba));
    mix(static_cast<std::uint64_t>(generations));
    mix(physical_fes);
    return hash;
}

}  // namespace

Problem::Problem(const Role role)
    : role_(role),
      id_("t78_" + role_name(role)),
      lower_bounds_(static_cast<std::size_t>(kDimensions), 0.0),
      upper_bounds_(static_cast<std::size_t>(kDimensions), 0.0) {
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        upper_bounds_[static_cast<std::size_t>(2 * turbine)] = kFarmWidthM;
        upper_bounds_[static_cast<std::size_t>(2 * turbine + 1)] = kFarmLengthM;
    }
    // Figure-5 direction totals and within-sector five-speed stacks were
    // digitized to plot precision and normalized because no raw MET array is
    // published. Direction 0 is north and proceeds clockwise in 30-degree bins.
    constexpr std::array<double, 12> direction_mass{
        4.8, 7.0, 9.2, 11.4, 8.2, 9.6, 11.2, 17.3, 20.2, 15.1, 11.7, 8.1,
    };
    constexpr std::array<double, 5> speed_mass{
        0.13, 0.31, 0.34, 0.17, 0.05,
    };
    constexpr std::array<double, 5> speed_midpoint{
        2.5, 7.5, 12.5, 17.5, 22.5,
    };
    double total = 0.0;
    for (const double mass : direction_mass) total += mass;
    wind_states_.reserve(60);
    for (int direction = 0; direction < 12; ++direction) {
        for (int speed = 0; speed < 5; ++speed) {
            wind_states_.push_back({
                30.0 * static_cast<double>(direction),
                speed_midpoint[static_cast<std::size_t>(speed)],
                direction_mass[static_cast<std::size_t>(direction)]
                    * speed_mass[static_cast<std::size_t>(speed)] / total,
            });
        }
    }
    const auto reference = reference_decision();
    const auto unscaled = raw_metrics(reference, wind_states_, 1.0, 0.0);
    if (!(unscaled.annual_energy_gwh > 0.0)
        || !std::isfinite(unscaled.maximum_l10_dba)) {
        throw std::runtime_error("T78 invalid calibration reference");
    }
    power_scale_ = 4015.17 / unscaled.annual_energy_gwh;
    noise_offset_dba_ = 48.60 - unscaled.maximum_l10_dba;
}

Role Problem::role() const noexcept { return role_; }
const std::string& Problem::id() const noexcept { return id_; }
int Problem::dimensions() const noexcept { return kDimensions; }
int Problem::population_size() const noexcept { return 100; }
int Problem::maximum_iterations() const noexcept { return 200; }
int Problem::paper_repeats() const noexcept { return 10; }
const std::vector<double>& Problem::lower_bounds() const noexcept {
    return lower_bounds_;
}
const std::vector<double>& Problem::upper_bounds() const noexcept {
    return upper_bounds_;
}
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}

std::vector<double> Problem::reference_decision() const {
    std::vector<double> result(static_cast<std::size_t>(kDimensions), 0.0);
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        result[static_cast<std::size_t>(2 * turbine)] = std::clamp(
            kReferenceLayout[static_cast<std::size_t>(turbine)].x,
            0.0,
            kFarmWidthM
        );
        result[static_cast<std::size_t>(2 * turbine + 1)] = std::clamp(
            kReferenceLayout[static_cast<std::size_t>(turbine)].y,
            0.0,
            kFarmLengthM
        );
    }
    return result;
}

Evaluation Problem::evaluate(const std::vector<double>& decision) const {
    const auto raw = raw_metrics(
        decision, wind_states_, power_scale_, noise_offset_dba_
    );
    const double excess = std::max(0.0, raw.maximum_l10_dba - kStrictLimitDba);
    // A sub-micrometre aggregate residual is the declared floating-point
    // feasibility tolerance, not a physical spacing violation.
    const double spacing_violation = raw.spacing_violation_m <= 1.0e-6
        ? 0.0 : raw.spacing_violation_m;
    const double hard_noise_violation = role_ == Role::strict_noise_control
        ? excess
        : std::max(0.0, raw.maximum_l10_dba - kEconomicUpperLimitDba);
    const double noise_penalty = role_ == Role::economic_compensation
        ? kCompensationGwhPerDba * excess : 0.0;
    const double constraint_penalty = kConstraintPenalty
        * (spacing_violation + hard_noise_violation);
    const bool spacing_feasible = spacing_violation == 0.0;
    const bool feasible = spacing_feasible && hard_noise_violation <= 1.0e-9;
    return {
        raw.annual_energy_gwh,
        raw.maximum_l10_dba,
        excess,
        raw.minimum_spacing_m,
        spacing_violation,
        hard_noise_violation,
        noise_penalty,
        constraint_penalty,
        raw.annual_energy_gwh - noise_penalty - constraint_penalty,
        feasible,
    };
}

std::vector<std::string> paper_case_ids() {
    return {"strict_noise_control", "economic_compensation"};
}

std::string role_name(const Role role) {
    if (role == Role::strict_noise_control) return "strict_noise_control";
    return "economic_compensation";
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0) throw std::invalid_argument("T78 workers must be positive");
    const int population_size = config.population_override > 0
        ? config.population_override : problem.population_size();
    const int maximum_iterations = config.iteration_override > 0
        ? config.iteration_override : problem.maximum_iterations();
    if (population_size < 2 || maximum_iterations < 1) {
        throw std::invalid_argument("T78 population/iterations invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const auto& lower = problem.lower_bounds();
    const auto& upper = problem.upper_bounds();
    std::vector<double> base = problem.reference_decision();
    if (problem.role() == Role::strict_noise_control) base = strict_seed(base);
    std::vector<Particle> population(static_cast<std::size_t>(population_size));
    executor.parallel_for(0, population_size, [&](const int index) {
        auto& particle = population[static_cast<std::size_t>(index)];
        particle.position = base;
        particle.velocity.assign(static_cast<std::size_t>(kDimensions), 0.0);
        if (index > 0) {
            for (int coordinate = 0; coordinate < kDimensions; ++coordinate) {
                const std::size_t dimension = static_cast<std::size_t>(coordinate);
                const double range = upper[dimension] - lower[dimension];
                particle.position[dimension] = std::clamp(
                    particle.position[dimension]
                        + 0.025 * range * rng.normal(
                            0, 7801, index, coordinate
                        ),
                    lower[dimension],
                    upper[dimension]
                );
                particle.velocity[dimension] = 0.01 * range * rng.normal(
                    0, 7802, index, coordinate
                );
            }
            repair_spacing(particle.position);
        }
    });
    double evaluator_seconds = 0.0;
    auto evaluate_population = [&] {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, population_size, [&](const int index) {
            auto& particle = population[static_cast<std::size_t>(index)];
            particle.evaluation = problem.evaluate(particle.position);
            particle.fitness = -particle.evaluation.objective_gwh;
        });
        evaluator_seconds += elapsed(evaluation_started);
    };
    evaluate_population();
    std::uint64_t physical_fes = static_cast<std::uint64_t>(population_size);
    for (auto& particle : population) {
        particle.personal_best = particle.position;
        particle.personal_best_fitness = particle.fitness;
    }
    auto best_index = [&] {
        int best = 0;
        for (int index = 1; index < population_size; ++index) {
            if (population[static_cast<std::size_t>(index)]
                    .personal_best_fitness
                < population[static_cast<std::size_t>(best)]
                    .personal_best_fitness) {
                best = index;
            }
        }
        return best;
    };
    int global_index = best_index();
    std::vector<double> global_best = population[
        static_cast<std::size_t>(global_index)
    ].personal_best;
    double global_fitness = population[
        static_cast<std::size_t>(global_index)
    ].personal_best_fitness;
    for (int generation = 1; generation <= maximum_iterations; ++generation) {
        executor.parallel_for(0, population_size, [&](const int index) {
            auto& particle = population[static_cast<std::size_t>(index)];
            for (int coordinate = 0; coordinate < kDimensions; ++coordinate) {
                const std::size_t dimension = static_cast<std::size_t>(coordinate);
                const double range = upper[dimension] - lower[dimension];
                const double velocity = particle.velocity[dimension]
                    + 2.0 * rng.uniform(
                        generation, 7803, index, coordinate, 0
                    ) * (particle.personal_best[dimension]
                        - particle.position[dimension])
                    + 2.0 * rng.uniform(
                        generation, 7803, index, coordinate, 1
                    ) * (global_best[dimension] - particle.position[dimension]);
                particle.velocity[dimension] = std::clamp(
                    velocity, -0.10 * range, 0.10 * range
                );
                const double proposed = particle.position[dimension]
                    + particle.velocity[dimension];
                particle.position[dimension] = std::clamp(
                    proposed, lower[dimension], upper[dimension]
                );
                if (particle.position[dimension] != proposed) {
                    particle.velocity[dimension] *= -0.5;
                }
            }
            repair_spacing(particle.position);
        });
        evaluate_population();
        physical_fes += static_cast<std::uint64_t>(population_size);
        for (auto& particle : population) {
            if (particle.fitness < particle.personal_best_fitness) {
                particle.personal_best = particle.position;
                particle.personal_best_fitness = particle.fitness;
            }
        }
        global_index = best_index();
        if (population[static_cast<std::size_t>(global_index)]
                .personal_best_fitness < global_fitness) {
            global_best = population[
                static_cast<std::size_t>(global_index)
            ].personal_best;
            global_fitness = population[
                static_cast<std::size_t>(global_index)
            ].personal_best_fitness;
        }
    }
    int final_index = best_index();
    Particle best = population[static_cast<std::size_t>(final_index)];
    if (global_fitness < best.personal_best_fitness) {
        best.personal_best = global_best;
        best.personal_best_fitness = global_fitness;
    }
    best.evaluation = problem.evaluate(best.personal_best);
    const double end_to_end = elapsed(started);
    return {
        .case_id = problem.id(),
        .method_semantic_id = "t78_traditional_pso_declared_v1",
        .problem_semantic_id = "t78_fino3_noise_layout_two_case_declared_v1",
        .protocol_semantic_id = "t78_native_2x10_repeat_declared_v1",
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers = executor.work_receipt().distinct_participants,
        .population_size = population_size,
        .generations = maximum_iterations,
        .dimensions = kDimensions,
        .physical_fes = physical_fes,
        .evaluator_seconds = evaluator_seconds,
        .algorithm_seconds = std::max(0.0, end_to_end - evaluator_seconds),
        .end_to_end_seconds = end_to_end,
        .scientific_hash = hash_result(best, maximum_iterations, physical_fes),
        .best_decision = best.personal_best,
        .best_evaluation = best.evaluation,
    };
}

}  // namespace core99::t78
