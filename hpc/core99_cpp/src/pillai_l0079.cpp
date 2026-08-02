/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0079 pure-C++ Middelgrunden evaluator, adaptive GA and PSO
Paper/DOI: Pillai et al.; 10.1016/j.oceaneng.2017.04.049
Public assets, omissions, conflicts, completion decisions and claim boundary:
include/core99/pillai_l0079.hpp
Missing information and reconstruction boundaries are declared there.
Method semantic IDs: l0079_adaptive_ga_three_encoding_declared_v1;
l0079_gbest_pso_three_encoding_declared_v1.
HPC analysis: evidence/development/L0079_H0_H4_mathematical_hpc_analysis_20260731.md
Controlling contract: shared/contracts/core99_l0079_pillai_middelgrunden_2017.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/pillai_l0079.hpp"

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
#include <unordered_set>
#include <utility>
#include <vector>

namespace core99::l0079 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr int kTurbines = 20;
constexpr int kDirections = 12;
constexpr int kFirstSpeed = 3;
constexpr int kLastSpeed = 25;
constexpr int kSpeeds = kLastSpeed - kFirstSpeed + 1;
constexpr int kWindStates = kDirections * kSpeeds;
constexpr double kRotorDiameterM = 76.0;
constexpr double kRotorAreaM2 = kPi * 0.25 * kRotorDiameterM * kRotorDiameterM;
constexpr double kHubHeightM = 64.0;
constexpr double kMinimumSpacingM = 175.0;
constexpr double kDomainSemiMajorM = 1990.0;
constexpr double kDomainSemiMinorM = 5700000.0 / (kPi * kDomainSemiMajorM);
constexpr double kDomainAreaKm2 = 5.7;
constexpr double kTargetValidationAepMwh = 95410.0;
constexpr double kValidationAvailability = 0.93;
constexpr double kTargetOptimizationAepMwh =
    kTargetValidationAepMwh / kValidationAvailability;
constexpr double kReferenceLifetimeCostGbp = 91500000.0;
constexpr double kReferenceLcoe = 86.63;
constexpr double kReferenceArrayCableCostGbp = 5319000.0;
constexpr int kCableGroupCapacity = 5;

constexpr std::array<double, kDirections> kDirectionFrequency{
    0.07,0.05,0.05,0.09,0.09,0.08,0.14,0.10,0.09,0.11,0.08,0.04
};
constexpr std::array<double, kDirections> kWeibullScale{
    7.54,6.77,6.86,7.27,8.02,7.44,7.34,6.74,6.87,7.07,6.76,5.92
};
constexpr std::array<double, kDirections> kWeibullShape{
    2.01,2.32,3.09,2.19,3.00,2.73,2.21,2.32,2.76,2.72,2.42,2.05
};
constexpr std::array<double, kDirections> kAmbientTi{
    0.094,0.082,0.085,0.098,0.085,0.099,
    0.114,0.115,0.109,0.127,0.128,0.121
};
constexpr std::array<double, kSpeeds> kB76PowerKw{
    0,43,133,237,401,623,886,1190,1502,1740,1891,1962,
    1988,1996,1999,2000,2000,2000,2000,2000,2000,2000,2000
};
constexpr std::array<double, kSpeeds> kB76Ct{
    0,0.857,0.858,0.810,0.853,0.870,0.811,0.756,0.679,0.584,
    0.511,0.439,0.383,0.338,0.301,0.271,0.246,0.225,0.207,
    0.192,0.179,0.168,0.158
};
constexpr std::array<Point, kTurbines> kAsBuilt{
    Point{730458.93,6179564.99}, Point{730498.59,6179386.19},
    Point{730534.82,6179206.96}, Point{730567.71,6179027.04},
    Point{730597.40,6178846.51}, Point{730623.63,6178665.47},
    Point{730646.62,6178484.06}, Point{730666.30,6178302.19},
    Point{730682.87,6178119.86}, Point{730695.70,6177937.44},
    Point{730705.46,6177754.84}, Point{730711.71,6177571.92},
    Point{730714.74,6177389.07}, Point{730714.45,6177206.12},
    Point{730710.62,6177023.25}, Point{730703.70,6176840.44},
    Point{730693.34,6176657.73}, Point{730679.50,6176475.28},
    Point{730662.60,6176293.10}, Point{730642.20,6176111.30}
};

double elapsed_seconds(const Clock::time_point started) {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

double squared_distance(const Point& first, const Point& second) {
    const double dx = first.x_m - second.x_m;
    const double dy = first.y_m - second.y_m;
    return dx * dx + dy * dy;
}

double interpolate_table(
    const std::array<double, kSpeeds>& values,
    const double speed
) {
    if (speed < kFirstSpeed || speed > kLastSpeed) return 0.0;
    const int low = std::clamp(
        static_cast<int>(std::floor(speed)) - kFirstSpeed,
        0,
        kSpeeds - 1
    );
    const int high = std::min(low + 1, kSpeeds - 1);
    return std::lerp(
        values[static_cast<std::size_t>(low)],
        values[static_cast<std::size_t>(high)],
        speed - std::floor(speed)
    );
}

double weibull_cdf(const double speed, const double scale, const double shape) {
    if (speed <= 0.0) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    return hash * 1099511628211ULL;
}

std::uint64_t decision_hash(const std::vector<double>& values) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const double value : values) {
        const double quantized = std::round(value * 1.0e8) / 1.0e8;
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(quantized));
    }
    return hash;
}

double minimum_spacing(const std::vector<Point>& layout) {
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t first = 0; first < layout.size(); ++first) {
        for (std::size_t second = first + 1; second < layout.size(); ++second) {
            result = std::min(
                result,
                std::sqrt(squared_distance(layout[first], layout[second]))
            );
        }
    }
    return std::isfinite(result) ? result : 0.0;
}

}  // namespace

struct Problem::Impl {
    explicit Impl(const CandidateProfile selected_profile)
        : profile(selected_profile), as_built(kAsBuilt.begin(), kAsBuilt.end()) {
        center = std::accumulate(
            as_built.begin(), as_built.end(), Point{},
            [](Point sum, const Point& value) {
                sum.x_m += value.x_m;
                sum.y_m += value.y_m;
                return sum;
            }
        );
        center.x_m /= static_cast<double>(as_built.size());
        center.y_m /= static_cast<double>(as_built.size());
        build_wind_states();
        build_candidates();
        reference_cable_length_m = cable_length(as_built);
        if (!(reference_cable_length_m > 0.0)) {
            throw std::runtime_error("L0079 reference cable construction failed");
        }
        cable_cost_per_m =
            kReferenceArrayCableCostGbp / reference_cable_length_m;
        aep_calibration = 1.0;
        const RawEnergy raw = raw_energy(as_built);
        if (!(raw.net_aep_8766 > 0.0)) {
            throw std::runtime_error("L0079 reference AEP construction failed");
        }
        aep_calibration = kTargetOptimizationAepMwh / raw.net_aep_8766;
        discounted_energy_years = kReferenceLifetimeCostGbp
            / (kTargetOptimizationAepMwh * kReferenceLcoe);
    }

    struct WindState {
        double direction_degrees = 0.0;
        double speed_mps = 0.0;
        double probability = 0.0;
        double turbulence_intensity = 0.0;
    };

    struct RawEnergy {
        double gross_aep_8766 = 0.0;
        double net_aep_8766 = 0.0;
        double loss_fraction = 0.0;
    };

    CandidateProfile profile;
    Point center;
    std::vector<Point> as_built;
    std::vector<Point> candidates;
    std::vector<WindState> wind_states;
    double reference_cable_length_m = 0.0;
    double cable_cost_per_m = 0.0;
    double aep_calibration = 1.0;
    double discounted_energy_years = 1.0;

    [[nodiscard]] bool inside(const Point& point) const {
        const double nx = (point.x_m - center.x_m) / kDomainSemiMinorM;
        const double ny = (point.y_m - center.y_m) / kDomainSemiMajorM;
        return nx * nx + ny * ny <= 1.0 + 1.0e-12;
    }

    [[nodiscard]] Point project_inside(Point point) const {
        const double nx = (point.x_m - center.x_m) / kDomainSemiMinorM;
        const double ny = (point.y_m - center.y_m) / kDomainSemiMajorM;
        const double radius = std::sqrt(nx * nx + ny * ny);
        if (radius <= 0.999999) return point;
        const double scale = 0.999999 / radius;
        point.x_m = center.x_m + (point.x_m - center.x_m) * scale;
        point.y_m = center.y_m + (point.y_m - center.y_m) * scale;
        return point;
    }

    void build_wind_states() {
        wind_states.reserve(kWindStates);
        for (int direction = 0; direction < kDirections; ++direction) {
            for (int speed = kFirstSpeed; speed <= kLastSpeed; ++speed) {
                const double mass = kDirectionFrequency[
                    static_cast<std::size_t>(direction)]
                    * (weibull_cdf(
                           static_cast<double>(speed) + 0.5,
                           kWeibullScale[static_cast<std::size_t>(direction)],
                           kWeibullShape[static_cast<std::size_t>(direction)])
                       - weibull_cdf(
                           std::max(0.0, static_cast<double>(speed) - 0.5),
                           kWeibullScale[static_cast<std::size_t>(direction)],
                           kWeibullShape[static_cast<std::size_t>(direction)]));
                wind_states.push_back({
                    30.0 * direction,
                    static_cast<double>(speed),
                    mass,
                    kAmbientTi[static_cast<std::size_t>(direction)]
                });
            }
        }
    }

    void build_candidates() {
        std::vector<Point> full;
        int row = 0;
        for (double y = -kDomainSemiMajorM;
             y <= kDomainSemiMajorM + 1.0e-9;
             y += 100.0 * std::sqrt(3.0) / 2.0, ++row) {
            for (double x = -kDomainSemiMinorM + (row % 2 ? 50.0 : 0.0);
                 x <= kDomainSemiMinorM + 1.0e-9;
                 x += 100.0) {
                Point point{center.x_m + x, center.y_m + y};
                if (inside(point)) full.push_back(point);
            }
        }
        if (full.size() < 658U) {
            throw std::runtime_error("L0079 declared triangular grid too small");
        }
        std::stable_sort(full.begin(), full.end(), [&](const Point& a, const Point& b) {
            const double ar = std::pow(
                (a.x_m - center.x_m) / kDomainSemiMinorM, 2)
                + std::pow((a.y_m - center.y_m) / kDomainSemiMajorM, 2);
            const double br = std::pow(
                (b.x_m - center.x_m) / kDomainSemiMinorM, 2)
                + std::pow((b.y_m - center.y_m) / kDomainSemiMajorM, 2);
            if (ar != br) return ar < br;
            if (a.y_m != b.y_m) return a.y_m < b.y_m;
            return a.x_m < b.x_m;
        });
        const std::size_t count = profile == CandidateProfile::journal_628
            ? 628U : 658U;
        candidates.assign(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(count));
    }

    [[nodiscard]] double cable_length(const std::vector<Point>& layout) const {
        if (layout.size() != kTurbines) return 0.0;
        const Point shore{center.x_m - kDomainSemiMinorM, center.y_m};
        std::vector<int> order(kTurbines);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
            const double aa = std::atan2(
                layout[static_cast<std::size_t>(a)].y_m - shore.y_m,
                layout[static_cast<std::size_t>(a)].x_m - shore.x_m);
            const double ab = std::atan2(
                layout[static_cast<std::size_t>(b)].y_m - shore.y_m,
                layout[static_cast<std::size_t>(b)].x_m - shore.x_m);
            return aa == ab ? a < b : aa < ab;
        });
        double total = 0.0;
        for (int begin = 0; begin < kTurbines; begin += kCableGroupCapacity) {
            const int count = std::min(kCableGroupCapacity, kTurbines - begin);
            std::vector<Point> nodes;
            nodes.reserve(static_cast<std::size_t>(count + 1));
            nodes.push_back(shore);
            for (int offset = 0; offset < count; ++offset) {
                nodes.push_back(layout[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(begin + offset)])]);
            }
            std::vector<double> best(nodes.size(),
                                     std::numeric_limits<double>::infinity());
            std::vector<bool> used(nodes.size(), false);
            best[0] = 0.0;
            for (std::size_t step = 0; step < nodes.size(); ++step) {
                std::size_t selected = nodes.size();
                for (std::size_t node = 0; node < nodes.size(); ++node) {
                    if (!used[node] && (selected == nodes.size()
                        || best[node] < best[selected])) selected = node;
                }
                used[selected] = true;
                total += best[selected];
                for (std::size_t node = 0; node < nodes.size(); ++node) {
                    if (!used[node]) {
                        best[node] = std::min(
                            best[node],
                            std::sqrt(squared_distance(nodes[selected], nodes[node]))
                        );
                    }
                }
            }
        }
        return total;
    }

    [[nodiscard]] double state_power_mw(
        const std::vector<Point>& layout,
        const WindState& state
    ) const {
        const double radians = state.direction_degrees * kPi / 180.0;
        const double down_x = -std::sin(radians);
        const double down_y = -std::cos(radians);
        const double cross_x = -down_y;
        const double cross_y = down_x;
        std::array<double, kTurbines> down{};
        std::array<double, kTurbines> cross{};
        std::array<int, kTurbines> order{};
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            const Point& point = layout[static_cast<std::size_t>(turbine)];
            down[static_cast<std::size_t>(turbine)] =
                point.x_m * down_x + point.y_m * down_y;
            cross[static_cast<std::size_t>(turbine)] =
                point.x_m * cross_x + point.y_m * cross_y;
            order[static_cast<std::size_t>(turbine)] = turbine;
        }
        std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
            return down[static_cast<std::size_t>(a)] == down[static_cast<std::size_t>(b)]
                ? a < b
                : down[static_cast<std::size_t>(a)] < down[static_cast<std::size_t>(b)];
        });
        std::array<double, kTurbines> effective{};
        double power_mw = 0.0;
        for (int rank = 0; rank < kTurbines; ++rank) {
            const int target = order[static_cast<std::size_t>(rank)];
            double rss = 0.0;
            for (int upstream_rank = 0; upstream_rank < rank; ++upstream_rank) {
                const int source = order[static_cast<std::size_t>(upstream_rank)];
                const double x = down[static_cast<std::size_t>(target)]
                    - down[static_cast<std::size_t>(source)];
                if (x <= 1.0e-9) continue;
                const double r = std::abs(
                    cross[static_cast<std::size_t>(target)]
                    - cross[static_cast<std::size_t>(source)]);
                const double ct = std::clamp(
                    interpolate_table(kB76Ct, effective[static_cast<std::size_t>(source)]),
                    0.0, 0.95);
                if (ct <= 1.0e-12) continue;
                const double root = std::sqrt(std::max(1.0e-12, 1.0 - ct));
                const double effective_diameter = kRotorDiameterM * std::sqrt(
                    (1.0 + root) / (2.0 * root));
                const double rnb = std::max(
                    1.08 * kRotorDiameterM,
                    1.08 * kRotorDiameterM
                        + 21.7 * kRotorDiameterM
                            * (state.turbulence_intensity - 0.05));
                const double r95 = 0.5 * (rnb + std::min(kHubHeightM, rnb));
                const double ratio = 2.0 * r95 / effective_diameter;
                const double denominator = ratio * ratio * ratio - 1.0;
                if (denominator <= 1.0e-12) continue;
                const double x0 = 9.5 * kRotorDiameterM / denominator;
                const double c1 = std::pow(effective_diameter / 2.0, 2.5)
                    * std::pow(105.0 / (2.0 * kPi), -0.5)
                    * std::pow(ct * kRotorAreaM2 * x0, -5.0 / 6.0);
                const double three_c1_sq = 3.0 * c1 * c1;
                const double wake_radius =
                    std::pow(35.0 / (2.0 * kPi), 0.2)
                    * std::pow(three_c1_sq, 0.2)
                    * std::cbrt(ct * kRotorAreaM2 * x);
                if (r > wake_radius) continue;
                const double shifted = x + x0;
                const double bracket = std::pow(r, 1.5)
                        / std::sqrt(three_c1_sq * ct * kRotorAreaM2 * shifted)
                    - std::pow(35.0 / (2.0 * kPi), 0.3)
                        * std::pow(three_c1_sq, -0.2);
                const double deficit = std::clamp(
                    (1.0 / 9.0)
                        * std::cbrt(ct * kRotorAreaM2 / (shifted * shifted))
                        * bracket * bracket,
                    0.0, 0.95);
                rss += deficit * deficit;
            }
            effective[static_cast<std::size_t>(target)] = state.speed_mps
                * (1.0 - std::min(0.95, std::sqrt(rss)));
            power_mw += 0.001 * interpolate_table(
                kB76PowerKw,
                effective[static_cast<std::size_t>(target)]
            );
        }
        return power_mw;
    }

    [[nodiscard]] RawEnergy raw_energy(const std::vector<Point>& layout) const {
        double expected_power_mw = 0.0;
        for (const WindState& state : wind_states) {
            expected_power_mw += state.probability * state_power_mw(layout, state);
        }
        const double length = cable_length(layout);
        const double loss = std::clamp(0.005 + 1.5e-6 * length, 0.005, 0.08);
        return {
            8766.0 * expected_power_mw,
            8766.0 * expected_power_mw * (1.0 - loss),
            loss
        };
    }

    [[nodiscard]] Evaluation evaluate(const std::vector<Point>& layout) const {
        Evaluation result;
        if (layout.size() != kTurbines) return result;
        for (const Point& point : layout) {
            if (!inside(point)) return result;
        }
        result.minimum_spacing_m = minimum_spacing(layout);
        if (result.minimum_spacing_m + 1.0e-9 < kMinimumSpacingM) return result;
        const RawEnergy raw = raw_energy(layout);
        result.gross_aep_mwh_8766 = raw.gross_aep_8766 * aep_calibration;
        result.net_aep_mwh_8766 = raw.net_aep_8766 * aep_calibration;
        result.net_aep_mwh_8760 = result.net_aep_mwh_8766 * 8760.0 / 8766.0;
        result.cable_length_m = cable_length(layout);
        result.electrical_loss_fraction = raw.loss_fraction;
        result.lifetime_cost_gbp = kReferenceLifetimeCostGbp
            + cable_cost_per_m
                * (result.cable_length_m - reference_cable_length_m);
        result.lifetime_cost_gbp = std::max(
            0.5 * kReferenceLifetimeCostGbp,
            result.lifetime_cost_gbp
        );
        result.lcoe_gbp_per_mwh = result.lifetime_cost_gbp
            / (result.net_aep_mwh_8766 * discounted_energy_years);
        result.feasible = std::isfinite(result.lcoe_gbp_per_mwh)
            && result.lcoe_gbp_per_mwh > 0.0;
        return result;
    }

    [[nodiscard]] std::vector<Point> decode(
        const std::vector<double>& decision,
        const ConstraintMode mode
    ) const {
        if (mode == ConstraintMode::binary) {
            if (decision.size() != candidates.size()) return {};
            std::vector<int> order(decision.size());
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
                const double av = decision[static_cast<std::size_t>(a)];
                const double bv = decision[static_cast<std::size_t>(b)];
                return av == bv ? a < b : av > bv;
            });
            std::vector<Point> result;
            result.reserve(kTurbines);
            for (const int index : order) {
                const Point& point = candidates[static_cast<std::size_t>(index)];
                bool separated = true;
                for (const Point& existing : result) {
                    if (squared_distance(point, existing)
                        + 1.0e-9 < kMinimumSpacingM * kMinimumSpacingM) {
                        separated = false;
                        break;
                    }
                }
                if (separated) result.push_back(point);
                if (result.size() == kTurbines) break;
            }
            return result.size() == kTurbines ? result : std::vector<Point>{};
        }
        if (mode == ConstraintMode::continuous) {
            if (decision.size() != 2U * kTurbines) return {};
            std::vector<Point> result(kTurbines);
            for (int turbine = 0; turbine < kTurbines; ++turbine) {
                result[static_cast<std::size_t>(turbine)] = {
                    decision[static_cast<std::size_t>(2 * turbine)],
                    decision[static_cast<std::size_t>(2 * turbine + 1)]
                };
            }
            return result;
        }
        if (decision.size() != 6U) return {};
        const double spacing_one = decision[0];
        const double spacing_two = decision[1];
        const double theta_one = decision[2] * kPi / 180.0;
        const double theta_two = decision[3] * kPi / 180.0;
        const Point u{std::cos(theta_one), std::sin(theta_one)};
        const Point v{std::cos(theta_two), std::sin(theta_two)};
        const Point origin{
            center.x_m + decision[4] * u.x_m + decision[5] * v.x_m,
            center.y_m + decision[4] * u.y_m + decision[5] * v.y_m
        };
        std::vector<Point> legal;
        for (int first = -20; first <= 20; ++first) {
            for (int second = -20; second <= 20; ++second) {
                const Point point{
                    origin.x_m + first * spacing_one * u.x_m
                        + second * spacing_two * v.x_m,
                    origin.y_m + first * spacing_one * u.y_m
                        + second * spacing_two * v.y_m
                };
                if (inside(point)) legal.push_back(point);
            }
        }
        std::stable_sort(legal.begin(), legal.end(), [&](const Point& a, const Point& b) {
            const double ad = squared_distance(a, center);
            const double bd = squared_distance(b, center);
            if (ad != bd) return ad < bd;
            if (a.y_m != b.y_m) return a.y_m < b.y_m;
            return a.x_m < b.x_m;
        });
        std::vector<Point> result;
        result.reserve(kTurbines);
        for (const Point& point : legal) {
            bool separated = true;
            for (const Point& existing : result) {
                if (squared_distance(point, existing)
                    + 1.0e-9 < kMinimumSpacingM * kMinimumSpacingM) {
                    separated = false;
                    break;
                }
            }
            if (separated) result.push_back(point);
            if (result.size() == kTurbines) break;
        }
        return result.size() == kTurbines ? result : std::vector<Point>{};
    }
};

namespace {

struct Individual {
    std::vector<double> decision;
    Evaluation evaluation;
};

double objective(const Evaluation& evaluation) {
    return evaluation.feasible
        ? evaluation.lcoe_gbp_per_mwh
        : 1.0e12;
}

int decision_dimension(
    const Problem::Impl& problem,
    const ConstraintMode mode
) {
    if (mode == ConstraintMode::array) return 6;
    if (mode == ConstraintMode::continuous) return 2 * kTurbines;
    return static_cast<int>(problem.candidates.size());
}

void normalize_array_decision(std::vector<double>& decision) {
    decision[0] = std::clamp(decision[0], kMinimumSpacingM, 900.0);
    decision[1] = std::clamp(decision[1], kMinimumSpacingM, 900.0);
    for (int coordinate = 2; coordinate <= 3; ++coordinate) {
        decision[static_cast<std::size_t>(coordinate)] = std::fmod(
            decision[static_cast<std::size_t>(coordinate)], 360.0);
        if (decision[static_cast<std::size_t>(coordinate)] < 0.0) {
            decision[static_cast<std::size_t>(coordinate)] += 360.0;
        }
    }
    double difference = std::fmod(std::abs(decision[3] - decision[2]), 180.0);
    difference = std::min(difference, 180.0 - difference);
    if (difference < 45.0) decision[3] = std::fmod(decision[2] + 90.0, 360.0);
    decision[4] = std::clamp(decision[4], 0.0, decision[0]);
    decision[5] = std::clamp(decision[5], 0.0, decision[1]);
}

std::vector<double> random_decision(
    const Problem::Impl& problem,
    const ConstraintMode mode,
    const fode::CounterRng& random,
    const std::uint64_t event,
    const int individual
) {
    if (mode == ConstraintMode::array) {
        for (int attempt = 0; attempt < 10000; ++attempt) {
            const double angle = 360.0 * random.uniform(
                event, 7901, individual, attempt, 0);
            std::vector<double> result{
                220.0 + 480.0 * random.uniform(event,7901,individual,attempt,1),
                220.0 + 480.0 * random.uniform(event,7901,individual,attempt,2),
                angle,
                std::fmod(angle + 90.0, 360.0),
                0.0,
                0.0
            };
            result[4] = result[0] * random.uniform(event,7901,individual,attempt,3);
            result[5] = result[1] * random.uniform(event,7901,individual,attempt,4);
            if (problem.decode(result, mode).size() == kTurbines) return result;
        }
        throw std::runtime_error("L0079 array initialization exhausted");
    }
    if (mode == ConstraintMode::binary) {
        std::vector<double> result(problem.candidates.size());
        for (std::size_t index = 0; index < result.size(); ++index) {
            result[index] = random.uniform(event,7902,individual,index);
        }
        return result;
    }
    std::vector<double> result(static_cast<std::size_t>(2 * kTurbines));
    std::vector<Point> layout;
    layout.reserve(kTurbines);
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        bool accepted = false;
        for (int attempt = 0; attempt < 100000; ++attempt) {
            const double radius = std::sqrt(random.uniform(
                event,7903,individual,turbine,2 * attempt));
            const double angle = 2.0 * kPi * random.uniform(
                event,7903,individual,turbine,2 * attempt + 1);
            const Point point{
                problem.center.x_m + kDomainSemiMinorM * radius * std::cos(angle),
                problem.center.y_m + kDomainSemiMajorM * radius * std::sin(angle)
            };
            bool separated = true;
            for (const Point& existing : layout) {
                if (squared_distance(point, existing)
                    < kMinimumSpacingM * kMinimumSpacingM) {
                    separated = false;
                    break;
                }
            }
            if (!separated) continue;
            layout.push_back(point);
            result[static_cast<std::size_t>(2 * turbine)] = point.x_m;
            result[static_cast<std::size_t>(2 * turbine + 1)] = point.y_m;
            accepted = true;
            break;
        }
        if (!accepted) throw std::runtime_error("L0079 continuous initialization exhausted");
    }
    return result;
}

void repair_decision(
    const Problem::Impl& problem,
    std::vector<double>& decision,
    const ConstraintMode mode,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const int individual
) {
    if (mode == ConstraintMode::array) {
        normalize_array_decision(decision);
        if (problem.decode(decision, mode).size() != kTurbines) {
            decision = random_decision(problem, mode, random,
                                       generation + phase, individual);
        }
        return;
    }
    if (mode == ConstraintMode::binary) {
        const std::vector<Point> chosen = problem.decode(decision, mode);
        std::fill(decision.begin(), decision.end(), 0.0);
        for (const Point& point : chosen) {
            const auto found = std::find_if(
                problem.candidates.begin(), problem.candidates.end(),
                [&](const Point& candidate) {
                    return candidate.x_m == point.x_m && candidate.y_m == point.y_m;
                });
            if (found != problem.candidates.end()) {
                decision[static_cast<std::size_t>(found - problem.candidates.begin())] = 1.0;
            }
        }
        return;
    }
    std::vector<Point> accepted;
    accepted.reserve(kTurbines);
    for (int turbine = 0; turbine < kTurbines; ++turbine) {
        Point point{
            decision[static_cast<std::size_t>(2 * turbine)],
            decision[static_cast<std::size_t>(2 * turbine + 1)]
        };
        point = problem.project_inside(point);
        bool separated = true;
        for (const Point& existing : accepted) {
            if (squared_distance(point, existing)
                < kMinimumSpacingM * kMinimumSpacingM) {
                separated = false;
                break;
            }
        }
        for (int attempt = 0; !separated && attempt < 100000; ++attempt) {
            const double radius = std::sqrt(random.uniform(
                generation, phase, individual, turbine, 2 * attempt));
            const double angle = 2.0 * kPi * random.uniform(
                generation, phase, individual, turbine, 2 * attempt + 1);
            point = {
                problem.center.x_m + kDomainSemiMinorM * radius * std::cos(angle),
                problem.center.y_m + kDomainSemiMajorM * radius * std::sin(angle)
            };
            separated = true;
            for (const Point& existing : accepted) {
                if (squared_distance(point, existing)
                    < kMinimumSpacingM * kMinimumSpacingM) {
                    separated = false;
                    break;
                }
            }
        }
        if (!separated) throw std::runtime_error("L0079 continuous repair exhausted");
        accepted.push_back(point);
        decision[static_cast<std::size_t>(2 * turbine)] = point.x_m;
        decision[static_cast<std::size_t>(2 * turbine + 1)] = point.y_m;
    }
}

void evaluate_batch(
    const Problem::Impl& problem,
    std::vector<Individual>& values,
    const ConstraintMode mode,
    fode::PersistentExecutor& executor,
    double& evaluator_seconds,
    std::uint64_t& physical_fes
) {
    const auto started = Clock::now();
    executor.parallel_for(0, static_cast<int>(values.size()), [&](const int index) {
        Individual& value = values[static_cast<std::size_t>(index)];
        value.evaluation = problem.evaluate(problem.decode(value.decision, mode));
    });
    evaluator_seconds += elapsed_seconds(started);
    physical_fes += static_cast<std::uint64_t>(values.size());
}

std::size_t best_index(const std::vector<Individual>& population) {
    return static_cast<std::size_t>(std::min_element(
        population.begin(), population.end(),
        [](const Individual& a, const Individual& b) {
            return objective(a.evaluation) < objective(b.evaluation);
        }) - population.begin());
}

int diversity_count(const std::vector<Individual>& population) {
    std::unordered_set<std::uint64_t> hashes;
    for (const Individual& individual : population) {
        hashes.insert(decision_hash(individual.decision));
    }
    return static_cast<int>(hashes.size());
}

int roulette_parent(
    const std::vector<Individual>& population,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const int child,
    const int draw
) {
    double fitness_sum = 0.0;
    for (const Individual& value : population) {
        fitness_sum += objective(value.evaluation);
    }
    std::vector<double> weights(population.size());
    double total = 0.0;
    for (std::size_t index = 0; index < population.size(); ++index) {
        weights[index] = std::max(
            1.0e-12,
            1.0 - objective(population[index].evaluation) / fitness_sum
        );
        total += weights[index];
    }
    double target = total * random.uniform(generation,7904,child,draw);
    for (std::size_t index = 0; index < weights.size(); ++index) {
        target -= weights[index];
        if (target <= 0.0) return static_cast<int>(index);
    }
    return static_cast<int>(weights.size() - 1);
}

double adaptive_probability(
    const std::vector<Individual>& population,
    const double value,
    const double maximum
) {
    double best_quality = 0.0;
    double mean_quality = 0.0;
    for (const Individual& item : population) {
        const double quality = 1.0 / objective(item.evaluation);
        best_quality = std::max(best_quality, quality);
        mean_quality += quality;
    }
    mean_quality /= static_cast<double>(population.size());
    const double quality = 1.0 / value;
    if (quality < mean_quality || best_quality <= mean_quality + 1.0e-18) {
        return maximum;
    }
    return std::clamp(
        maximum * (best_quality - quality) / (best_quality - mean_quality),
        0.0, maximum
    );
}

void mutate(
    const Problem::Impl& problem,
    std::vector<double>& decision,
    const ConstraintMode mode,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const int child
) {
    if (mode == ConstraintMode::binary) {
        std::vector<int> ones;
        std::vector<int> zeros;
        for (std::size_t index = 0; index < decision.size(); ++index) {
            (decision[index] > 0.5 ? ones : zeros).push_back(static_cast<int>(index));
        }
        if (!ones.empty() && !zeros.empty()) {
            const int on = ones[static_cast<std::size_t>(random.integer(
                0,static_cast<int>(ones.size()),generation,7905,child,0))];
            const int off = zeros[static_cast<std::size_t>(random.integer(
                0,static_cast<int>(zeros.size()),generation,7905,child,1))];
            decision[static_cast<std::size_t>(on)] = 0.0;
            decision[static_cast<std::size_t>(off)] = 1.0;
        }
        return;
    }
    const int coordinate = random.integer(
        0, static_cast<int>(decision.size()), generation,7905,child,0);
    if (mode == ConstraintMode::array) {
        if (coordinate <= 1) {
            decision[static_cast<std::size_t>(coordinate)] = 220.0
                + 480.0 * random.uniform(generation,7905,child,coordinate,1);
        } else if (coordinate <= 3) {
            decision[static_cast<std::size_t>(coordinate)] = 360.0
                * random.uniform(generation,7905,child,coordinate,1);
        } else {
            const int spacing = coordinate - 4;
            decision[static_cast<std::size_t>(coordinate)] =
                decision[static_cast<std::size_t>(spacing)]
                * random.uniform(generation,7905,child,coordinate,1);
        }
        return;
    }
    const int turbine = coordinate / 2;
    const double radius = std::sqrt(random.uniform(generation,7905,child,turbine,2));
    const double angle = 2.0 * kPi
        * random.uniform(generation,7905,child,turbine,3);
    decision[static_cast<std::size_t>(2 * turbine)] =
        problem.center.x_m + kDomainSemiMinorM * radius * std::cos(angle);
    decision[static_cast<std::size_t>(2 * turbine + 1)] =
        problem.center.y_m + kDomainSemiMajorM * radius * std::sin(angle);
}

RunResult run_ga(
    const Problem::Impl& problem,
    const RunConfig& config,
    fode::PersistentExecutor& executor,
    const Clock::time_point run_started
) {
    const fode::CounterRng random(config.seed);
    std::vector<Individual> population(static_cast<std::size_t>(config.population));
    executor.parallel_for(0, config.population, [&](const int individual) {
        population[static_cast<std::size_t>(individual)].decision = random_decision(
            problem, config.mode, random, 0, individual);
        repair_decision(problem,
            population[static_cast<std::size_t>(individual)].decision,
            config.mode, random, 0, 7906, individual);
    });
    double evaluator_seconds = 0.0;
    std::uint64_t physical_fes = 0;
    evaluate_batch(problem, population, config.mode, executor,
                   evaluator_seconds, physical_fes);
    double incumbent = objective(population[best_index(population)].evaluation);
    int stagnation = 0;
    int generations = 0;
    std::string reason = "maximum_generations";
    const int offspring_count = config.population - config.population / 5;
    for (int generation = 1; generation <= config.maximum_generations; ++generation) {
        std::vector<Individual> children(static_cast<std::size_t>(offspring_count));
        executor.parallel_for(0, offspring_count, [&](const int child) {
            const int first = roulette_parent(population,random,generation,child,0);
            const int second = roulette_parent(population,random,generation,child,1);
            const double parent_value = std::min(
                objective(population[static_cast<std::size_t>(first)].evaluation),
                objective(population[static_cast<std::size_t>(second)].evaluation));
            const double probability = adaptive_probability(population,parent_value,1.0);
            auto& decision = children[static_cast<std::size_t>(child)].decision;
            decision = population[static_cast<std::size_t>(first)].decision;
            if (random.uniform(generation,7907,child) < probability) {
                for (std::size_t coordinate = 0; coordinate < decision.size(); ++coordinate) {
                    if (random.uniform(generation,7907,child,coordinate,1) < 0.5) {
                        decision[coordinate] = population[
                            static_cast<std::size_t>(second)].decision[coordinate];
                    }
                }
            }
            repair_decision(problem,decision,config.mode,random,generation,7908,child);
        });
        evaluate_batch(problem, children, config.mode, executor,
                       evaluator_seconds, physical_fes);
        executor.parallel_for(0, offspring_count, [&](const int child) {
            Individual& value = children[static_cast<std::size_t>(child)];
            const double probability = adaptive_probability(
                population, objective(value.evaluation), 0.5);
            if (random.uniform(generation,7909,child) < probability) {
                mutate(problem,value.decision,config.mode,random,generation,child);
            }
            repair_decision(problem,value.decision,config.mode,
                            random,generation,7910,child);
        });
        evaluate_batch(problem, children, config.mode, executor,
                       evaluator_seconds, physical_fes);
        std::stable_sort(population.begin(), population.end(),
            [](const Individual& a, const Individual& b) {
                return objective(a.evaluation) < objective(b.evaluation);
            });
        std::stable_sort(children.begin(), children.end(),
            [](const Individual& a, const Individual& b) {
                return objective(a.evaluation) < objective(b.evaluation);
            });
        population.resize(static_cast<std::size_t>(config.population / 5));
        population.insert(population.end(),
                          std::make_move_iterator(children.begin()),
                          std::make_move_iterator(children.end()));
        generations = generation;
        const double current = objective(population[best_index(population)].evaluation);
        if (current + 1.0e-12 < incumbent) {
            incumbent = current;
            stagnation = 0;
        } else {
            ++stagnation;
        }
        if (config.enable_convergence) {
            const int diversity = diversity_count(population);
            double mean = 0.0;
            for (const Individual& value : population) mean += objective(value.evaluation);
            mean /= static_cast<double>(population.size());
            if (diversity <= config.population / 10) {
                reason = "diversity_at_most_10_percent";
                break;
            }
            if ((mean - incumbent) / incumbent <= 0.001) {
                reason = "paper_mean_best_ratio";
                break;
            }
            if (stagnation >= config.no_improvement_generations) {
                reason = "no_improvement_50_generations";
                break;
            }
        }
    }
    const Individual& best = population[best_index(population)];
    RunResult result;
    result.method_semantic_id = "l0079_adaptive_ga_three_encoding_declared_v1";
    result.optimizer = to_string(config.optimizer);
    result.constraint_mode = to_string(config.mode);
    result.candidate_profile = to_string(config.candidate_profile);
    result.case_id = "L0079_" + result.optimizer + "_" + result.constraint_mode;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = config.population;
    result.generations = generations;
    result.physical_fes = physical_fes;
    result.convergence_reason = reason;
    result.reference_evaluation = problem.evaluate(problem.as_built);
    result.best_evaluation = best.evaluation;
    result.best_layout = problem.decode(best.decision, config.mode);
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(run_started);
    result.algorithm_seconds = std::max(0.0,
        result.end_to_end_seconds - result.evaluator_seconds);
    return result;
}

RunResult run_pso(
    const Problem::Impl& problem,
    const RunConfig& config,
    fode::PersistentExecutor& executor,
    const Clock::time_point run_started
) {
    const fode::CounterRng random(config.seed);
    const int dimension = decision_dimension(problem, config.mode);
    std::vector<Individual> population(static_cast<std::size_t>(config.population));
    std::vector<std::vector<double>> velocity(
        static_cast<std::size_t>(config.population),
        std::vector<double>(static_cast<std::size_t>(dimension), 0.0));
    executor.parallel_for(0, config.population, [&](const int individual) {
        auto& decision = population[static_cast<std::size_t>(individual)].decision;
        decision = random_decision(problem,config.mode,random,0,individual);
        repair_decision(problem,decision,config.mode,random,0,7911,individual);
    });
    double evaluator_seconds = 0.0;
    std::uint64_t physical_fes = 0;
    evaluate_batch(problem,population,config.mode,executor,
                   evaluator_seconds,physical_fes);
    std::vector<Individual> personal_best = population;
    double incumbent = objective(personal_best[best_index(personal_best)].evaluation);
    int stagnation = 0;
    int generations = 0;
    std::string reason = "maximum_generations";
    for (int generation = 1; generation <= config.maximum_generations; ++generation) {
        const std::vector<double> global = personal_best[
            best_index(personal_best)].decision;
        const double inertia = 0.9 - 0.5
            * static_cast<double>(generation - 1)
            / static_cast<double>(std::max(1, config.maximum_generations - 1));
        executor.parallel_for(0, config.population, [&](const int particle) {
            auto& position = population[static_cast<std::size_t>(particle)].decision;
            auto& particle_velocity = velocity[static_cast<std::size_t>(particle)];
            const auto& personal = personal_best[static_cast<std::size_t>(particle)].decision;
            if (config.mode == ConstraintMode::binary) {
                std::vector<double> scores(position.size());
                for (int coordinate = 0; coordinate < dimension; ++coordinate) {
                    const std::size_t c = static_cast<std::size_t>(coordinate);
                    particle_velocity[c] = inertia * particle_velocity[c]
                        + 2.0 * random.uniform(generation,7912,particle,coordinate,0)
                            * (personal[c] - position[c])
                        + 2.0 * random.uniform(generation,7912,particle,coordinate,1)
                            * (global[c] - position[c]);
                    const double transfer = 2.0 / kPi
                        * std::atan(std::abs(particle_velocity[c]) * kPi / 2.0);
                    const bool flip = random.uniform(
                        generation,7912,particle,coordinate,2) < transfer;
                    scores[c] = flip ? 1.0 - position[c] : position[c];
                    scores[c] += 1.0e-9 * random.uniform(
                        generation,7912,particle,coordinate,3);
                }
                position = std::move(scores);
            } else {
                for (int coordinate = 0; coordinate < dimension; ++coordinate) {
                    const std::size_t c = static_cast<std::size_t>(coordinate);
                    const double range = config.mode == ConstraintMode::array
                        ? (coordinate <= 1 ? 725.0
                           : coordinate <= 3 ? 360.0 : 900.0)
                        : (coordinate % 2 == 0
                           ? 2.0 * kDomainSemiMinorM
                           : 2.0 * kDomainSemiMajorM);
                    particle_velocity[c] = inertia * particle_velocity[c]
                        + 2.0 * random.uniform(generation,7912,particle,coordinate,0)
                            * (personal[c] - position[c])
                        + 2.0 * random.uniform(generation,7912,particle,coordinate,1)
                            * (global[c] - position[c]);
                    particle_velocity[c] = std::clamp(
                        particle_velocity[c], -range, range);
                    position[c] += particle_velocity[c];
                }
            }
            repair_decision(problem,position,config.mode,random,
                            generation,7913,particle);
        });
        evaluate_batch(problem,population,config.mode,executor,
                       evaluator_seconds,physical_fes);
        for (int particle = 0; particle < config.population; ++particle) {
            if (objective(population[static_cast<std::size_t>(particle)].evaluation)
                < objective(personal_best[static_cast<std::size_t>(particle)].evaluation)) {
                personal_best[static_cast<std::size_t>(particle)] =
                    population[static_cast<std::size_t>(particle)];
            }
        }
        generations = generation;
        const double current = objective(personal_best[best_index(personal_best)].evaluation);
        if (current + 1.0e-12 < incumbent) {
            incumbent = current;
            stagnation = 0;
        } else {
            ++stagnation;
        }
        if (config.enable_convergence) {
            if (diversity_count(personal_best) <= config.population / 10) {
                reason = "diversity_at_most_10_percent";
                break;
            }
            if (stagnation >= config.no_improvement_generations) {
                reason = "no_improvement_50_generations";
                break;
            }
        }
    }
    const Individual& best = personal_best[best_index(personal_best)];
    RunResult result;
    result.method_semantic_id = "l0079_gbest_pso_three_encoding_declared_v1";
    result.optimizer = to_string(config.optimizer);
    result.constraint_mode = to_string(config.mode);
    result.candidate_profile = to_string(config.candidate_profile);
    result.case_id = "L0079_" + result.optimizer + "_" + result.constraint_mode;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = config.population;
    result.generations = generations;
    result.physical_fes = physical_fes;
    result.convergence_reason = reason;
    result.reference_evaluation = problem.evaluate(problem.as_built);
    result.best_evaluation = best.evaluation;
    result.best_layout = problem.decode(best.decision, config.mode);
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(run_started);
    result.algorithm_seconds = std::max(0.0,
        result.end_to_end_seconds - result.evaluator_seconds);
    return result;
}

std::uint64_t scientific_hash(const RunResult& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : result.best_layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
        result.best_evaluation.lcoe_gbp_per_mwh));
    hash = mix_hash(hash, result.physical_fes);
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.generations));
    return hash;
}

}  // namespace

Problem::Problem(const CandidateProfile profile)
    : impl_(std::make_unique<Impl>(profile)) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

int Problem::turbine_count() const noexcept { return kTurbines; }
int Problem::wind_state_count() const noexcept { return kWindStates; }
int Problem::candidate_count() const noexcept {
    return static_cast<int>(impl_->candidates.size());
}
double Problem::minimum_spacing_m() const noexcept { return kMinimumSpacingM; }
double Problem::domain_area_km2() const noexcept { return kDomainAreaKm2; }
CandidateProfile Problem::candidate_profile() const noexcept { return impl_->profile; }
const std::vector<Point>& Problem::as_built_layout() const noexcept {
    return impl_->as_built;
}
const std::vector<Point>& Problem::candidate_positions() const noexcept {
    return impl_->candidates;
}
Evaluation Problem::evaluate_layout(const std::vector<Point>& layout) const {
    return impl_->evaluate(layout);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0 || config.population <= 1
        || config.maximum_generations <= 0
        || config.no_improvement_generations <= 0
        || config.candidate_profile != problem.impl_->profile) {
        throw std::invalid_argument("L0079 run configuration invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    RunResult result = config.optimizer == Optimizer::adaptive_ga
        ? run_ga(*problem.impl_, config, executor, started)
        : run_pso(*problem.impl_, config, executor, started);
    const fode::ExecutorWorkReceipt receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    result.scientific_hash = scientific_hash(result);
    return result;
}

std::string to_string(const Optimizer value) {
    return value == Optimizer::adaptive_ga ? "adaptive_ga" : "gbest_pso";
}
std::string to_string(const ConstraintMode value) {
    if (value == ConstraintMode::array) return "array";
    if (value == ConstraintMode::binary) return "binary";
    return "continuous";
}
std::string to_string(const CandidateProfile value) {
    return value == CandidateProfile::journal_628
        ? "journal_628" : "thesis_658";
}

}  // namespace core99::l0079
