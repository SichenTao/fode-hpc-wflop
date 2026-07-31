/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T60 pure-C++ incremental Jensen evaluator and improved RS
Paper/DOI: Solving the Wind Farm Layout Optimization Problem Using Random
Search Algorithm; 10.1016/j.renene.2015.01.005
Public source, missing/conflicting fields, and every deterministic completion:
hpc/core99_cpp/include/core99/feng_t60.hpp
Method/problem semantic IDs: t60_improved_rs_incremental_v1;
t60_ideal_continuous_jensen_v1; t60_hornsrev_jensen_v80_v1
Controlling contract: shared/contracts/core99_t60_feng_shen_2015.json
HPC design: profile lookup removes speed-bin integration from the hot path;
one moved turbine updates the affected tensor row and column in O(S*N), versus
O(S*N*N) full recomputation; fixed-order direction reductions preserve
scientific identity; independent trajectories provide full-core parallelism.
Claim boundary: declared flexible academic reproduction, not author source,
random-state or exact-number replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/feng_t60.hpp"

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

namespace core99::t60 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kDiameterM = 80.0;
constexpr double kRotorRadiusM = 40.0;
constexpr double kMinimumSpacingM = 5.0 * kDiameterM;
constexpr double kIdealDomainM = 4000.0;
constexpr double kIdealHubHeightM = 60.0;
constexpr double kIdealRoughnessM = 0.3;
constexpr double kHornsHubHeightM = 70.0;
constexpr double kHornsReferenceHeightM = 62.0;
constexpr double kHornsRoughnessM = 0.0002;
constexpr double kBasisAxM = 560.0;
constexpr double kBasisAyM = 0.0;
constexpr double kBasisBxM = 478.0 / 7.0;
constexpr double kBasisByM = 3891.0 / 7.0;
constexpr int kLookupBins = 32768;
constexpr double kLookupMaximumQ = 80.0;
constexpr const char* kMethodId = "t60_improved_rs_incremental_v1";

constexpr std::array<double, 12> kMeasuredScale{
    8.71, 9.36, 9.29, 10.27, 10.89, 10.49,
    10.94, 11.23, 11.93, 11.94, 12.17, 10.31
};
constexpr std::array<double, 12> kMeasuredShape{
    2.08, 2.22, 2.41, 2.37, 2.51, 2.75,
    2.61, 2.51, 2.33, 2.35, 2.58, 2.01
};
constexpr std::array<double, 12> kMeasuredFrequency{
    3.8, 4.3, 5.5, 8.3, 8.7, 6.7,
    8.4, 10.5, 11.4, 12.2, 13.9, 6.1
};
constexpr std::array<double, 12> kConstructedFrequency{
    5.0, 5.0, 5.0, 5.0, 5.0, 5.0,
    5.0, 5.0, 5.0, 5.0, 45.0, 5.0
};
constexpr std::array<double, 9> kTailDirectionWeights{
    1.549, 1.841, 2.132, 3.395, 4.029, 3.395, 2.132, 1.841, 1.549
};
constexpr std::array<std::array<double, 3>, 9> kTailSpeedWeights{{
    {0.836, 0.578, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 1.161, 0.135},
    {0.836, 1.128, 1.431},
    {0.836, 1.762, 1.431},
    {0.836, 1.128, 1.431},
    {0.836, 1.161, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 0.578, 0.135},
}};
constexpr std::array<double, 23> kV80Speed{
    3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25
};
constexpr std::array<double, 23> kV80PowerKw{
    0,66.6,154,282,460,696,996,1341,1661,1866,1958,1988,
    1997,1999,2000,2000,2000,2000,2000,2000,2000,2000,2000
};
constexpr std::array<double, 23> kV80Ct{
    0,0.818,0.806,0.804,0.805,0.806,0.807,0.793,0.739,0.709,
    0.409,0.314,0.249,0.202,0.167,0.14,0.119,0.102,0.088,
    0.077,0.067,0.06,0.053
};

struct SpeedBin {
    double speed_mps = 0.0;
    double weight = 0.0;
};

struct WindDirection {
    double degrees = 0.0;
    double sine = 0.0;
    double cosine = 1.0;
    int profile = 0;
    double direction_weight = 1.0;
};

struct Profile {
    std::vector<SpeedBin> speeds;
    std::vector<double> lookup_kw;
};

double interpolate(
    const std::array<double, 23>& x,
    const std::array<double, 23>& y,
    const double value
) {
    if (value < x.front() || value > x.back()) return 0.0;
    const auto upper = std::lower_bound(x.begin(), x.end(), value);
    if (upper == x.begin()) return y.front();
    if (upper == x.end()) return y.back();
    const std::size_t hi = static_cast<std::size_t>(upper - x.begin());
    const std::size_t lo = hi - 1U;
    const double fraction = (value - x[lo]) / (x[hi] - x[lo]);
    return std::lerp(y[lo], y[hi], fraction);
}

double ideal_power_kw(const double speed) {
    if (!(speed > 0.0)) return 0.0;
    return std::min(0.3 * speed * speed * speed, 630.0);
}

double turbine_power_kw(const bool horns, const double speed) {
    return horns
        ? interpolate(kV80Speed, kV80PowerKw, speed)
        : ideal_power_kw(speed);
}

double thrust_coefficient(const bool horns, const double speed) {
    return horns ? interpolate(kV80Speed, kV80Ct, speed) : 0.88;
}

double weibull_cdf(
    const double speed,
    const double scale,
    const double shape
) {
    if (!(speed > 0.0)) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

std::vector<SpeedBin> weibull_bins(
    const double scale,
    const double shape
) {
    std::vector<SpeedBin> bins;
    double total = 0.0;
    for (int index = 0; index <= 60; ++index) {
        const double speed = 0.5 * static_cast<double>(index);
        const double low = std::max(0.0, speed - 0.25);
        const double high = speed + 0.25;
        const double weight =
            weibull_cdf(high, scale, shape)
            - weibull_cdf(low, scale, shape);
        bins.push_back({speed, weight});
        total += weight;
    }
    for (auto& bin : bins) bin.weight /= total;
    return bins;
}

double circle_overlap(
    const double distance,
    const double first_radius,
    const double second_radius
) {
    if (distance >= first_radius + second_radius) return 0.0;
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return kPi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (
            distance * distance + first_radius * first_radius
            - second_radius * second_radius
        ) / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (
            distance * distance + second_radius * second_radius
            - first_radius * first_radius
        ) / (2.0 * distance * second_radius),
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
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radicand);
}

class OverlapFractionLookup {
public:
    static constexpr int kWakeBins = 1024;
    static constexpr int kCrossBins = 1024;
    static constexpr double kMaximumWakeRatio = 16.0;
    static constexpr double kMaximumCrossRatio = 17.0;

    OverlapFractionLookup()
        : values_(
            static_cast<std::size_t>(
                (kWakeBins + 1) * (kCrossBins + 1)
            ),
            0.0F
        ) {
        const double rotor_area = kPi * kRotorRadiusM * kRotorRadiusM;
        for (int wake_bin = 0; wake_bin <= kWakeBins; ++wake_bin) {
            const double wake_ratio = 1.0
                + (kMaximumWakeRatio - 1.0)
                    * static_cast<double>(wake_bin)
                    / static_cast<double>(kWakeBins);
            const double wake_radius = wake_ratio * kRotorRadiusM;
            for (int cross_bin = 0; cross_bin <= kCrossBins; ++cross_bin) {
                const double cross_ratio = kMaximumCrossRatio
                    * static_cast<double>(cross_bin)
                    / static_cast<double>(kCrossBins);
                values_[index(wake_bin, cross_bin)] = static_cast<float>(
                    circle_overlap(
                        cross_ratio * kRotorRadiusM,
                        wake_radius,
                        kRotorRadiusM
                    ) / rotor_area
                );
            }
        }
    }

    [[nodiscard]] double operator()(
        const double wake_radius,
        const double crosswind
    ) const {
        const double wake_ratio = wake_radius / kRotorRadiusM;
        const double cross_ratio = crosswind / kRotorRadiusM;
        if (cross_ratio >= wake_ratio + 1.0) return 0.0;
        if (cross_ratio <= wake_ratio - 1.0) return 1.0;
        const double wake_position = std::clamp(
            (wake_ratio - 1.0) / (kMaximumWakeRatio - 1.0)
                * static_cast<double>(kWakeBins),
            0.0,
            static_cast<double>(kWakeBins)
        );
        const double cross_position = std::clamp(
            cross_ratio / kMaximumCrossRatio
                * static_cast<double>(kCrossBins),
            0.0,
            static_cast<double>(kCrossBins)
        );
        const int wake_low = static_cast<int>(wake_position);
        const int cross_low = static_cast<int>(cross_position);
        const int wake_high = std::min(wake_low + 1, kWakeBins);
        const int cross_high = std::min(cross_low + 1, kCrossBins);
        const double wake_fraction =
            wake_position - static_cast<double>(wake_low);
        const double cross_fraction =
            cross_position - static_cast<double>(cross_low);
        const double low = std::lerp(
            static_cast<double>(values_[index(wake_low, cross_low)]),
            static_cast<double>(values_[index(wake_high, cross_low)]),
            wake_fraction
        );
        const double high = std::lerp(
            static_cast<double>(values_[index(wake_low, cross_high)]),
            static_cast<double>(values_[index(wake_high, cross_high)]),
            wake_fraction
        );
        return std::lerp(low, high, cross_fraction);
    }

private:
    std::vector<float> values_;

    static std::size_t index(const int wake_bin, const int cross_bin) {
        return static_cast<std::size_t>(
            wake_bin * (kCrossBins + 1) + cross_bin
        );
    }
};

const OverlapFractionLookup& overlap_lookup() {
    static const OverlapFractionLookup lookup;
    return lookup;
}

std::uint64_t result_hash(
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        hash ^= std::bit_cast<std::uint64_t>(point.x_m);
        hash *= 1099511628211ULL;
        hash ^= std::bit_cast<std::uint64_t>(point.y_m);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(evaluation.expected_power_kw);
    hash *= 1099511628211ULL;
    return hash;
}

std::vector<Point> grid_layout(
    const std::vector<std::pair<int, int>>& occupied
) {
    std::vector<Point> result;
    result.reserve(occupied.size());
    for (const auto& [column, row] : occupied) {
        result.push_back({
            (static_cast<double>(column) + 0.5) * 400.0,
            (static_cast<double>(row) + 0.5) * 400.0,
        });
    }
    return result;
}

std::vector<Point> ideal_case_1_layout() {
    std::vector<std::pair<int, int>> occupied;
    for (int row : {0, 4, 9}) {
        for (int column = 0; column < 10; ++column) {
            occupied.emplace_back(column, row);
        }
    }
    return grid_layout(occupied);
}

std::vector<Point> ideal_case_2_layout() {
    return grid_layout({
        {0,0},{1,0},{3,0},{5,0},{7,0},{9,0},
        {4,1},{7,1},{9,1},{0,2},{3,2},{6,2},
        {2,3},{5,3},{7,3},{9,3},{1,4},{4,4},{8,4},
        {0,5},{3,5},{6,5},{7,5},{2,6},{5,6},{9,6},
        {0,7},{3,7},{6,7},{8,7},{0,8},{7,8},
        {0,9},{2,9},{3,9},{4,9},{5,9},{7,9},{9,9},
    });
}

std::vector<Point> ideal_case_3_layout() {
    return grid_layout({
        {0,0},{2,0},{4,0},{6,0},{7,0},{9,0},
        {2,1},{6,1},{8,1},{0,2},{4,2},{9,2},
        {0,3},{2,3},{6,3},{9,3},{0,4},{4,4},{8,4},
        {0,5},{4,5},{5,5},{9,5},{0,6},{2,6},{7,6},
        {0,7},{4,7},{5,7},{9,7},{0,8},{8,8},
        {0,9},{2,9},{3,9},{4,9},{6,9},{7,9},{9,9},
    });
}

std::vector<Point> horns_layout() {
    constexpr std::array<double, 8> row_x{
        0.0,68.0,137.0,205.0,273.0,341.0,410.0,478.0
    };
    constexpr std::array<double, 8> row_y{
        0.0,556.0,1112.0,1668.0,2223.0,2779.0,3335.0,3891.0
    };
    constexpr std::array<double, 10> column_x{
        0.0,560.0,1120.0,1680.0,2240.0,
        2800.0,3360.0,3920.0,4480.0,5040.0
    };
    std::vector<Point> result;
    result.reserve(80);
    for (int column = 0; column < 10; ++column) {
        for (int row = 0; row < 8; ++row) {
            result.push_back({
                column_x[static_cast<std::size_t>(column)]
                    + row_x[static_cast<std::size_t>(row)],
                row_y[static_cast<std::size_t>(row)],
            });
        }
    }
    return result;
}

}  // namespace

struct Problem::Impl {
    std::string id;
    std::string semantic_id;
    bool horns = false;
    bool constructed_wind = false;
    bool enlarged_boundary = false;
    int turbines = 0;
    int direction_sectors = 0;
    double wake_decay = 0.0;
    double long_edge = 0.0;
    double direction_rotation = 0.0;
    double scale_multiplier = 1.0;
    double shape_multiplier = 1.0;
    std::vector<WindDirection> directions;
    std::vector<Profile> profiles;
    double no_wake_power_kw = 0.0;

    [[nodiscard]] std::pair<double, double> affine_uv(
        const Point& point
    ) const {
        const double determinant =
            kBasisAxM * kBasisByM - kBasisAyM * kBasisBxM;
        return {
            (point.x_m * kBasisByM - point.y_m * kBasisBxM)
                / determinant,
            (kBasisAxM * point.y_m - kBasisAyM * point.x_m)
                / determinant,
        };
    }

    [[nodiscard]] Point affine_point(
        const double u,
        const double v
    ) const {
        return {
            u * kBasisAxM + v * kBasisBxM,
            u * kBasisAyM + v * kBasisByM,
        };
    }

    [[nodiscard]] bool inside(const Point& point) const {
        if (!horns) {
            return point.x_m >= 0.0 && point.x_m <= kIdealDomainM
                && point.y_m >= 0.0 && point.y_m <= kIdealDomainM;
        }
        const auto [u, v] = affine_uv(point);
        const double extension = enlarged_boundary ? 0.5 : 0.0;
        // The public UTM coordinates are rounded to integer metres, so their
        // affine outer rows differ from the exact paper parallelogram by less
        // than one metre. This tolerance admits those source coordinates only.
        constexpr double coordinate_rounding_tolerance = 0.001;
        return u >= -extension - coordinate_rounding_tolerance
            && u <= 9.0 + extension + coordinate_rounding_tolerance
            && v >= -extension - coordinate_rounding_tolerance
            && v <= 7.0 + extension + coordinate_rounding_tolerance;
    }

    [[nodiscard]] double constraint_violation(
        const std::vector<Point>& layout
    ) const {
        double violation = std::abs(
            static_cast<double>(layout.size())
            - static_cast<double>(turbines)
        ) * kMinimumSpacingM;
        for (const Point& point : layout) {
            if (!inside(point)) violation = std::max(violation, 1.0);
        }
        for (std::size_t left = 0; left < layout.size(); ++left) {
            for (
                std::size_t right = left + 1U;
                right < layout.size();
                ++right
            ) {
                violation = std::max(
                    violation,
                    std::max(
                        0.0,
                        kMinimumSpacingM
                            - std::hypot(
                                layout[left].x_m - layout[right].x_m,
                                layout[left].y_m - layout[right].y_m
                            )
                    )
                );
            }
        }
        return violation;
    }

    [[nodiscard]] bool moved_turbine_feasible(
        const std::vector<Point>& layout,
        const int moved
    ) const {
        const Point& point = layout[static_cast<std::size_t>(moved)];
        if (!inside(point)) return false;
        for (int other = 0; other < turbines; ++other) {
            if (other == moved) continue;
            if (
                std::hypot(
                    point.x_m - layout[static_cast<std::size_t>(other)].x_m,
                    point.y_m - layout[static_cast<std::size_t>(other)].y_m
                ) < kMinimumSpacingM
            ) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] double influence(
        const Point& source,
        const Point& target,
        const WindDirection& direction
    ) const {
        const double dx = target.x_m - source.x_m;
        const double dy = target.y_m - source.y_m;
        const double downstream =
            direction.sine * dx + direction.cosine * dy;
        if (!(downstream > 0.0)) return 0.0;
        const double crosswind = std::abs(
            direction.cosine * dx - direction.sine * dy
        );
        const double wake_radius =
            kRotorRadiusM + wake_decay * downstream;
        const double fraction = overlap_lookup()(wake_radius, crosswind);
        const double expansion =
            1.0 + wake_decay * downstream / kRotorRadiusM;
        return fraction * fraction
            / (expansion * expansion * expansion * expansion);
    }

    [[nodiscard]] double profile_power(
        const int profile_index,
        const double q
    ) const {
        const auto& lookup =
            profiles[static_cast<std::size_t>(profile_index)].lookup_kw;
        const double position = std::clamp(
            std::sqrt(std::max(0.0, q) / kLookupMaximumQ)
                * static_cast<double>(kLookupBins),
            0.0,
            static_cast<double>(kLookupBins)
        );
        const int lower = static_cast<int>(position);
        const int upper = std::min(lower + 1, kLookupBins);
        return std::lerp(
            lookup[static_cast<std::size_t>(lower)],
            lookup[static_cast<std::size_t>(upper)],
            position - static_cast<double>(lower)
        );
    }

    [[nodiscard]] std::vector<double> full_qsum(
        const std::vector<Point>& layout
    ) const {
        std::vector<double> qsum(
            static_cast<std::size_t>(direction_sectors * turbines),
            0.0
        );
        for (int direction = 0; direction < direction_sectors; ++direction) {
            for (int target = 0; target < turbines; ++target) {
                double sum = 0.0;
                for (int source = 0; source < turbines; ++source) {
                    if (source == target) continue;
                    sum += influence(
                        layout[static_cast<std::size_t>(source)],
                        layout[static_cast<std::size_t>(target)],
                        directions[static_cast<std::size_t>(direction)]
                    );
                }
                qsum[static_cast<std::size_t>(
                    direction * turbines + target
                )] = sum;
            }
        }
        return qsum;
    }

    [[nodiscard]] std::vector<double> incremental_qsum(
        const std::vector<Point>& current,
        const std::vector<Point>& candidate,
        const int moved,
        const std::vector<double>& current_qsum
    ) const {
        std::vector<double> result = current_qsum;
        for (int direction = 0; direction < direction_sectors; ++direction) {
            const auto& wind =
                directions[static_cast<std::size_t>(direction)];
            for (int target = 0; target < turbines; ++target) {
                const std::size_t index = static_cast<std::size_t>(
                    direction * turbines + target
                );
                if (target == moved) {
                    double sum = 0.0;
                    for (int source = 0; source < turbines; ++source) {
                        if (source == moved) continue;
                        sum += influence(
                            current[static_cast<std::size_t>(source)],
                            candidate[static_cast<std::size_t>(moved)],
                            wind
                        );
                    }
                    result[index] = sum;
                } else {
                    result[index] += influence(
                        candidate[static_cast<std::size_t>(moved)],
                        current[static_cast<std::size_t>(target)],
                        wind
                    ) - influence(
                        current[static_cast<std::size_t>(moved)],
                        current[static_cast<std::size_t>(target)],
                        wind
                    );
                    result[index] = std::max(0.0, result[index]);
                }
            }
        }
        return result;
    }

    [[nodiscard]] Evaluation evaluation_from_qsum(
        const std::vector<Point>& layout,
        const std::vector<double>& qsum,
        const bool known_feasible = false
    ) const {
        Evaluation evaluation;
        evaluation.constraint_violation_m = known_feasible
            ? 0.0 : constraint_violation(layout);
        evaluation.feasible =
            known_feasible || evaluation.constraint_violation_m <= 1.0e-10;
        evaluation.no_wake_power_kw = no_wake_power_kw;
        if (layout.size() != static_cast<std::size_t>(turbines)) {
            return evaluation;
        }
        double total = 0.0;
        for (int direction = 0; direction < direction_sectors; ++direction) {
            const auto& wind =
                directions[static_cast<std::size_t>(direction)];
            double direction_power = 0.0;
            for (int target = 0; target < turbines; ++target) {
                direction_power += profile_power(
                    wind.profile,
                    qsum[static_cast<std::size_t>(
                        direction * turbines + target
                    )]
                );
            }
            total += wind.direction_weight * direction_power;
        }
        evaluation.expected_power_kw = total;
        evaluation.efficiency = total / std::max(no_wake_power_kw, 1.0e-300);
        return evaluation;
    }

    void build_lookup() {
        for (Profile& profile : profiles) {
            profile.lookup_kw.resize(
                static_cast<std::size_t>(kLookupBins + 1)
            );
            for (int bin = 0; bin <= kLookupBins; ++bin) {
                const double root_q = std::sqrt(kLookupMaximumQ)
                    * static_cast<double>(bin)
                    / static_cast<double>(kLookupBins);
                double value = 0.0;
                for (const SpeedBin& speed_bin : profile.speeds) {
                    const double ct = thrust_coefficient(
                        horns, speed_bin.speed_mps
                    );
                    const double coefficient =
                        1.0 - std::sqrt(std::max(0.0, 1.0 - ct));
                    const double effective = speed_bin.speed_mps
                        * std::max(0.0, 1.0 - coefficient * root_q);
                    value += speed_bin.weight
                        * turbine_power_kw(horns, effective);
                }
                profile.lookup_kw[static_cast<std::size_t>(bin)] = value;
            }
        }
        no_wake_power_kw = 0.0;
        for (const WindDirection& direction : directions) {
            no_wake_power_kw += direction.direction_weight
                * static_cast<double>(turbines)
                * profile_power(direction.profile, 0.0);
        }
    }
};

Problem::Problem(
    const std::string& problem_id,
    const int direction_sectors,
    const double direction_rotation_degrees,
    const double weibull_scale_multiplier,
    const double weibull_shape_multiplier
) : impl_(std::make_unique<Impl>()) {
    impl_->id = problem_id;
    impl_->direction_rotation = direction_rotation_degrees;
    impl_->scale_multiplier = weibull_scale_multiplier;
    impl_->shape_multiplier = weibull_shape_multiplier;
    impl_->horns = problem_id.starts_with("t60_horns_case");
    if (
        !impl_->horns
        && problem_id != "t60_ideal_case1"
        && problem_id != "t60_ideal_case2"
        && problem_id != "t60_ideal_case3"
    ) {
        throw std::invalid_argument("unknown T60 problem: " + problem_id);
    }
    if (
        impl_->horns
        && problem_id != "t60_horns_case1"
        && problem_id != "t60_horns_case2"
        && problem_id != "t60_horns_case3"
    ) {
        throw std::invalid_argument("unknown T60 Horns problem: " + problem_id);
    }
    if (
        direction_sectors <= 0
        || 360 % direction_sectors != 0
        || weibull_scale_multiplier <= 0.0
        || weibull_shape_multiplier <= 0.0
    ) {
        throw std::invalid_argument("invalid T60 wind completion");
    }
    impl_->semantic_id = impl_->horns
        ? "t60_hornsrev_jensen_v80_v1"
        : "t60_ideal_continuous_jensen_v1";
    impl_->turbines = impl_->horns
        ? 80
        : (problem_id == "t60_ideal_case1" ? 30 : 39);
    impl_->constructed_wind = problem_id == "t60_horns_case2";
    impl_->enlarged_boundary = problem_id == "t60_horns_case3";
    impl_->direction_sectors = impl_->horns ? direction_sectors
                                           : (problem_id == "t60_ideal_case1"
                                                  ? 1 : 36);
    impl_->wake_decay = 0.5 / std::log(
        (impl_->horns ? kHornsHubHeightM : kIdealHubHeightM)
        / (impl_->horns ? kHornsRoughnessM : kIdealRoughnessM)
    );
    impl_->long_edge = impl_->horns
        ? std::max(
            std::hypot(9.0 * kBasisAxM, 9.0 * kBasisAyM),
            std::hypot(7.0 * kBasisBxM, 7.0 * kBasisByM)
        ) + (impl_->enlarged_boundary ? 7.0 * kDiameterM : 0.0)
        : kIdealDomainM;

    if (!impl_->horns) {
        if (problem_id != "t60_ideal_case3") {
            impl_->profiles.push_back({{{12.0, 1.0}}, {}});
            const double direction_weight =
                1.0 / static_cast<double>(impl_->direction_sectors);
            for (
                int direction = 0;
                direction < impl_->direction_sectors;
                ++direction
            ) {
                impl_->directions.push_back({
                    10.0 * static_cast<double>(direction),
                    std::sin(
                        (
                            10.0 * static_cast<double>(direction)
                            + impl_->direction_rotation
                        ) * kPi / 180.0
                    ),
                    std::cos(
                        (
                            10.0 * static_cast<double>(direction)
                            + impl_->direction_rotation
                        ) * kPi / 180.0
                    ),
                    0,
                    direction_weight,
                });
            }
        } else {
            double raw_total = 27.0 * (0.836 + 0.292 + 0.135)
                + std::accumulate(
                    kTailDirectionWeights.begin(),
                    kTailDirectionWeights.end(),
                    0.0
                );
            for (int direction = 0; direction < 36; ++direction) {
                const auto weights = direction < 27
                    ? std::array<double, 3>{0.836, 0.292, 0.135}
                    : kTailSpeedWeights[
                          static_cast<std::size_t>(direction - 27)
                      ];
                const double total =
                    weights[0] + weights[1] + weights[2];
                impl_->profiles.push_back({
                    {
                        {8.0, weights[0] / total},
                        {12.0, weights[1] / total},
                        {17.0, weights[2] / total},
                    },
                    {},
                });
                impl_->directions.push_back({
                    10.0 * static_cast<double>(direction),
                    std::sin(
                        (
                            10.0 * static_cast<double>(direction)
                            + impl_->direction_rotation
                        ) * kPi / 180.0
                    ),
                    std::cos(
                        (
                            10.0 * static_cast<double>(direction)
                            + impl_->direction_rotation
                        ) * kPi / 180.0
                    ),
                    direction,
                    total / raw_total,
                });
            }
        }
    } else {
        const auto& frequencies = impl_->constructed_wind
            ? kConstructedFrequency : kMeasuredFrequency;
        double frequency_total = std::accumulate(
            frequencies.begin(), frequencies.end(), 0.0
        );
        for (int sector = 0; sector < 12; ++sector) {
            const double scale =
                kMeasuredScale[static_cast<std::size_t>(sector)]
                * impl_->scale_multiplier;
            const double shape =
                kMeasuredShape[static_cast<std::size_t>(sector)]
                * impl_->shape_multiplier;
            impl_->profiles.push_back({
                weibull_bins(
                    scale
                    * std::log(kHornsHubHeightM / kHornsRoughnessM)
                    / std::log(kHornsReferenceHeightM / kHornsRoughnessM),
                    shape
                ),
                {},
            });
        }
        const int subdivisions = impl_->direction_sectors / 12;
        for (
            int direction = 0;
            direction < impl_->direction_sectors;
            ++direction
        ) {
            const int sector = direction / subdivisions;
            const double degrees =
                360.0 * static_cast<double>(direction)
                / static_cast<double>(impl_->direction_sectors);
            const double angle =
                (degrees + impl_->direction_rotation) * kPi / 180.0;
            impl_->directions.push_back({
                degrees,
                std::sin(angle),
                std::cos(angle),
                sector,
                frequencies[static_cast<std::size_t>(sector)]
                    / frequency_total / static_cast<double>(subdivisions),
            });
        }
    }
    impl_->build_lookup();
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const std::string& Problem::id() const noexcept { return impl_->id; }
const std::string& Problem::semantic_id() const noexcept {
    return impl_->semantic_id;
}
int Problem::turbine_count() const noexcept { return impl_->turbines; }
int Problem::direction_count() const noexcept {
    return impl_->direction_sectors;
}
double Problem::long_edge_m() const noexcept { return impl_->long_edge; }
double Problem::minimum_spacing_m() const noexcept {
    return kMinimumSpacingM;
}

Evaluation Problem::evaluate_full(
    const std::vector<Point>& layout
) const {
    return impl_->evaluation_from_qsum(layout, impl_->full_qsum(layout));
}

Evaluation Problem::evaluate_parallel(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    const double violation = impl_->constraint_violation(layout);
    if (layout.size() != static_cast<std::size_t>(impl_->turbines)) {
        return {
            0.0,
            impl_->no_wake_power_kw,
            0.0,
            violation,
            false,
        };
    }
    std::vector<double> partial(
        static_cast<std::size_t>(impl_->direction_sectors),
        0.0
    );
    executor.parallel_for(
        0,
        impl_->direction_sectors,
        [&](const int direction) {
            const auto& wind =
                impl_->directions[static_cast<std::size_t>(direction)];
            double value = 0.0;
            for (int target = 0; target < impl_->turbines; ++target) {
                double q = 0.0;
                for (int source = 0; source < impl_->turbines; ++source) {
                    if (source == target) continue;
                    q += impl_->influence(
                        layout[static_cast<std::size_t>(source)],
                        layout[static_cast<std::size_t>(target)],
                        wind
                    );
                }
                value += impl_->profile_power(wind.profile, q);
            }
            partial[static_cast<std::size_t>(direction)] =
                wind.direction_weight * value;
        }
    );
    const double power = std::accumulate(
        partial.begin(), partial.end(), 0.0
    );
    return {
        power,
        impl_->no_wake_power_kw,
        power / std::max(impl_->no_wake_power_kw, 1.0e-300),
        violation,
        violation <= 1.0e-10,
    };
}

Evaluation Problem::evaluate_incremental_candidate(
    const std::vector<Point>& current,
    const std::vector<Point>& candidate,
    const int moved_turbine
) const {
    if (
        moved_turbine < 0
        || moved_turbine >= impl_->turbines
        || current.size() != candidate.size()
    ) {
        throw std::invalid_argument("invalid T60 incremental candidate");
    }
    const auto qsum = impl_->full_qsum(current);
    return impl_->evaluation_from_qsum(
        candidate,
        impl_->incremental_qsum(
            current, candidate, moved_turbine, qsum
        )
    );
}

std::vector<Point> Problem::paper_initial_layout() const {
    if (impl_->id == "t60_ideal_case1") return ideal_case_1_layout();
    if (impl_->id == "t60_ideal_case2") return ideal_case_2_layout();
    if (impl_->id == "t60_ideal_case3") return ideal_case_3_layout();
    return horns_layout();
}

std::vector<Point> Problem::random_feasible_layout(
    const std::uint64_t seed
) const {
    const fode::CounterRng random(seed);
    if (impl_->horns) {
        std::vector<Point> result;
        result.reserve(80);
        const double extension = impl_->enlarged_boundary ? 0.5 : 0.0;
        for (int column = 0; column < 10; ++column) {
            for (int row = 0; row < 8; ++row) {
                const int index = column * 8 + row;
                const double base_u = impl_->enlarged_boundary
                    ? -0.4 + static_cast<double>(column) * 9.8 / 9.0
                    : static_cast<double>(column);
                const double base_v = impl_->enlarged_boundary
                    ? -0.4 + static_cast<double>(row) * 7.8 / 7.0
                    : static_cast<double>(row);
                const double jitter_u = 0.08 * (
                    2.0 * random.uniform(0, 6001, index, 0) - 1.0
                );
                const double jitter_v = 0.08 * (
                    2.0 * random.uniform(0, 6001, index, 1) - 1.0
                );
                const double u = std::clamp(
                    base_u + jitter_u, -extension, 9.0 + extension
                );
                const double v = std::clamp(
                    base_v + jitter_v, -extension, 7.0 + extension
                );
                result.push_back(impl_->affine_point(u, v));
            }
        }
        if (impl_->constraint_violation(result) > 1.0e-10) {
            throw std::runtime_error("T60 Horns stratified start infeasible");
        }
        return result;
    }

    for (int restart = 0; restart < 100; ++restart) {
        std::vector<Point> result;
        result.reserve(static_cast<std::size_t>(impl_->turbines));
        for (int turbine = 0; turbine < impl_->turbines; ++turbine) {
            bool placed = false;
            for (int attempt = 0; attempt < 20000; ++attempt) {
                const Point candidate{
                    kIdealDomainM * random.uniform(
                        restart, 6002, turbine, attempt, 0
                    ),
                    kIdealDomainM * random.uniform(
                        restart, 6002, turbine, attempt, 1
                    ),
                };
                const bool separated = std::all_of(
                    result.begin(), result.end(),
                    [&](const Point& existing) {
                        return std::hypot(
                            candidate.x_m - existing.x_m,
                            candidate.y_m - existing.y_m
                        ) >= kMinimumSpacingM;
                    }
                );
                if (separated) {
                    result.push_back(candidate);
                    placed = true;
                    break;
                }
            }
            if (!placed) break;
        }
        if (result.size() == static_cast<std::size_t>(impl_->turbines)) {
            return result;
        }
    }
    throw std::runtime_error("cannot construct T60 random feasible layout");
}

RunResult run(
    const Problem& problem,
    const RunConfig& config
) {
    if (config.physical_fes < 1U) {
        throw std::invalid_argument("T60 physical FES must include initial");
    }
    const auto start = Clock::now();
    const fode::CounterRng random(config.seed);
    std::vector<Point> layout = config.random_initial_layout
        ? problem.random_feasible_layout(config.seed)
        : problem.paper_initial_layout();
    auto qsum = problem.impl_->full_qsum(layout);
    const auto initial_eval_start = Clock::now();
    Evaluation current =
        problem.impl_->evaluation_from_qsum(layout, qsum);
    double evaluator_seconds =
        std::chrono::duration<double>(Clock::now() - initial_eval_start)
            .count();
    if (!current.feasible) {
        throw std::runtime_error("T60 initial layout is infeasible");
    }

    RunResult result;
    result.problem_id = problem.id();
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id = kMethodId;
    result.seed = config.seed;
    result.random_initial_layout = config.random_initial_layout;
    result.initial_power_kw = current.expected_power_kw;
    result.physical_fes = 1U;

    bool improve_flag = false;
    int remembered_turbine = -1;
    double remembered_angle = 0.0;
    std::uint64_t proposal_event = 0U;
    while (result.physical_fes < config.physical_fes) {
        if (proposal_event > 1000000000ULL) {
            throw std::runtime_error("T60 feasible-move retry limit exceeded");
        }
        const int turbine = improve_flag
            ? remembered_turbine
            : random.integer(
                0,
                problem.impl_->turbines,
                proposal_event,
                6003,
                result.physical_fes
            );
        const double angle = improve_flag
            ? remembered_angle
            : 2.0 * kPi * random.uniform(
                proposal_event, 6004, result.physical_fes
            );
        const double step = problem.impl_->long_edge * random.uniform(
            proposal_event, 6005, result.physical_fes
        );
        ++proposal_event;
        std::vector<Point> candidate = layout;
        candidate[static_cast<std::size_t>(turbine)].x_m +=
            step * std::cos(angle);
        candidate[static_cast<std::size_t>(turbine)].y_m +=
            step * std::sin(angle);
        if (!problem.impl_->moved_turbine_feasible(candidate, turbine)) {
            ++result.rejected_infeasible_proposals;
            // A remembered ray can point permanently outside the feasible
            // polygon after an accepted boundary move. Algorithm 1 says to
            // repeat Random Move but does not define this corner; return to
            // its random branch, as the cited predecessor implementation does.
            improve_flag = false;
            continue;
        }
        ++result.feasible_proposals;
        const auto evaluator_start = Clock::now();
        auto candidate_qsum = problem.impl_->incremental_qsum(
            layout, candidate, turbine, qsum
        );
        const Evaluation candidate_evaluation =
            problem.impl_->evaluation_from_qsum(
                candidate, candidate_qsum, true
            );
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - evaluator_start
        ).count();
        ++result.physical_fes;
        if (
            candidate_evaluation.expected_power_kw
            > current.expected_power_kw
        ) {
            layout = std::move(candidate);
            qsum = std::move(candidate_qsum);
            current = candidate_evaluation;
            improve_flag = true;
            remembered_turbine = turbine;
            remembered_angle = angle;
            ++result.accepted_moves;
        } else {
            improve_flag = false;
        }
    }
    const auto end = Clock::now();
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds =
        std::chrono::duration<double>(end - start).count();
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - evaluator_seconds
    );
    result.final_evaluation = current;
    result.final_layout = std::move(layout);
    result.scientific_hash = result_hash(
        result.final_layout, result.final_evaluation
    );
    return result;
}

std::vector<std::string> paper_problem_ids() {
    return {
        "t60_ideal_case1",
        "t60_ideal_case2",
        "t60_ideal_case3",
        "t60_horns_case1",
        "t60_horns_case2",
        "t60_horns_case3",
    };
}

}  // namespace core99::t60
