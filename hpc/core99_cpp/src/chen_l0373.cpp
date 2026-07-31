/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0373 pure-C++ FLORIS-lineage evaluator, PSO and DBHM
Paper/DOI: Chen et al.; 10.1016/j.renene.2021.10.032
Public paper source: arXiv 2107.11620, archive SHA-256
6c5dd1b686f0501051974d4d464a44ca5283847b34a459d6e4daf5535bcbef9c.
Cited dependency: FLORISSE_M, MIT, commit 36cb0a0295d2a1e05640fdbbcb9bb361ac8d592e,
repository archive SHA-256 451150ba62b242353f9bdaa5e5bef10cad5d63e0e3841d625838572d9f0c1f75.
Target-code search, missing information, equation conflicts, corrections,
declared reconstruction, semantic IDs, HPC backend and claim boundary:
include/core99/chen_l0373.hpp
HPC analysis: evidence/development/L0373_H0_H4_mathematical_hpc_analysis_20260801.md
Controlling contract: shared/contracts/core99_l0373_chen_2021.json.
Claim boundary: flexible academic reconstruction, not author code, exact
FLORISSE-M adaptation, MATLAB trajectory, private arrays or numeric replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/chen_l0373.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::l0373 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kRotorDiameterM = 126.0;
constexpr double kAirDensity = 1.29;
constexpr double kYawPowerExponent = 1.88;
constexpr double kYawMinimumDegrees = -30.0;
constexpr double kYawMaximumDegrees = 30.0;
constexpr double kAxialMinimum = 0.1;
constexpr double kAxialMaximum = 1.0 / 3.0;
constexpr double kDeflectionAd = -0.0356;
constexpr double kDeflectionBd = -0.01;
constexpr double kWakeExpansion = 0.0229;
constexpr double kFlorisAlpha = 2.32;
constexpr double kFlorisBeta = 0.154;
constexpr double kFlorisKa = 0.3837;
constexpr double kFlorisKb = 0.0037;
constexpr double kAmbientTiCompletion =
    (kWakeExpansion - kFlorisKb) / kFlorisKa;
constexpr double kPenaltyFactor = 1.0e5;
constexpr double kDbhmMu = 10.0;
constexpr double kConsensusToleranceM = 10.0;
constexpr double kHoursPerYear = 8760.0;
constexpr std::array<double, 36> kWindRoseDigitizedRadii{
    427.0, 382.0, 336.0, 290.0, 287.0, 283.0,
    307.0, 351.0, 388.0, 415.0, 413.0, 450.0,
    491.0, 454.0, 418.0, 367.0, 410.0, 438.0,
    467.0, 512.0, 543.0, 589.0, 592.0, 610.0,
    626.0, 618.0, 608.0, 599.0, 589.0, 565.0,
    555.0, 543.0, 535.0, 525.0, 498.0, 471.0,
};
constexpr std::array<double, 9> kRotorOffsets{
    -0.8888888888888888, -0.6666666666666666,
    -0.4444444444444444, -0.2222222222222222,
    0.0,
    0.2222222222222222, 0.4444444444444444,
    0.6666666666666666, 0.8888888888888888,
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double square(const double value) { return value * value; }

double degrees_to_radians(const double degrees) {
    return degrees * kPi / 180.0;
}

double distance(const Point& first, const Point& second) {
    return std::hypot(first.x_m - second.x_m, first.y_m - second.y_m);
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t quantized(const double value, const double scale = 1.0e6) {
    const auto integer = static_cast<std::int64_t>(std::llround(value * scale));
    return std::bit_cast<std::uint64_t>(integer);
}

std::vector<WindState> make_winds(const int count) {
    if (count == 1) return {{0.0, 1.0}};
    if (count != 12 && count != 36 && count != 180 && count != 360) {
        throw std::invalid_argument("L0373 unsupported wind cardinality");
    }
    const double total = std::accumulate(
        kWindRoseDigitizedRadii.begin(), kWindRoseDigitizedRadii.end(), 0.0
    );
    std::vector<WindState> result;
    result.reserve(static_cast<std::size_t>(count));
    if (count == 12) {
        for (int sector = 0; sector < 12; ++sector) {
            double probability = 0.0;
            for (int bin = 0; bin < 3; ++bin) {
                probability += kWindRoseDigitizedRadii[
                    static_cast<std::size_t>(3 * sector + bin)
                ] / total;
            }
            result.push_back({30.0 * sector + 10.0, probability});
        }
        return result;
    }
    const int subdivisions = count / 36;
    for (int base = 0; base < 36; ++base) {
        const double probability = kWindRoseDigitizedRadii[
            static_cast<std::size_t>(base)
        ] / total / static_cast<double>(subdivisions);
        for (int part = 0; part < subdivisions; ++part) {
            result.push_back({
                10.0 * base + (static_cast<double>(part) + 0.5)
                    * 10.0 / subdivisions,
                probability,
            });
        }
    }
    return result;
}

Controls greedy_controls(const int turbine_count) {
    return {
        std::vector<double>(static_cast<std::size_t>(turbine_count), 0.0),
        std::vector<double>(
            static_cast<std::size_t>(turbine_count), kAxialMaximum
        ),
    };
}

std::vector<Controls> greedy_schedule(const int winds, const int turbines) {
    return std::vector<Controls>(
        static_cast<std::size_t>(winds), greedy_controls(turbines)
    );
}

double turbine_power_mw(
    const double speed_mps,
    const double yaw_degrees,
    const double axial_induction
) {
    const double yaw = degrees_to_radians(yaw_degrees);
    const double cosine = std::max(0.0, std::cos(yaw));
    const double cp = 4.0 * axial_induction
        * square(1.0 - axial_induction);
    const double area = 0.25 * kPi * square(kRotorDiameterM);
    return 0.5 * kAirDensity * area * cp
        * std::pow(cosine, kYawPowerExponent)
        * speed_mps * speed_mps * speed_mps / 1.0e6;
}

}  // namespace

struct Problem::Impl {
    ProfileId profile;
    std::string id;
    int turbines = 16;
    double x_low_m = 100.0;
    double x_high_m = 2000.0;
    double y_low_m = 100.0;
    double y_high_m = 1800.0;
    double spacing_m = 4.0 * kRotorDiameterM;
    std::vector<WindState> winds;

    explicit Impl(const ProfileId selected) : profile(selected) {
        switch (selected) {
            case ProfileId::illustrative_unrestricted:
                id = "L0373_ILLUSTRATIVE_UNRESTRICTED";
                turbines = 3;
                x_low_m = 0.0;
                x_high_m = 1100.0;
                y_low_m = 0.0;
                y_high_m = 0.0;
                spacing_m = 0.0;
                winds = make_winds(1);
                break;
            case ProfileId::illustrative_4d:
                id = "L0373_ILLUSTRATIVE_4D";
                turbines = 3;
                x_low_m = 0.0;
                x_high_m = 1100.0;
                y_low_m = 0.0;
                y_high_m = 0.0;
                spacing_m = 4.0 * kRotorDiameterM;
                winds = make_winds(1);
                break;
            case ProfileId::turbines16_directions36:
                id = "L0373_N16_W36";
                winds = make_winds(36);
                break;
            case ProfileId::turbines16_directions360:
                id = "L0373_N16_W360";
                winds = make_winds(360);
                break;
            case ProfileId::turbines80_directions12:
                id = "L0373_N80_W12";
                turbines = 80;
                x_low_m = -800.0;
                x_high_m = 8000.0;
                y_low_m = 0.0;
                y_high_m = 6200.0;
                spacing_m = 4.0 * kRotorDiameterM;
                winds = make_winds(12);
                break;
            case ProfileId::turbines80_directions180:
                id = "L0373_N80_W180";
                turbines = 80;
                x_low_m = -800.0;
                x_high_m = 8000.0;
                y_low_m = 0.0;
                y_high_m = 6200.0;
                spacing_m = 4.0 * kRotorDiameterM;
                winds = make_winds(180);
                break;
        }
    }

    [[nodiscard]] std::vector<Point> initial() const {
        if (turbines == 3) {
            return {{0.0, 0.0}, {550.0, 0.0}, {1100.0, 0.0}};
        }
        std::vector<Point> result;
        result.reserve(static_cast<std::size_t>(turbines));
        if (turbines == 16) {
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    result.push_back({
                        x_low_m + (x_high_m - x_low_m) * column / 3.0,
                        y_low_m + (y_high_m - y_low_m) * row / 3.0,
                    });
                }
            }
            return result;
        }
        const double side = 7.0 * kRotorDiameterM;
        const double tilt = degrees_to_radians(7.2);
        for (int row = 0; row < 8; ++row) {
            for (int column = 0; column < 10; ++column) {
                result.push_back({
                    side * column - side * std::sin(tilt) * row,
                    side * std::cos(tilt) * row,
                });
            }
        }
        return result;
    }

    [[nodiscard]] double state_power_mw(
        const std::vector<Point>& layout,
        const Controls& controls,
        const WindState& wind,
        std::atomic<std::uint64_t>* state_evaluations
    ) const {
        if (state_evaluations != nullptr) {
            state_evaluations->fetch_add(1, std::memory_order_relaxed);
        }
        if (layout.size() != static_cast<std::size_t>(turbines)
            || controls.yaw_degrees.size() != layout.size()
            || controls.axial_induction.size() != layout.size()) {
            throw std::invalid_argument("L0373 state cardinality mismatch");
        }
        const double angle = degrees_to_radians(wind.direction_degrees);
        const double flow_x = std::cos(angle);
        const double flow_y = std::sin(angle);
        const double cross_x = -flow_y;
        const double cross_y = flow_x;
        std::vector<double> along(layout.size());
        std::vector<double> across(layout.size());
        std::vector<int> order(layout.size());
        std::iota(order.begin(), order.end(), 0);
        for (std::size_t index = 0; index < layout.size(); ++index) {
            along[index] = layout[index].x_m * flow_x + layout[index].y_m * flow_y;
            across[index] = layout[index].x_m * cross_x + layout[index].y_m * cross_y;
        }
        std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
            const std::size_t lhs = static_cast<std::size_t>(left);
            const std::size_t rhs = static_cast<std::size_t>(right);
            return along[lhs] == along[rhs] ? left < right : along[lhs] < along[rhs];
        });
        double farm_power_mw = 0.0;
        for (std::size_t target_position = 0;
             target_position < order.size(); ++target_position) {
            const std::size_t target = static_cast<std::size_t>(
                order[target_position]
            );
            double speed_sum = 0.0;
            double weight_sum = 0.0;
            for (const double normalized_offset : kRotorOffsets) {
                const double rotor_offset = 0.5 * kRotorDiameterM
                    * normalized_offset;
                const double rotor_weight = std::sqrt(std::max(
                    0.0, 1.0 - normalized_offset * normalized_offset
                ));
                double squared_deficit_sum = 0.0;
                for (std::size_t source_position = 0;
                     source_position < target_position; ++source_position) {
                    const std::size_t source = static_cast<std::size_t>(
                        order[source_position]
                    );
                    const double downstream = along[target] - along[source];
                    if (!(downstream > 0.0)) continue;
                    const double yaw = degrees_to_radians(
                        controls.yaw_degrees[source]
                    );
                    const double cosine = std::max(1.0e-6, std::cos(yaw));
                    const double axial = std::clamp(
                        controls.axial_induction[source],
                        kAxialMinimum, kAxialMaximum
                    );
                    const double ct = std::clamp(
                        4.0 * axial * (1.0 - axial), 1.0e-6, 0.999999
                    );
                    const double root = std::sqrt(std::max(0.0, 1.0 - ct));
                    const double x0 = kRotorDiameterM * cosine * (1.0 + root)
                        / (std::sqrt(2.0) * (
                            kFlorisAlpha * kAmbientTiCompletion
                            + kFlorisBeta * (1.0 - root)
                        ));
                    const double sigma0 = kRotorDiameterM * cosine
                        / (2.0 * std::sqrt(2.0));
                    const double effective_x = std::max(downstream, x0);
                    const double sigma = sigma0
                        + (effective_x - x0) * kWakeExpansion;
                    const double phi = 0.3 * yaw / cosine * (
                        1.0 - std::sqrt(std::max(0.0, 1.0 - ct * cosine))
                    );
                    const double c0 = 1.0 - root;
                    const double e0 = c0 * c0
                        - 3.0 * std::exp(1.0 / 12.0) * c0
                        + 3.0 * std::exp(1.0 / 3.0);
                    const double sigma_ratio = std::sqrt(std::max(
                        1.0, sigma / sigma0
                    ));
                    const double sqrt_ct = std::sqrt(ct);
                    const double numerator = (1.6 + sqrt_ct)
                        * std::max(1.0e-12, 1.6 * sigma_ratio - sqrt_ct);
                    const double denominator = std::max(
                        1.0e-12,
                        (1.6 - sqrt_ct) * (1.6 * sigma_ratio + sqrt_ct)
                    );
                    const double deflection = kDeflectionAd * kRotorDiameterM
                        + kDeflectionBd * downstream + std::tan(phi) * x0
                        + phi / 5.2 * e0
                            * std::sqrt(sigma0 / (kWakeExpansion * ct))
                            * std::log(numerator / denominator);
                    const double lateral = across[target] + rotor_offset
                        - across[source] - deflection;
                    const double radical = std::clamp(
                        1.0 - sigma0 / sigma * ct, 0.0, 1.0
                    );
                    const double amplitude = 1.0 - std::sqrt(radical);
                    const double deficit = amplitude * std::exp(
                        -0.5 * square(lateral / sigma)
                    );
                    squared_deficit_sum += deficit * deficit;
                }
                const double speed = 9.0 * std::max(
                    0.0, 1.0 - std::sqrt(squared_deficit_sum)
                );
                speed_sum += rotor_weight * speed;
                weight_sum += rotor_weight;
            }
            const double rotor_speed = speed_sum / weight_sum;
            farm_power_mw += turbine_power_mw(
                rotor_speed,
                controls.yaw_degrees[target],
                controls.axial_induction[target]
            );
        }
        return farm_power_mw;
    }

    [[nodiscard]] Evaluation geometry(const std::vector<Point>& layout) const {
        Evaluation result;
        if (layout.size() != static_cast<std::size_t>(turbines)) return result;
        result.minimum_distance_m = std::numeric_limits<double>::infinity();
        bool bounds = true;
        for (std::size_t first = 0; first < layout.size(); ++first) {
            bounds = bounds
                && layout[first].x_m >= x_low_m - 1.0e-8
                && layout[first].x_m <= x_high_m + 1.0e-8
                && layout[first].y_m >= y_low_m - 1.0e-8
                && layout[first].y_m <= y_high_m + 1.0e-8;
            for (std::size_t second = first + 1; second < layout.size(); ++second) {
                const double separation = distance(layout[first], layout[second]);
                result.minimum_distance_m = std::min(
                    result.minimum_distance_m, separation
                );
                result.spacing_violation_squared_m2 += std::max(
                    0.0, spacing_m * spacing_m - separation * separation
                );
            }
        }
        if (layout.size() < 2U) result.minimum_distance_m = 0.0;
        result.feasible = bounds
            && result.spacing_violation_squared_m2 <= 1.0e-5;
        return result;
    }

    [[nodiscard]] Evaluation evaluate_all(
        const std::vector<Point>& layout,
        const std::vector<Controls>& schedule,
        std::atomic<std::uint64_t>* state_evaluations
    ) const {
        Evaluation result = geometry(layout);
        if (schedule.size() != winds.size()) {
            throw std::invalid_argument("L0373 schedule cardinality mismatch");
        }
        for (std::size_t wind = 0; wind < winds.size(); ++wind) {
            const double power = state_power_mw(
                layout, schedule[wind], winds[wind], state_evaluations
            );
            result.expected_power_mw += winds[wind].probability * power;
            double no_wake = 0.0;
            for (int turbine = 0; turbine < turbines; ++turbine) {
                no_wake += turbine_power_mw(
                    9.0,
                    schedule[wind].yaw_degrees[static_cast<std::size_t>(turbine)],
                    schedule[wind].axial_induction[
                        static_cast<std::size_t>(turbine)
                    ]
                );
            }
            result.no_wake_power_mw += winds[wind].probability * no_wake;
        }
        result.aep_gwh = result.expected_power_mw * kHoursPerYear / 1000.0;
        result.efficiency_percent = result.no_wake_power_mw > 0.0
            ? 100.0 * result.expected_power_mw / result.no_wake_power_mw
            : 0.0;
        return result;
    }

    [[nodiscard]] bool feasible(const std::vector<Point>& layout) const {
        return geometry(layout).feasible;
    }

    void repair(
        std::vector<Point>& layout,
        const fode::CounterRng& random,
        const std::uint64_t generation,
        const std::uint64_t individual
    ) const {
        if (layout.size() != static_cast<std::size_t>(turbines)) {
            throw std::invalid_argument("L0373 repair cardinality mismatch");
        }
        for (Point& point : layout) {
            point.x_m = std::clamp(point.x_m, x_low_m, x_high_m);
            point.y_m = std::clamp(point.y_m, y_low_m, y_high_m);
        }
        if (!(spacing_m > 0.0)) return;
        for (int pass = 0; pass < 80; ++pass) {
            bool clean = true;
            for (std::size_t second = 1; second < layout.size(); ++second) {
                for (std::size_t first = 0; first < second; ++first) {
                    double dx = layout[second].x_m - layout[first].x_m;
                    double dy = layout[second].y_m - layout[first].y_m;
                    double separation = std::hypot(dx, dy);
                    if (separation >= spacing_m - 1.0e-7) continue;
                    clean = false;
                    if (!(separation > 1.0e-12)) {
                        const double angle = 2.0 * kPi * random.uniform(
                            generation,
                            900U + static_cast<std::uint64_t>(pass),
                            individual,
                            first,
                            second
                        );
                        dx = std::cos(angle);
                        dy = std::sin(angle);
                        separation = 1.0;
                    }
                    const double shift = 0.505 * (spacing_m - separation);
                    const double ux = dx / separation;
                    const double uy = dy / separation;
                    layout[first].x_m = std::clamp(
                        layout[first].x_m - shift * ux, x_low_m, x_high_m
                    );
                    layout[first].y_m = std::clamp(
                        layout[first].y_m - shift * uy, y_low_m, y_high_m
                    );
                    layout[second].x_m = std::clamp(
                        layout[second].x_m + shift * ux, x_low_m, x_high_m
                    );
                    layout[second].y_m = std::clamp(
                        layout[second].y_m + shift * uy, y_low_m, y_high_m
                    );
                }
            }
            if (clean) return;
        }
        if (!feasible(layout)) layout = initial();
    }
};

namespace {

struct PsoParticle {
    std::vector<Point> layout;
    std::vector<Point> velocity;
    std::vector<Point> personal_best;
    Evaluation evaluation;
    Evaluation personal_evaluation;
};

bool better(const Evaluation& left, const Evaluation& right) {
    if (left.feasible != right.feasible) return left.feasible;
    if (left.feasible) return left.aep_gwh > right.aep_gwh;
    const double left_penalized_watts = left.expected_power_mw * 1.0e6
        - kPenaltyFactor * left.spacing_violation_squared_m2;
    const double right_penalized_watts = right.expected_power_mw * 1.0e6
        - kPenaltyFactor * right.spacing_violation_squared_m2;
    return left_penalized_watts > right_penalized_watts;
}

std::vector<Point> pso_warm_start(
    const Problem::Impl& problem,
    const RunConfig& config,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& state_evaluations,
    std::uint64_t& layout_evaluations
) {
    const fode::CounterRng random(config.seed);
    const auto greedy = greedy_schedule(
        static_cast<int>(problem.winds.size()), problem.turbines
    );
    std::vector<Point> best_layout = problem.initial();
    Evaluation best_evaluation = problem.evaluate_all(
        best_layout, greedy, &state_evaluations
    );
    ++layout_evaluations;
    for (int trial = 0; trial < config.pso_trials; ++trial) {
        std::vector<PsoParticle> swarm(
            static_cast<std::size_t>(config.pso_population)
        );
        executor.parallel_for(0, config.pso_population, [&](const int particle) {
            auto& item = swarm[static_cast<std::size_t>(particle)];
            item.layout = problem.initial();
            item.velocity.resize(item.layout.size());
            for (std::size_t turbine = 0; turbine < item.layout.size(); ++turbine) {
                const double x_range = problem.x_high_m - problem.x_low_m;
                const double y_range = problem.y_high_m - problem.y_low_m;
                if (particle != 0 || trial != 0) {
                    item.layout[turbine].x_m += 0.20 * x_range * random.normal(
                        trial, 100, particle, turbine, 0
                    );
                    item.layout[turbine].y_m += 0.20 * y_range * random.normal(
                        trial, 100, particle, turbine, 1
                    );
                }
                item.velocity[turbine] = {
                    0.10 * x_range * (2.0 * random.uniform(
                        trial, 101, particle, turbine, 0
                    ) - 1.0),
                    0.10 * y_range * (2.0 * random.uniform(
                        trial, 101, particle, turbine, 1
                    ) - 1.0),
                };
            }
            problem.repair(item.layout, random, trial, particle);
        });
        auto evaluate_swarm = [&]() {
            executor.parallel_for(0, config.pso_population, [&](const int particle) {
                auto& item = swarm[static_cast<std::size_t>(particle)];
                item.evaluation = problem.evaluate_all(
                    item.layout, greedy, &state_evaluations
                );
            });
            layout_evaluations += static_cast<std::uint64_t>(
                config.pso_population
            );
        };
        evaluate_swarm();
        for (PsoParticle& item : swarm) {
            item.personal_best = item.layout;
            item.personal_evaluation = item.evaluation;
            if (better(item.evaluation, best_evaluation)) {
                best_evaluation = item.evaluation;
                best_layout = item.layout;
            }
        }
        for (int iteration = 1; iteration <= config.pso_iterations; ++iteration) {
            const std::vector<Point> global = best_layout;
            const std::uint64_t random_epoch =
                (static_cast<std::uint64_t>(trial) << 32U)
                | static_cast<std::uint32_t>(iteration);
            executor.parallel_for(0, config.pso_population, [&](const int particle) {
                auto& item = swarm[static_cast<std::size_t>(particle)];
                for (std::size_t turbine = 0; turbine < item.layout.size(); ++turbine) {
                    const double r1x = random.uniform(
                        random_epoch, 110, particle, turbine, 0
                    );
                    const double r2x = random.uniform(
                        random_epoch, 111, particle, turbine, 0
                    );
                    const double r1y = random.uniform(
                        random_epoch, 112, particle, turbine, 0
                    );
                    const double r2y = random.uniform(
                        random_epoch, 113, particle, turbine, 0
                    );
                    item.velocity[turbine].x_m =
                        0.7298 * item.velocity[turbine].x_m
                        + 1.49618 * r1x * (
                            item.personal_best[turbine].x_m
                            - item.layout[turbine].x_m
                        )
                        + 1.49618 * r2x * (
                            global[turbine].x_m - item.layout[turbine].x_m
                        );
                    item.velocity[turbine].y_m =
                        0.7298 * item.velocity[turbine].y_m
                        + 1.49618 * r1y * (
                            item.personal_best[turbine].y_m
                            - item.layout[turbine].y_m
                        )
                        + 1.49618 * r2y * (
                            global[turbine].y_m - item.layout[turbine].y_m
                        );
                    item.layout[turbine].x_m += item.velocity[turbine].x_m;
                    item.layout[turbine].y_m += item.velocity[turbine].y_m;
                }
                problem.repair(
                    item.layout, random,
                    static_cast<std::uint64_t>(trial * 1000 + iteration),
                    static_cast<std::uint64_t>(particle)
                );
            });
            evaluate_swarm();
            for (PsoParticle& item : swarm) {
                if (better(item.evaluation, item.personal_evaluation)) {
                    item.personal_evaluation = item.evaluation;
                    item.personal_best = item.layout;
                }
                if (better(item.evaluation, best_evaluation)) {
                    best_evaluation = item.evaluation;
                    best_layout = item.layout;
                }
            }
        }
    }
    return best_layout;
}

Controls optimize_one_control(
    const Problem::Impl& problem,
    const std::vector<Point>& layout,
    const std::size_t wind_index,
    Controls controls,
    const int passes,
    std::atomic<std::uint64_t>& state_evaluations
) {
    auto power = [&](const Controls& candidate) {
        return problem.state_power_mw(
            layout, candidate, problem.winds[wind_index], &state_evaluations
        );
    };
    double current = power(controls);
    for (int pass = 0; pass < passes; ++pass) {
        const double yaw_step = 10.0 * std::pow(0.55, pass);
        const double axial_step = 0.04 * std::pow(0.55, pass);
        for (int turbine = 0; turbine < problem.turbines; ++turbine) {
            const std::size_t index = static_cast<std::size_t>(turbine);
            for (const double sign : {-1.0, 1.0}) {
                Controls candidate = controls;
                candidate.yaw_degrees[index] = std::clamp(
                    candidate.yaw_degrees[index] + sign * yaw_step,
                    kYawMinimumDegrees, kYawMaximumDegrees
                );
                const double value = power(candidate);
                if (value > current) {
                    controls = std::move(candidate);
                    current = value;
                }
            }
            for (const double sign : {-1.0, 1.0}) {
                Controls candidate = controls;
                candidate.axial_induction[index] = std::clamp(
                    candidate.axial_induction[index] + sign * axial_step,
                    kAxialMinimum, kAxialMaximum
                );
                const double value = power(candidate);
                if (value > current) {
                    controls = std::move(candidate);
                    current = value;
                }
            }
        }
    }
    return controls;
}

std::vector<Controls> optimize_schedule(
    const Problem::Impl& problem,
    const std::vector<Point>& layout,
    std::vector<Controls> schedule,
    const int passes,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& state_evaluations
) {
    executor.parallel_for(0, static_cast<int>(problem.winds.size()), [&](const int wind) {
        const std::size_t index = static_cast<std::size_t>(wind);
        schedule[index] = optimize_one_control(
            problem, layout, index, std::move(schedule[index]), passes,
            state_evaluations
        );
    });
    return schedule;
}

struct DbhmLocal {
    std::vector<Point> layout;
    Controls controls;
    std::vector<Point> lambda;
};

double augmented_objective(
    const Problem::Impl& problem,
    const std::size_t wind,
    const std::vector<Point>& global,
    const DbhmLocal& local,
    std::atomic<std::uint64_t>& state_evaluations
) {
    const double power_watts = 1.0e6 * problem.state_power_mw(
        local.layout, local.controls, problem.winds[wind], &state_evaluations
    );
    double result = -problem.winds[wind].probability * power_watts;
    for (std::size_t turbine = 0; turbine < global.size(); ++turbine) {
        const double dx = global[turbine].x_m - local.layout[turbine].x_m;
        const double dy = global[turbine].y_m - local.layout[turbine].y_m;
        result += local.lambda[turbine].x_m * dx
            + local.lambda[turbine].y_m * dy
            + kDbhmMu * (dx * dx + dy * dy);
    }
    return result;
}

void optimize_local_subproblem(
    const Problem::Impl& problem,
    const std::size_t wind,
    const std::vector<Point>& global,
    const int outer_iteration,
    DbhmLocal& local,
    std::atomic<std::uint64_t>& state_evaluations
) {
    double current = augmented_objective(
        problem, wind, global, local, state_evaluations
    );
    const double position_step = 45.0 * std::pow(0.78, outer_iteration);
    const double yaw_step = 7.5 * std::pow(0.78, outer_iteration);
    const double axial_step = 0.025 * std::pow(0.78, outer_iteration);
    for (int turbine = 0; turbine < problem.turbines; ++turbine) {
        const std::size_t index = static_cast<std::size_t>(turbine);
        for (int coordinate = 0; coordinate < 2; ++coordinate) {
            for (const double sign : {-1.0, 1.0}) {
                DbhmLocal candidate = local;
                double& value = coordinate == 0
                    ? candidate.layout[index].x_m
                    : candidate.layout[index].y_m;
                value += sign * position_step;
                value = coordinate == 0
                    ? std::clamp(value, problem.x_low_m, problem.x_high_m)
                    : std::clamp(value, problem.y_low_m, problem.y_high_m);
                if (!problem.feasible(candidate.layout)) continue;
                const double objective = augmented_objective(
                    problem, wind, global, candidate, state_evaluations
                );
                if (objective < current) {
                    local = std::move(candidate);
                    current = objective;
                }
            }
        }
        for (const double sign : {-1.0, 1.0}) {
            DbhmLocal candidate = local;
            candidate.controls.yaw_degrees[index] = std::clamp(
                candidate.controls.yaw_degrees[index] + sign * yaw_step,
                kYawMinimumDegrees, kYawMaximumDegrees
            );
            const double objective = augmented_objective(
                problem, wind, global, candidate, state_evaluations
            );
            if (objective < current) {
                local = std::move(candidate);
                current = objective;
            }
        }
        for (const double sign : {-1.0, 1.0}) {
            DbhmLocal candidate = local;
            candidate.controls.axial_induction[index] = std::clamp(
                candidate.controls.axial_induction[index] + sign * axial_step,
                kAxialMinimum, kAxialMaximum
            );
            const double objective = augmented_objective(
                problem, wind, global, candidate, state_evaluations
            );
            if (objective < current) {
                local = std::move(candidate);
                current = objective;
            }
        }
    }
}

struct DbhmResult {
    std::vector<Point> layout;
    std::vector<Controls> controls;
    int iterations = 0;
    double consensus_m = 0.0;
};

DbhmResult run_dbhm(
    const Problem::Impl& problem,
    const RunConfig& config,
    const std::vector<Point>& warm_layout,
    const std::vector<Controls>& warm_controls,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& state_evaluations
) {
    const fode::CounterRng random(config.seed);
    std::vector<Point> global = warm_layout;
    std::vector<DbhmLocal> locals(problem.winds.size());
    for (std::size_t wind = 0; wind < locals.size(); ++wind) {
        locals[wind] = {
            warm_layout,
            warm_controls[wind],
            std::vector<Point>(warm_layout.size()),
        };
    }
    std::vector<Point> incumbent_layout = warm_layout;
    std::vector<Controls> incumbent_controls = warm_controls;
    Evaluation incumbent = problem.evaluate_all(
        incumbent_layout, incumbent_controls, &state_evaluations
    );
    double consensus = std::numeric_limits<double>::infinity();
    int completed = 0;
    for (int iteration = 0; iteration < config.dbhm_iterations; ++iteration) {
        const std::vector<Point> frozen_global = global;
        executor.parallel_for(0, static_cast<int>(locals.size()), [&](const int wind) {
            optimize_local_subproblem(
                problem, static_cast<std::size_t>(wind), frozen_global,
                iteration, locals[static_cast<std::size_t>(wind)],
                state_evaluations
            );
        });
        std::vector<Point> coordinated(global.size());
        for (std::size_t turbine = 0; turbine < global.size(); ++turbine) {
            for (const DbhmLocal& local : locals) {
                coordinated[turbine].x_m += local.layout[turbine].x_m
                    - local.lambda[turbine].x_m / (2.0 * kDbhmMu);
                coordinated[turbine].y_m += local.layout[turbine].y_m
                    - local.lambda[turbine].y_m / (2.0 * kDbhmMu);
            }
            coordinated[turbine].x_m /= static_cast<double>(locals.size());
            coordinated[turbine].y_m /= static_cast<double>(locals.size());
        }
        problem.repair(
            coordinated, random,
            static_cast<std::uint64_t>(2000 + iteration), 0
        );
        global = std::move(coordinated);
        consensus = 0.0;
        double x_norm_sum = 0.0;
        double y_norm_sum = 0.0;
        for (std::size_t wind = 0; wind < locals.size(); ++wind) {
            double x_squared = 0.0;
            double y_squared = 0.0;
            for (std::size_t turbine = 0; turbine < global.size(); ++turbine) {
                const double dx = global[turbine].x_m
                    - locals[wind].layout[turbine].x_m;
                const double dy = global[turbine].y_m
                    - locals[wind].layout[turbine].y_m;
                x_squared += dx * dx;
                y_squared += dy * dy;
                locals[wind].lambda[turbine].x_m += 2.0 * kDbhmMu * dx;
                locals[wind].lambda[turbine].y_m += 2.0 * kDbhmMu * dy;
            }
            x_norm_sum += std::sqrt(x_squared);
            y_norm_sum += std::sqrt(y_squared);
        }
        consensus = x_norm_sum + y_norm_sum;
        std::vector<Controls> candidate_controls;
        candidate_controls.reserve(locals.size());
        for (const DbhmLocal& local : locals) {
            candidate_controls.push_back(local.controls);
        }
        const Evaluation candidate = problem.evaluate_all(
            global, candidate_controls, &state_evaluations
        );
        if (better(candidate, incumbent)) {
            incumbent = candidate;
            incumbent_layout = global;
            incumbent_controls = std::move(candidate_controls);
        }
        completed = iteration + 1;
        if (consensus < kConsensusToleranceM) break;
    }
    return {
        std::move(incumbent_layout), std::move(incumbent_controls),
        completed, consensus,
    };
}

CaseResult make_case(
    std::string role,
    std::vector<Point> layout,
    std::vector<Controls> controls,
    const Problem::Impl& problem,
    std::atomic<std::uint64_t>& state_evaluations
) {
    const Evaluation evaluation = problem.evaluate_all(
        layout, controls, &state_evaluations
    );
    return {
        std::move(role), std::move(layout), std::move(controls), evaluation,
    };
}

RunResult run_illustrative(
    const Problem::Impl& problem,
    const RunConfig& config,
    fode::PersistentExecutor& executor,
    std::atomic<std::uint64_t>& state_evaluations,
    const Clock::time_point started
) {
    RunResult result;
    result.profile_id = problem.id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    const int begin = problem.spacing_m > 0.0
        ? static_cast<int>(std::ceil(problem.spacing_m)) : 1;
    const int end = problem.spacing_m > 0.0
        ? static_cast<int>(std::floor(1100.0 - problem.spacing_m)) : 1099;
    std::vector<Point> best_greedy;
    Evaluation best_greedy_evaluation;
    std::vector<Point> best_joint;
    std::vector<Controls> best_joint_controls;
    Evaluation best_joint_evaluation;
    for (int middle = begin; middle <= end; ++middle) {
        std::vector<Point> layout{{0.0, 0.0}, {
            static_cast<double>(middle), 0.0
        }, {1100.0, 0.0}};
        auto greedy = greedy_schedule(1, 3);
        const Evaluation greedy_evaluation = problem.evaluate_all(
            layout, greedy, &state_evaluations
        );
        ++result.complete_layout_evaluations;
        if (best_greedy.empty() || better(
            greedy_evaluation, best_greedy_evaluation
        )) {
            best_greedy = layout;
            best_greedy_evaluation = greedy_evaluation;
        }
        auto controlled = optimize_schedule(
            problem, layout, std::move(greedy), config.control_passes,
            executor, state_evaluations
        );
        const Evaluation joint_evaluation = problem.evaluate_all(
            layout, controlled, &state_evaluations
        );
        ++result.complete_layout_evaluations;
        if (best_joint.empty() || better(joint_evaluation, best_joint_evaluation)) {
            best_joint = layout;
            best_joint_controls = std::move(controlled);
            best_joint_evaluation = joint_evaluation;
        }
    }
    auto sequential_controls = optimize_schedule(
        problem, best_greedy, greedy_schedule(1, 3), config.control_passes,
        executor, state_evaluations
    );
    result.cases.push_back(make_case(
        "greedy_layout", best_greedy, greedy_schedule(1, 3),
        problem, state_evaluations
    ));
    result.cases.push_back(make_case(
        "sequential_layout_then_control", best_greedy,
        std::move(sequential_controls), problem, state_evaluations
    ));
    result.cases.push_back(make_case(
        "joint_layout_and_control", best_joint,
        std::move(best_joint_controls), problem, state_evaluations
    ));
    result.single_wind_state_evaluations = state_evaluations.load();
    result.end_to_end_seconds = elapsed_seconds(started);
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    return result;
}

std::uint64_t scientific_hash(const RunResult& result) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const CaseResult& role : result.cases) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(role.layout.size()));
        for (const Point& point : role.layout) {
            hash = mix_hash(hash, quantized(point.x_m));
            hash = mix_hash(hash, quantized(point.y_m));
        }
        hash = mix_hash(hash, quantized(role.evaluation.aep_gwh));
        hash = mix_hash(hash, quantized(role.evaluation.expected_power_mw));
        for (const Controls& controls : role.controls_by_wind) {
            for (const double yaw : controls.yaw_degrees) {
                hash = mix_hash(hash, quantized(yaw));
            }
            for (const double axial : controls.axial_induction) {
                hash = mix_hash(hash, quantized(axial));
            }
        }
    }
    return hash;
}

}  // namespace

Problem::Problem(const ProfileId profile)
    : impl_(std::make_unique<Impl>(profile)) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
ProfileId Problem::profile() const noexcept { return impl_->profile; }
const std::string& Problem::id() const noexcept { return impl_->id; }
int Problem::turbine_count() const noexcept { return impl_->turbines; }
double Problem::width_m() const noexcept {
    return impl_->x_high_m - impl_->x_low_m;
}
double Problem::height_m() const noexcept {
    return impl_->y_high_m - impl_->y_low_m;
}
double Problem::minimum_spacing_m() const noexcept { return impl_->spacing_m; }
const std::vector<WindState>& Problem::winds() const noexcept {
    return impl_->winds;
}
std::vector<Point> Problem::initial_layout() const { return impl_->initial(); }
Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const std::vector<Controls>& controls_by_wind
) const {
    return impl_->evaluate_all(layout, controls_by_wind, nullptr);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.pso_trials < 1
        || config.pso_population < 8 || config.pso_iterations < 1
        || config.control_passes < 1 || config.dbhm_iterations < 1) {
        throw std::invalid_argument("L0373 invalid run configuration");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::atomic<std::uint64_t> state_evaluations{0};
    if (problem.impl_->turbines == 3) {
        RunResult result = run_illustrative(
            *problem.impl_, config, executor, state_evaluations, started
        );
        result.scientific_hash = scientific_hash(result);
        return result;
    }
    RunResult result;
    result.profile_id = problem.impl_->id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.pso_trials = config.pso_trials;
    result.pso_population = config.pso_population;
    result.pso_iterations = config.pso_iterations;

    const auto initial = problem.impl_->initial();
    const auto greedy = greedy_schedule(
        static_cast<int>(problem.impl_->winds.size()), problem.impl_->turbines
    );
    result.cases.push_back(make_case(
        "case1_initial_layout_greedy_control", initial, greedy,
        *problem.impl_, state_evaluations
    ));

    const auto control_started = Clock::now();
    auto initial_controlled = optimize_schedule(
        *problem.impl_, initial, greedy, config.control_passes,
        executor, state_evaluations
    );
    result.control_stage_seconds += elapsed_seconds(control_started);
    result.cases.push_back(make_case(
        "case2_initial_layout_optimized_control", initial,
        std::move(initial_controlled), *problem.impl_, state_evaluations
    ));

    const auto pso_started = Clock::now();
    const std::vector<Point> pso_layout = pso_warm_start(
        *problem.impl_, config, executor, state_evaluations,
        result.complete_layout_evaluations
    );
    result.isolated_layout_stage_seconds = elapsed_seconds(pso_started);
    result.cases.push_back(make_case(
        "case3_optimized_layout_greedy_control", pso_layout, greedy,
        *problem.impl_, state_evaluations
    ));

    const auto sequential_control_started = Clock::now();
    auto sequential_controls = optimize_schedule(
        *problem.impl_, pso_layout, greedy, config.control_passes,
        executor, state_evaluations
    );
    result.control_stage_seconds += elapsed_seconds(sequential_control_started);
    result.cases.push_back(make_case(
        "case4_optimized_layout_sequential_control", pso_layout,
        sequential_controls, *problem.impl_, state_evaluations
    ));

    const auto dbhm_started = Clock::now();
    DbhmResult dbhm = run_dbhm(
        *problem.impl_, config, pso_layout, sequential_controls,
        executor, state_evaluations
    );
    dbhm.controls = optimize_schedule(
        *problem.impl_, dbhm.layout, std::move(dbhm.controls),
        config.control_passes, executor, state_evaluations
    );
    result.dbhm_stage_seconds = elapsed_seconds(dbhm_started);
    result.dbhm_iterations_completed = dbhm.iterations;
    result.final_consensus_violation_m = dbhm.consensus_m;
    result.cases.push_back(make_case(
        "case5_joint_layout_control_dbhm", std::move(dbhm.layout),
        std::move(dbhm.controls), *problem.impl_, state_evaluations
    ));

    result.single_wind_state_evaluations = state_evaluations.load();
    result.end_to_end_seconds = elapsed_seconds(started);
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    result.scientific_hash = scientific_hash(result);
    return result;
}

std::vector<ProfileId> paper_profiles() {
    return {
        ProfileId::illustrative_unrestricted,
        ProfileId::illustrative_4d,
        ProfileId::turbines16_directions36,
        ProfileId::turbines16_directions360,
        ProfileId::turbines80_directions12,
        ProfileId::turbines80_directions180,
    };
}

std::string to_string(const ProfileId value) {
    switch (value) {
        case ProfileId::illustrative_unrestricted:
            return "illustrative-unrestricted";
        case ProfileId::illustrative_4d:
            return "illustrative-4d";
        case ProfileId::turbines16_directions36:
            return "n16-w36";
        case ProfileId::turbines16_directions360:
            return "n16-w360";
        case ProfileId::turbines80_directions12:
            return "n80-w12";
        case ProfileId::turbines80_directions180:
            return "n80-w180";
    }
    throw std::invalid_argument("L0373 unknown profile");
}

}  // namespace core99::l0373
