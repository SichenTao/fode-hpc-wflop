/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T69 pure-C++ changing-wind evaluator and random search
Paper/DOI: Feng and Shen; 10.1016/j.enconman.2017.06.005
Public source, omissions, conflicts and all completion decisions:
include/core99/feng_t69.hpp
HPC analysis: evidence/development/T69_H0_H4_mathematical_hpc_analysis_20260731.md
Method/problem semantic IDs: t69_random_position_rs_v1;
t69_horns_changing_wind_robustness_declared_v1
Controlling contract: shared/contracts/core99_t69_feng_robustness_2017.json
Claim boundary: flexible academic reconstruction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/feng_t69.hpp"

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

namespace core99::t69 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr int kTurbines = 80;
constexpr int kDirections = 360;
constexpr int kSectorCount = 12;
constexpr int kDirectionsPerSector = 30;
constexpr int kSpeedFirst = 3;
constexpr int kSpeedLast = 25;
constexpr int kSpeeds = kSpeedLast - kSpeedFirst + 1;
constexpr double kDiameterM = 80.0;
constexpr double kRadiusM = 40.0;
constexpr double kMinimumSpacingM = 5.0 * kDiameterM;
constexpr double kHubHeightM = 70.0;
constexpr double kReferenceHeightM = 62.0;
constexpr double kRoughnessM = 0.0002;
constexpr double kWakeDecay = 0.5 / std::log(kHubHeightM / kRoughnessM);
constexpr double kBasisAxM = 560.0;
constexpr double kBasisAyM = 0.0;
constexpr double kBasisBxM = 478.0 / 7.0;
constexpr double kBasisByM = 3891.0 / 7.0;
constexpr double kNameplateMw = 160.0;

constexpr std::array<double, 12> kScale{
    8.89, 9.27, 8.23, 9.78, 11.64, 11.03,
    11.50, 11.92, 11.49, 11.08, 11.34, 10.76
};
constexpr std::array<double, 12> kShape{
    2.09, 2.13, 2.29, 2.30, 2.67, 2.45,
    2.51, 2.40, 2.35, 2.27, 2.24, 2.19
};
constexpr std::array<double, 12> kFrequencyPercent{
    4.82, 4.06, 3.59, 5.27, 9.12, 6.97,
    9.17, 11.84, 12.41, 11.34, 11.70, 9.69
};
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
    return std::lerp(
        y[lo], y[hi], (value - x[lo]) / (x[hi] - x[lo])
    );
}

double weibull_cdf(const double speed, const double scale, const double shape) {
    if (!(speed > 0.0)) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

double weibull_cell(const int speed, const double scale, const double shape) {
    return weibull_cdf(static_cast<double>(speed) + 0.5, scale, shape)
        - weibull_cdf(std::max(0.0, static_cast<double>(speed) - 0.5),
                      scale, shape);
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
        (distance * distance + first_radius * first_radius
         - second_radius * second_radius)
            / (2.0 * distance * first_radius), -1.0, 1.0));
    const double second_angle = std::acos(std::clamp(
        (distance * distance + second_radius * second_radius
         - first_radius * first_radius)
            / (2.0 * distance * second_radius), -1.0, 1.0));
    const double radicand = std::max(
        0.0,
        (-distance + first_radius + second_radius)
            * (distance + first_radius - second_radius)
            * (distance - first_radius + second_radius)
            * (distance + first_radius + second_radius));
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radicand);
}

class OverlapLookup {
public:
    static constexpr int kWakeBins = 1024;
    static constexpr int kCrossBins = 1024;
    static constexpr double kMaximumWakeRatio = 16.0;
    static constexpr double kMaximumCrossRatio = 17.0;

    OverlapLookup()
        : values_(static_cast<std::size_t>(
              (kWakeBins + 1) * (kCrossBins + 1)), 0.0F) {
        const double rotor_area = kPi * kRadiusM * kRadiusM;
        for (int wake = 0; wake <= kWakeBins; ++wake) {
            const double wake_ratio = 1.0
                + (kMaximumWakeRatio - 1.0) * static_cast<double>(wake)
                    / static_cast<double>(kWakeBins);
            for (int cross = 0; cross <= kCrossBins; ++cross) {
                const double cross_ratio = kMaximumCrossRatio
                    * static_cast<double>(cross)
                    / static_cast<double>(kCrossBins);
                values_[index(wake, cross)] = static_cast<float>(
                    circle_overlap(cross_ratio * kRadiusM,
                                   wake_ratio * kRadiusM, kRadiusM)
                    / rotor_area);
            }
        }
    }

    [[nodiscard]] double operator()(
        const double wake_radius, const double crosswind
    ) const {
        const double wake_ratio = wake_radius / kRadiusM;
        const double cross_ratio = crosswind / kRadiusM;
        if (cross_ratio >= wake_ratio + 1.0) return 0.0;
        if (cross_ratio <= wake_ratio - 1.0) return 1.0;
        const double wp = std::clamp(
            (wake_ratio - 1.0) / (kMaximumWakeRatio - 1.0) * kWakeBins,
            0.0, static_cast<double>(kWakeBins));
        const double cp = std::clamp(
            cross_ratio / kMaximumCrossRatio * kCrossBins,
            0.0, static_cast<double>(kCrossBins));
        const int wl = static_cast<int>(wp);
        const int cl = static_cast<int>(cp);
        const int wh = std::min(wl + 1, kWakeBins);
        const int ch = std::min(cl + 1, kCrossBins);
        const double low = std::lerp(
            static_cast<double>(values_[index(wl, cl)]),
            static_cast<double>(values_[index(wh, cl)]), wp - wl);
        const double high = std::lerp(
            static_cast<double>(values_[index(wl, ch)]),
            static_cast<double>(values_[index(wh, ch)]), wp - wl);
        return std::lerp(low, high, cp - cl);
    }

private:
    std::vector<float> values_;
    static std::size_t index(const int wake, const int cross) {
        return static_cast<std::size_t>(wake * (kCrossBins + 1) + cross);
    }
};

const OverlapLookup& overlap_lookup() {
    static const OverlapLookup lookup;
    return lookup;
}

std::vector<Point> horns_layout() {
    constexpr std::array<double, 8> row_x{
        0.0,68.0,137.0,205.0,273.0,341.0,410.0,478.0};
    constexpr std::array<double, 8> row_y{
        0.0,556.0,1112.0,1668.0,2223.0,2779.0,3335.0,3891.0};
    constexpr std::array<double, 10> column_x{
        0.0,560.0,1120.0,1680.0,2240.0,2800.0,3360.0,3920.0,4480.0,5040.0};
    std::vector<Point> result;
    result.reserve(kTurbines);
    for (int column = 0; column < 10; ++column) {
        for (int row = 0; row < 8; ++row) {
            result.push_back({column_x[static_cast<std::size_t>(column)]
                                  + row_x[static_cast<std::size_t>(row)],
                              row_y[static_cast<std::size_t>(row)]});
        }
    }
    return result;
}

double lambda_one_draw(
    const fode::CounterRng& random,
    const int scenario,
    const int parameter
) {
    for (int attempt = 0; attempt < 100000; ++attempt) {
        const double value = 2.0 * random.uniform(
            scenario, 69010, parameter, attempt, 0) - 1.0;
        const double ordinate = random.uniform(
            scenario, 69010, parameter, attempt, 1);
        if (ordinate <= std::sqrt(std::max(0.0, 1.0 - value * value))) {
            return value;
        }
    }
    throw std::runtime_error("T69 lambda-PDF rejection limit");
}

double objective(const Evaluation& value, const Study study) {
    if (study == Study::short_term) return value.short_robustness;
    if (study == Study::long_term) return value.long_robustness;
    return value.overall_robustness;
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
    for (const double value : {
             evaluation.mean_power_mw,
             evaluation.variability_of_power,
             evaluation.long_term_mean_mw,
             evaluation.long_term_std_mw,
             evaluation.short_robustness,
             evaluation.long_robustness,
             evaluation.overall_robustness}) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

struct Problem::Impl {
    mutable fode::PersistentExecutor executor;
    std::uint64_t scenario_seed = 0;
    int scenarios = 0;
    std::array<double, kDirections> sine{};
    std::array<double, kDirections> cosine{};
    std::array<double, kSpeeds> ct_coefficient{};
    std::vector<double> base_weights;
    std::vector<double> scenario_weights;
    std::vector<Point> reference_layout;
    std::vector<double> reference_qsum;
    double scenario_precompute_seconds = 0.0;

    explicit Impl(
        const int workers,
        const std::uint64_t seed,
        const int scenario_count
    ) : executor(workers), scenario_seed(seed), scenarios(scenario_count) {
        if (workers < 1 || scenario_count < 2) {
            throw std::invalid_argument("T69 workers/scenarios invalid");
        }
        const auto started = Clock::now();
        for (int direction = 0; direction < kDirections; ++direction) {
            const double angle = static_cast<double>(direction) * kPi / 180.0;
            sine[static_cast<std::size_t>(direction)] = std::sin(angle);
            cosine[static_cast<std::size_t>(direction)] = std::cos(angle);
        }
        for (int speed_index = 0; speed_index < kSpeeds; ++speed_index) {
            const double ct = kV80Ct[static_cast<std::size_t>(speed_index)];
            ct_coefficient[static_cast<std::size_t>(speed_index)] =
                1.0 - std::sqrt(std::max(0.0, 1.0 - ct));
        }
        build_weights();
        reference_layout = horns_layout();
        reference_qsum = full_qsum(reference_layout);
        scenario_precompute_seconds =
            std::chrono::duration<double>(Clock::now() - started).count();
    }

    [[nodiscard]] std::pair<double, double> affine_uv(
        const Point& point
    ) const {
        const double determinant =
            kBasisAxM * kBasisByM - kBasisAyM * kBasisBxM;
        return {(point.x_m * kBasisByM - point.y_m * kBasisBxM) / determinant,
                (kBasisAxM * point.y_m - kBasisAyM * point.x_m) / determinant};
    }

    [[nodiscard]] Point affine_point(const double u, const double v) const {
        return {u * kBasisAxM + v * kBasisBxM,
                u * kBasisAyM + v * kBasisByM};
    }

    [[nodiscard]] bool moved_feasible(
        const std::vector<Point>& layout,
        const int moved
    ) const {
        const auto [u, v] = affine_uv(layout[static_cast<std::size_t>(moved)]);
        constexpr double tolerance = 0.001;
        if (u < -tolerance || u > 9.0 + tolerance
            || v < -tolerance || v > 7.0 + tolerance) return false;
        for (int other = 0; other < kTurbines; ++other) {
            if (other == moved) continue;
            if (std::hypot(
                    layout[static_cast<std::size_t>(moved)].x_m
                        - layout[static_cast<std::size_t>(other)].x_m,
                    layout[static_cast<std::size_t>(moved)].y_m
                        - layout[static_cast<std::size_t>(other)].y_m)
                < kMinimumSpacingM) return false;
        }
        return true;
    }

    [[nodiscard]] bool feasible(const std::vector<Point>& layout) const {
        if (layout.size() != static_cast<std::size_t>(kTurbines)) return false;
        for (int turbine = 0; turbine < kTurbines; ++turbine) {
            if (!moved_feasible(layout, turbine)) return false;
        }
        return true;
    }

    [[nodiscard]] double influence(
        const Point& source,
        const Point& target,
        const int direction
    ) const {
        const double dx = target.x_m - source.x_m;
        const double dy = target.y_m - source.y_m;
        const double downstream = sine[static_cast<std::size_t>(direction)] * dx
            + cosine[static_cast<std::size_t>(direction)] * dy;
        if (!(downstream > 0.0)) return 0.0;
        const double crosswind = std::abs(
            cosine[static_cast<std::size_t>(direction)] * dx
            - sine[static_cast<std::size_t>(direction)] * dy);
        const double expansion = 1.0 + kWakeDecay * downstream / kRadiusM;
        const double fraction = overlap_lookup()(
            kRadiusM + kWakeDecay * downstream, crosswind);
        const double ratio = fraction / (expansion * expansion);
        return ratio * ratio;
    }

    void build_weights() {
        const double frequency_total = std::accumulate(
            kFrequencyPercent.begin(), kFrequencyPercent.end(), 0.0);
        base_weights.assign(
            static_cast<std::size_t>(kSectorCount * kSpeeds), 0.0);
        for (int sector = 0; sector < kSectorCount; ++sector) {
            const double frequency =
                kFrequencyPercent[static_cast<std::size_t>(sector)]
                / frequency_total;
            for (int speed_index = 0; speed_index < kSpeeds; ++speed_index) {
                base_weights[static_cast<std::size_t>(
                    sector * kSpeeds + speed_index)] = frequency * weibull_cell(
                        kSpeedFirst + speed_index,
                        kScale[static_cast<std::size_t>(sector)],
                        kShape[static_cast<std::size_t>(sector)]);
            }
        }

        scenario_weights.assign(
            static_cast<std::size_t>(scenarios * kSectorCount * kSpeeds),
            0.0);
        const fode::CounterRng random(scenario_seed);
        executor.parallel_for(0, scenarios, [&](const int scenario) {
            std::array<double, kSectorCount> scale{};
            std::array<double, kSectorCount> shape{};
            std::array<double, kSectorCount> frequency{};
            double frequency_sum = 0.0;
            for (int sector = 0; sector < kSectorCount; ++sector) {
                scale[static_cast<std::size_t>(sector)] =
                    kScale[static_cast<std::size_t>(sector)]
                    * (1.0 + 0.2 * lambda_one_draw(random, scenario, sector));
                shape[static_cast<std::size_t>(sector)] =
                    kShape[static_cast<std::size_t>(sector)]
                    * (1.0 + 0.1 * lambda_one_draw(
                        random, scenario, kSectorCount + sector));
                frequency[static_cast<std::size_t>(sector)] =
                    kFrequencyPercent[static_cast<std::size_t>(sector)]
                    * (1.0 + 0.5 * lambda_one_draw(
                        random, scenario, 2 * kSectorCount + sector));
                frequency_sum += frequency[static_cast<std::size_t>(sector)];
            }
            for (int sector = 0; sector < kSectorCount; ++sector) {
                const double normalized =
                    frequency[static_cast<std::size_t>(sector)] / frequency_sum;
                for (int speed_index = 0; speed_index < kSpeeds; ++speed_index) {
                    scenario_weights[static_cast<std::size_t>(
                        (scenario * kSectorCount + sector) * kSpeeds
                        + speed_index)] = normalized * weibull_cell(
                            kSpeedFirst + speed_index,
                            scale[static_cast<std::size_t>(sector)],
                            shape[static_cast<std::size_t>(sector)]);
                }
            }
        });
    }

    [[nodiscard]] std::vector<double> full_qsum(
        const std::vector<Point>& layout
    ) const {
        std::vector<double> qsum(
            static_cast<std::size_t>(kDirections * kTurbines), 0.0);
        executor.parallel_for(0, kDirections, [&](const int direction) {
            for (int target = 0; target < kTurbines; ++target) {
                double sum = 0.0;
                for (int source = 0; source < kTurbines; ++source) {
                    if (source != target) {
                        sum += influence(
                            layout[static_cast<std::size_t>(source)],
                            layout[static_cast<std::size_t>(target)], direction);
                    }
                }
                qsum[static_cast<std::size_t>(
                    direction * kTurbines + target)] = sum;
            }
        });
        return qsum;
    }

    [[nodiscard]] std::vector<double> incremental_qsum(
        const std::vector<Point>& current,
        const std::vector<Point>& candidate,
        const int moved,
        const std::vector<double>& current_qsum
    ) const {
        std::vector<double> result = current_qsum;
        executor.parallel_for(0, kDirections, [&](const int direction) {
            for (int target = 0; target < kTurbines; ++target) {
                const std::size_t index = static_cast<std::size_t>(
                    direction * kTurbines + target);
                if (target == moved) {
                    double sum = 0.0;
                    for (int source = 0; source < kTurbines; ++source) {
                        if (source != moved) {
                            sum += influence(
                                current[static_cast<std::size_t>(source)],
                                candidate[static_cast<std::size_t>(moved)],
                                direction);
                        }
                    }
                    result[index] = sum;
                } else {
                    result[index] += influence(
                        candidate[static_cast<std::size_t>(moved)],
                        current[static_cast<std::size_t>(target)], direction)
                        - influence(
                            current[static_cast<std::size_t>(moved)],
                            current[static_cast<std::size_t>(target)], direction);
                    result[index] = std::max(0.0, result[index]);
                }
            }
        });
        return result;
    }

    [[nodiscard]] std::vector<double> power_surface(
        const std::vector<double>& qsum
    ) const {
        std::vector<double> surface(
            static_cast<std::size_t>(kDirections * kSpeeds), 0.0);
        executor.parallel_for(0, kDirections, [&](const int direction) {
            for (int speed_index = 0; speed_index < kSpeeds; ++speed_index) {
                const double free_speed = static_cast<double>(
                    kSpeedFirst + speed_index);
                const double coefficient =
                    ct_coefficient[static_cast<std::size_t>(speed_index)];
                double farm_power_kw = 0.0;
                for (int turbine = 0; turbine < kTurbines; ++turbine) {
                    const double effective_speed = free_speed * std::max(
                        0.0, 1.0 - coefficient * std::sqrt(qsum[
                            static_cast<std::size_t>(
                                direction * kTurbines + turbine)]));
                    farm_power_kw += interpolate(
                        kV80Speed, kV80PowerKw, effective_speed);
                }
                surface[static_cast<std::size_t>(
                    direction * kSpeeds + speed_index)] = farm_power_kw / 1000.0;
            }
        });
        return surface;
    }

    [[nodiscard]] Evaluation metrics(
        const std::vector<double>& qsum,
        const double alpha,
        const double beta,
        const double gamma,
        const ConflictProfile profile,
        const Evaluation* reference,
        const bool need_short,
        const bool need_long
    ) const {
        Evaluation value;
        value.feasible = true;
        const auto surface = power_surface(qsum);
        std::vector<double> sector_speed(
            static_cast<std::size_t>(kSectorCount * kSpeeds), 0.0);
        for (int sector = 0; sector < kSectorCount; ++sector) {
            for (int speed = 0; speed < kSpeeds; ++speed) {
                double sum = 0.0;
                for (int local = 0; local < kDirectionsPerSector; ++local) {
                    sum += surface[static_cast<std::size_t>(
                        (sector * kDirectionsPerSector + local) * kSpeeds
                        + speed)];
                }
                sector_speed[static_cast<std::size_t>(
                    sector * kSpeeds + speed)] =
                    sum / static_cast<double>(kDirectionsPerSector);
            }
        }
        value.mean_power_mw = std::inner_product(
            base_weights.begin(), base_weights.end(),
            sector_speed.begin(), 0.0);
        value.aep_mwh_paper_8770 = 8770.0 * value.mean_power_mw;
        value.aep_mwh_calendar_8760 = 8760.0 * value.mean_power_mw;

        if (need_short) {
            const double dv = 1.0 / static_cast<double>(kSpeedLast-kSpeedFirst);
            const double dt = 1.0 / 360.0;
            const double diagonal = dv * dv + dt * dt;
            std::vector<double> directional_vop(
                static_cast<std::size_t>(kDirections), 0.0);
            executor.parallel_for(0, kDirections, [&](const int direction) {
                const int west = (direction + kDirections - 1) % kDirections;
                const int east = (direction + 1) % kDirections;
                double sum = 0.0;
                const int sector = direction / kDirectionsPerSector;
                for (int speed = 0; speed < kSpeeds; ++speed) {
                    auto p = [&](const int d, const int s) {
                        if (s < 0 || s >= kSpeeds) return 0.0;
                        return surface[static_cast<std::size_t>(
                            d * kSpeeds + s)] / kNameplateMw;
                    };
                    const double centre = p(direction, speed);
                    double rugged = 0.0;
                    for (const int neighbor : {speed - 1, speed + 1}) {
                        const double delta = centre - p(direction, neighbor);
                        rugged += delta * delta / (dv * dv);
                    }
                    for (const int neighbor : {west, east}) {
                        const double delta = centre - p(neighbor, speed);
                        rugged += delta * delta / (dt * dt);
                    }
                    for (const int neighbor_direction : {west, east}) {
                        for (const int neighbor_speed : {speed - 1, speed + 1}) {
                            const double delta =
                                centre - p(neighbor_direction, neighbor_speed);
                            rugged += delta * delta / diagonal;
                        }
                    }
                    const double psri = std::sqrt(rugged / 8.0);
                    const double probability = base_weights[static_cast<std::size_t>(
                        sector * kSpeeds + speed)]
                        / static_cast<double>(kDirectionsPerSector)
                        / static_cast<double>(
                            (kSpeedLast - kSpeedFirst) * kDirections);
                    sum += probability * psri;
                }
                directional_vop[static_cast<std::size_t>(direction)] = sum;
            });
            value.variability_of_power = std::accumulate(
                directional_vop.begin(), directional_vop.end(), 0.0);
        }

        if (need_long) {
            std::vector<double> means(static_cast<std::size_t>(scenarios), 0.0);
            executor.parallel_for(0, scenarios, [&](const int scenario) {
                const auto begin = scenario_weights.begin()
                    + static_cast<std::ptrdiff_t>(
                        scenario * kSectorCount * kSpeeds);
                means[static_cast<std::size_t>(scenario)] = std::inner_product(
                    begin, begin + kSectorCount * kSpeeds,
                    sector_speed.begin(), 0.0);
            });
            value.long_term_mean_mw = std::accumulate(
                means.begin(), means.end(), 0.0)
                / static_cast<double>(scenarios);
            double squared = 0.0;
            for (const double item : means) {
                const double delta = item - value.long_term_mean_mw;
                squared += delta * delta;
            }
            value.long_term_std_mw = std::sqrt(
                squared / static_cast<double>(scenarios - 1));
        }

        if (reference != nullptr) {
            if (need_short) {
                value.short_robustness =
                    std::pow(value.mean_power_mw / reference->mean_power_mw,
                             alpha)
                    / std::pow(
                        value.variability_of_power
                            / reference->variability_of_power,
                        1.0 - alpha);
            }
            if (need_long) {
                value.long_robustness =
                    std::pow(value.long_term_mean_mw, beta)
                    / std::pow(value.long_term_std_mw, 1.0 - beta);
                value.table3_compatible_long_robustness = std::sqrt(
                    value.long_term_mean_mw / value.long_term_std_mw);
            }
            if (need_short && need_long) {
                const double selected_long =
                    profile == ConflictProfile::table3_compatible
                    ? value.table3_compatible_long_robustness
                    : value.long_robustness;
                value.overall_robustness =
                    gamma * value.short_robustness
                    + (1.0 - gamma) * selected_long;
            }
        }
        return value;
    }
};

Problem::Problem(
    const int workers,
    const std::uint64_t scenario_seed,
    const int scenario_count
) : impl_(std::make_unique<Impl>(workers, scenario_seed, scenario_count)) {}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
int Problem::turbine_count() const noexcept { return kTurbines; }
int Problem::direction_count() const noexcept { return kDirections; }
int Problem::speed_count() const noexcept { return kSpeeds; }
int Problem::scenario_count() const noexcept { return impl_->scenarios; }
std::vector<Point> Problem::paper_initial_layout() const {
    return impl_->reference_layout;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const double alpha,
    const double beta,
    const double gamma,
    const ConflictProfile profile
) const {
    if (!impl_->feasible(layout)) return {};
    Evaluation reference = impl_->metrics(
        impl_->reference_qsum, alpha, beta, gamma, profile,
        nullptr, true, true);
    reference.short_robustness = 1.0;
    reference.long_robustness =
        std::pow(reference.long_term_mean_mw, beta)
        / std::pow(reference.long_term_std_mw, 1.0 - beta);
    reference.table3_compatible_long_robustness = std::sqrt(
        reference.long_term_mean_mw / reference.long_term_std_mw);
    return impl_->metrics(
        impl_->full_qsum(layout), alpha, beta, gamma, profile,
        &reference, true, true);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.physical_fes < 1U || config.workers != problem.impl_->executor.thread_count()) {
        throw std::invalid_argument("T69 FES/workers mismatch");
    }
    if (config.weight < 0.0 || config.weight > 1.0
        || config.alpha < 0.0 || config.alpha > 1.0
        || config.beta < 0.0 || config.beta > 1.0) {
        throw std::invalid_argument("T69 robustness weight outside [0,1]");
    }
    const auto started = Clock::now();
    problem.impl_->executor.reset_work_receipt();
    const fode::CounterRng random(config.seed);
    std::vector<Point> layout = problem.impl_->reference_layout;
    std::vector<double> qsum = problem.impl_->reference_qsum;
    const double alpha = config.study == Study::short_term
        ? config.weight : config.alpha;
    const double beta = config.study == Study::long_term
        ? config.weight : config.beta;
    const double gamma = config.study == Study::overall
        ? config.weight : (config.study == Study::short_term ? 1.0 : 0.0);
    const bool need_short = config.study != Study::long_term;
    const bool need_long = config.study != Study::short_term;

    const auto reference_metric_started = Clock::now();
    Evaluation reference = problem.impl_->metrics(
        qsum, alpha, beta, gamma, config.conflict_profile,
        nullptr, true, true);
    reference.short_robustness = 1.0;
    reference.long_robustness =
        std::pow(reference.long_term_mean_mw, beta)
        / std::pow(reference.long_term_std_mw, 1.0 - beta);
    reference.table3_compatible_long_robustness = std::sqrt(
        reference.long_term_mean_mw / reference.long_term_std_mw);
    const double reference_long =
        config.conflict_profile == ConflictProfile::table3_compatible
        ? reference.table3_compatible_long_robustness
        : reference.long_robustness;
    reference.overall_robustness = gamma + (1.0 - gamma) * reference_long;
    Evaluation current = reference;
    double metric_seconds = std::chrono::duration<double>(
        Clock::now() - reference_metric_started).count();
    double wake_seconds = 0.0;

    RunResult result;
    result.case_id = "t69_" + to_string(config.study) + "_"
        + std::to_string(config.weight);
    result.study = to_string(config.study);
    result.conflict_profile = to_string(config.conflict_profile);
    result.weight = config.weight;
    result.effective_alpha = alpha;
    result.effective_beta = beta;
    result.effective_gamma = gamma;
    result.seed = config.seed;
    result.physical_fes = 1;
    result.requested_workers = config.workers;
    result.reference = reference;

    std::uint64_t proposal = 0;
    while (result.physical_fes < config.physical_fes) {
        if (proposal > 1000000000ULL) {
            throw std::runtime_error("T69 feasible-position retry limit");
        }
        const int moved = random.integer(
            0, kTurbines, proposal, 69020, result.physical_fes);
        const double u = 9.0 * random.uniform(
            proposal, 69021, result.physical_fes, moved, 0);
        const double v = 7.0 * random.uniform(
            proposal, 69021, result.physical_fes, moved, 1);
        ++proposal;
        std::vector<Point> candidate = layout;
        candidate[static_cast<std::size_t>(moved)] =
            problem.impl_->affine_point(u, v);
        if (!problem.impl_->moved_feasible(candidate, moved)) {
            ++result.infeasible_proposals;
            continue;
        }
        const auto wake_started = Clock::now();
        auto candidate_qsum = problem.impl_->incremental_qsum(
            layout, candidate, moved, qsum);
        wake_seconds += std::chrono::duration<double>(
            Clock::now() - wake_started).count();
        const auto metric_started = Clock::now();
        Evaluation candidate_evaluation = problem.impl_->metrics(
            candidate_qsum, alpha, beta, gamma, config.conflict_profile,
            &reference, need_short, need_long);
        metric_seconds += std::chrono::duration<double>(
            Clock::now() - metric_started).count();
        ++result.physical_fes;
        if (objective(candidate_evaluation, config.study)
            > objective(current, config.study)) {
            layout = std::move(candidate);
            qsum = std::move(candidate_qsum);
            current = candidate_evaluation;
            ++result.accepted_moves;
        }
    }

    const auto final_metric_started = Clock::now();
    current = problem.impl_->metrics(
        qsum, alpha, beta, gamma, config.conflict_profile,
        &reference, true, true);
    metric_seconds += std::chrono::duration<double>(
        Clock::now() - final_metric_started).count();
    const auto ended = Clock::now();
    const auto receipt = problem.impl_->executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    result.final_evaluation = current;
    result.final_layout = std::move(layout);
    result.wind_scenario_precompute_seconds =
        problem.impl_->scenario_precompute_seconds;
    result.wake_update_seconds = wake_seconds;
    result.robustness_metric_seconds = metric_seconds;
    result.end_to_end_seconds =
        std::chrono::duration<double>(ended - started).count();
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - wake_seconds - metric_seconds);
    result.scientific_hash = result_hash(
        result.final_layout, result.final_evaluation);
    return result;
}

std::vector<double> paper_weights() { return {0.0, 0.05, 0.5, 0.95, 1.0}; }

std::string to_string(const Study value) {
    if (value == Study::short_term) return "short";
    if (value == Study::long_term) return "long";
    return "overall";
}

std::string to_string(const ConflictProfile value) {
    return value == ConflictProfile::equation_declared
        ? "equation_declared" : "table3_compatible";
}

}  // namespace core99::t69
