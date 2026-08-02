/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T85 yawed-Gaussian evaluator, AGLDPSO, and CPU-HPC path
Paper/DOI: Particle Swarm Optimization of a Wind Farm Layout with Active
Control of Turbine Yaws; 10.1016/j.renene.2023.02.058
Public source, cited predecessor, missing assets, reconstruction decisions,
semantic IDs, production backend, and claim boundary:
hpc/core99_cpp/include/core99/song_t85.hpp
HPC design: immutable wind trigonometry, turbine tables, and rotor samples are
precomputed; the persistent worker team performs initialization, normalized
LSH projection, independent subpopulation updates, repair, and population
evaluation. Each layout remains internally serial to avoid nested
oversubscription. Counter-keyed random events and fixed-order reductions
retain bitwise one/all-core trajectories.
Controlling contract: shared/contracts/core99_t85_song_2023.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/song_t85.hpp"

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

namespace core99::t85 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kReferenceHeightM = 25.0;
constexpr double kShearExponent = 0.1;
constexpr double kYawMinDeg = -30.0;
constexpr double kYawMaxDeg = 30.0;
constexpr double kDeltaStar = 0.607;
constexpr double kZeta = 0.75;
constexpr double kWakeExpansion = 0.0125;
constexpr double kCOne = 1.0;
constexpr double kCTwo = 0.1;
constexpr int kRotorSamples = 8;

const std::array<std::pair<double, double>, kRotorSamples>
    kRotorUnitSamples = [] {
        std::array<std::pair<double, double>, kRotorSamples> result{};
        for (int index = 0; index < kRotorSamples; ++index) {
            const int ring = index / 4;
            const int angular = index % 4;
            const double radius =
                std::sqrt((static_cast<double>(ring) + 0.5) / 2.0);
            const double angle =
                (static_cast<double>(angular) + 0.5 * ring)
                * std::numbers::pi / 2.0;
            result[static_cast<std::size_t>(index)] = {
                radius * std::cos(angle),
                radius * std::sin(angle),
            };
        }
        return result;
    }();

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double interpolate_table(
    const std::vector<double>& values,
    const double speed
) {
    if (!(speed > 0.0) || values.empty()) {
        return 0.0;
    }
    const double maximum =
        static_cast<double>(values.size() - 1U);
    if (speed >= maximum) {
        return values.back();
    }
    const int lower = static_cast<int>(std::floor(speed));
    const int upper = lower + 1;
    const double fraction = speed - static_cast<double>(lower);
    return values[static_cast<std::size_t>(lower)]
        + fraction
            * (
                values[static_cast<std::size_t>(upper)]
                - values[static_cast<std::size_t>(lower)]
            );
}

double signed_power(const double value, const double exponent) {
    if (value == 0.0) {
        return 0.0;
    }
    return std::copysign(std::pow(std::abs(value), exponent), value);
}

struct Particle {
    std::vector<double> position;
    std::vector<double> velocity;
    Evaluation evaluation;
};

double violation(const Evaluation& evaluation) {
    return evaluation.spacing_violation_m
        + evaluation.boundary_violation_m;
}

bool better(
    const Evaluation& left,
    const Evaluation& right
) {
    if (left.feasible != right.feasible) {
        return left.feasible;
    }
    if (!left.feasible) {
        if (violation(left) != violation(right)) {
            return violation(left) < violation(right);
        }
    }
    return left.aep_gwh > right.aep_gwh;
}

std::vector<double> flatten(
    const std::vector<TurbineDecision>& layout,
    const int wind_count
) {
    const int stride = wind_count + 2;
    std::vector<double> result(
        layout.size() * static_cast<std::size_t>(stride)
    );
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        const std::size_t base =
            turbine * static_cast<std::size_t>(stride);
        result[base] = layout[turbine].x_m;
        result[base + 1U] = layout[turbine].y_m;
        for (int wind = 0; wind < wind_count; ++wind) {
            result[
                base + 2U + static_cast<std::size_t>(wind)
            ] = layout[turbine].yaw_deg[static_cast<std::size_t>(wind)];
        }
    }
    return result;
}

std::vector<TurbineDecision> expand(
    const std::vector<double>& position,
    const int turbine_count,
    const int wind_count
) {
    const int stride = wind_count + 2;
    if (
        position.size()
        != static_cast<std::size_t>(turbine_count * stride)
    ) {
        throw std::invalid_argument("T85 decision dimension mismatch");
    }
    std::vector<TurbineDecision> result(
        static_cast<std::size_t>(turbine_count)
    );
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        const std::size_t base =
            static_cast<std::size_t>(turbine * stride);
        auto& decision = result[static_cast<std::size_t>(turbine)];
        decision.x_m = position[base];
        decision.y_m = position[base + 1U];
        decision.yaw_deg.resize(static_cast<std::size_t>(wind_count));
        for (int wind = 0; wind < wind_count; ++wind) {
            decision.yaw_deg[static_cast<std::size_t>(wind)] =
                position[
                    base + 2U + static_cast<std::size_t>(wind)
                ];
        }
    }
    return result;
}

void hash_mix(std::uint64_t& state, const std::uint64_t value) {
    state ^= value
        + 0x9e3779b97f4a7c15ULL
        + (state << 6)
        + (state >> 2);
}

std::uint64_t scientific_hash(
    const Particle& best_particle,
    const std::uint64_t physical_fes,
    const std::uint64_t generations,
    const int granularity
) {
    std::uint64_t result = 0xcbf29ce484222325ULL;
    for (const double value : best_particle.position) {
        hash_mix(result, std::bit_cast<std::uint64_t>(value));
    }
    hash_mix(
        result,
        std::bit_cast<std::uint64_t>(
            best_particle.evaluation.aep_gwh
        )
    );
    hash_mix(result, physical_fes);
    hash_mix(result, generations);
    hash_mix(result, static_cast<std::uint64_t>(granularity));
    return result;
}

std::vector<double> normalized_position(
    const Particle& particle,
    const Problem& problem
) {
    std::vector<double> result(particle.position.size());
    const int stride = problem.wind_state_count() + 2;
    for (std::size_t coordinate = 0;
         coordinate < particle.position.size();
         ++coordinate) {
        const int component =
            static_cast<int>(coordinate % static_cast<std::size_t>(stride));
        if (component < 2) {
            result[coordinate] =
                particle.position[coordinate] / problem.side_length_m();
        } else {
            result[coordinate] =
                (particle.position[coordinate] - kYawMinDeg)
                / (kYawMaxDeg - kYawMinDeg);
        }
    }
    return result;
}

}  // namespace

Problem::Problem(const CaseId id) : case_id_(id) {
    switch (id) {
    case CaseId::wf1:
        id_ = "t85_wf1_v80_u8_n25";
        break;
    case CaseId::wf1_u6:
        id_ = "t85_wf1_v80_u6_n25";
        break;
    case CaseId::wf1_v112:
        id_ = "t85_wf1_v112_u8_n25";
        break;
    case CaseId::wf2:
        id_ = "t85_wf2_v80_u8_n25";
        side_length_m_ = 1920.0;
        break;
    case CaseId::wf3:
        id_ = "t85_wf3_v80_u8_n36";
        turbine_count_ = 36;
        side_length_m_ = 2400.0;
        break;
    case CaseId::wf4:
        id_ = "t85_wf4_v80_uneven_n25";
        break;
    }

    if (id == CaseId::wf1_v112) {
        turbine_.diameter_m = 112.0;
        turbine_.hub_height_m = 84.0;
        turbine_.rated_power_mw = 3.0;
        turbine_.power_mw = {
            0.0, 0.0, 0.0, 0.0, 0.24, 0.45, 0.74, 1.08, 1.49,
            1.96, 2.43, 2.84, 2.98, 3.00, 3.00, 3.00, 3.00,
            3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00, 3.00,
        };
        turbine_.thrust = {
            0.0, 0.0, 0.0, 0.0, 0.80, 0.82, 0.79, 0.82, 0.80,
            0.79, 0.72, 0.58, 0.39, 0.29, 0.23, 0.19, 0.16,
            0.14, 0.12, 0.105, 0.090, 0.078, 0.068, 0.058, 0.050,
            0.045,
        };
    } else {
        turbine_.power_mw = {
            0.0, 0.0, 0.0, 0.0, 0.12, 0.24, 0.41, 0.61, 0.84,
            1.10, 1.38, 1.63, 1.81, 1.92, 1.97, 1.99, 2.00,
            2.00, 2.00, 2.00, 2.00, 2.00, 2.00, 2.00, 2.00, 2.00,
        };
        turbine_.thrust = {
            0.0, 0.0, 0.0, 0.0, 0.80, 0.82, 0.80, 0.82, 0.81,
            0.80, 0.79, 0.78, 0.70, 0.41, 0.30, 0.24, 0.20,
            0.17, 0.15, 0.13, 0.115, 0.10, 0.088, 0.078, 0.068,
            0.058,
        };
    }

    const double uniform_speed =
        id == CaseId::wf1_u6 ? 6.0 : 8.0;
    for (int direction = 0; direction < 8; ++direction) {
        const double angle =
            static_cast<double>(direction)
            * std::numbers::pi / 4.0;
        double speed = uniform_speed;
        double probability = 0.125;
        if (id == CaseId::wf4) {
            if (direction == 0) {
                speed = 11.0;
                probability = 0.20;
            } else if (direction == 1 || direction == 7) {
                speed = 9.0;
                probability = 0.15;
            } else {
                speed = 8.0;
                probability = 0.10;
            }
        }
        winds_.push_back({
            std::cos(angle),
            std::sin(angle),
            speed,
            probability,
        });
    }
}

const std::string& Problem::id() const noexcept {
    return id_;
}

CaseId Problem::case_id() const noexcept {
    return case_id_;
}

int Problem::turbine_count() const noexcept {
    return turbine_count_;
}

int Problem::wind_state_count() const noexcept {
    return static_cast<int>(winds_.size());
}

int Problem::decision_dimension() const noexcept {
    return turbine_count_ * (wind_state_count() + 2);
}

int Problem::declared_population() const noexcept {
    return 500;
}

std::uint64_t Problem::declared_physical_fes() const noexcept {
    return 10000;
}

int Problem::declared_repeats() const noexcept {
    return 25;
}

double Problem::side_length_m() const noexcept {
    return side_length_m_;
}

double Problem::rotor_diameter_m() const noexcept {
    return turbine_.diameter_m;
}

double Problem::power_mw(const double speed_mps) const {
    if (speed_mps < 4.0 || speed_mps > 25.0) {
        return 0.0;
    }
    return std::clamp(
        interpolate_table(turbine_.power_mw, speed_mps),
        0.0,
        turbine_.rated_power_mw
    );
}

double Problem::thrust(const double speed_mps) const {
    if (speed_mps < 4.0 || speed_mps > 25.0) {
        return 0.0;
    }
    return std::clamp(
        interpolate_table(turbine_.thrust, speed_mps),
        0.0,
        0.95
    );
}

Evaluation Problem::evaluate(
    const std::vector<TurbineDecision>& layout
) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T85 layout cardinality mismatch");
    }
    Evaluation result;
    const double minimum_spacing = turbine_.diameter_m;
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        const auto& item = layout[turbine];
        if (
            item.yaw_deg.size()
            != static_cast<std::size_t>(wind_state_count())
        ) {
            throw std::invalid_argument("T85 yaw cardinality mismatch");
        }
        result.boundary_violation_m +=
            std::max(0.0, -item.x_m)
            + std::max(0.0, item.x_m - side_length_m_)
            + std::max(0.0, -item.y_m)
            + std::max(0.0, item.y_m - side_length_m_);
        for (std::size_t other = 0; other < turbine; ++other) {
            const double distance = std::hypot(
                item.x_m - layout[other].x_m,
                item.y_m - layout[other].y_m
            );
            result.spacing_violation_m +=
                std::max(0.0, minimum_spacing - distance);
        }
    }
    result.feasible =
        result.spacing_violation_m <= 1.0e-9
        && result.boundary_violation_m <= 1.0e-9;

    const std::size_t count = layout.size();
    std::vector<double> along(count);
    std::vector<double> across(count);
    std::vector<double> inflow(count);
    std::vector<int> order(count);
    std::iota(order.begin(), order.end(), 0);
    double expected_power_mw = 0.0;

    for (std::size_t wind_index = 0;
         wind_index < winds_.size();
         ++wind_index) {
        const auto& wind = winds_[wind_index];
        for (std::size_t turbine = 0; turbine < count; ++turbine) {
            along[turbine] =
                wind.along_cos * layout[turbine].x_m
                + wind.along_sin * layout[turbine].y_m;
            across[turbine] =
                -wind.along_sin * layout[turbine].x_m
                + wind.along_cos * layout[turbine].y_m;
        }
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int left, const int right) {
                const auto left_index = static_cast<std::size_t>(left);
                const auto right_index = static_cast<std::size_t>(right);
                if (along[left_index] != along[right_index]) {
                    return along[left_index] < along[right_index];
                }
                return left < right;
            }
        );
        double state_power_mw = 0.0;
        for (std::size_t downstream_position = 0;
             downstream_position < count;
             ++downstream_position) {
            const std::size_t downstream = static_cast<std::size_t>(
                order[downstream_position]
            );
            double rotor_sum = 0.0;
            for (const auto& sample : kRotorUnitSamples) {
                const double sample_cross =
                    across[downstream]
                    + 0.5 * turbine_.diameter_m * sample.first;
                const double sample_height =
                    turbine_.hub_height_m
                    + 0.5 * turbine_.diameter_m * sample.second;
                const double free_speed =
                    wind.reference_speed_mps
                    * std::pow(
                        std::max(1.0, sample_height) / kReferenceHeightM,
                        kShearExponent
                    );
                double deficit_speed = 0.0;
                for (std::size_t upstream_position = 0;
                     upstream_position < downstream_position;
                     ++upstream_position) {
                    const std::size_t upstream =
                        static_cast<std::size_t>(
                            order[upstream_position]
                        );
                    const double downstream_distance =
                        along[downstream] - along[upstream];
                    if (!(downstream_distance > 0.0)) {
                        continue;
                    }
                    const double gamma =
                        layout[upstream].yaw_deg[wind_index]
                        * std::numbers::pi / 180.0;
                    const double cosine_gamma = std::cos(gamma);
                    if (!(cosine_gamma > 0.0)) {
                        continue;
                    }
                    const double ct = thrust(inflow[upstream]);
                    if (!(ct > 0.0)) {
                        continue;
                    }
                    const double ct_star =
                        ct * cosine_gamma * cosine_gamma;
                    const double beta_root = std::sqrt(
                        std::max(
                            1.0e-12,
                            1.0 - ct_star * cosine_gamma
                        )
                    );
                    const double beta =
                        (1.0 + beta_root) / (2.0 * beta_root);
                    const double sigma_yaw =
                        kWakeExpansion * downstream_distance
                            / (turbine_.diameter_m * cosine_gamma)
                        + std::sqrt(beta) / 5.0;
                    const double sigma_z =
                        kWakeExpansion * downstream_distance
                            / turbine_.diameter_m
                        + std::sqrt(beta) / 5.0;
                    const double signed_deflection =
                        signed_power(ct * std::sin(gamma), kZeta);
                    const double offset =
                        turbine_.diameter_m
                        * kDeltaStar * ct
                        * signed_deflection
                        * std::pow(cosine_gamma, 2.0 * kZeta)
                        * std::sqrt(
                            downstream_distance / turbine_.diameter_m
                        );
                    const double vertical =
                        sample_height - turbine_.hub_height_m;
                    const double offset_at_height =
                        offset
                        * std::exp(
                            -0.5 * vertical * vertical
                            / (
                                turbine_.diameter_m
                                * turbine_.diameter_m
                                * sigma_z * sigma_z
                            )
                        );
                    const double cross =
                        sample_cross - across[upstream]
                        - offset_at_height;
                    const double exponent =
                        -0.5 * cross * cross
                            / (
                                turbine_.diameter_m
                                * turbine_.diameter_m
                                * cosine_gamma * cosine_gamma
                                * sigma_yaw * sigma_yaw
                            )
                        -0.5 * vertical * vertical
                            / (
                                turbine_.diameter_m
                                * turbine_.diameter_m
                                * sigma_z * sigma_z
                            );
                    const double radical = std::clamp(
                        1.0
                            - ct_star * cosine_gamma
                                / (8.0 * sigma_yaw * sigma_z),
                        0.0,
                        1.0
                    );
                    const double fraction =
                        (1.0 - std::sqrt(radical))
                        * std::exp(exponent);
                    deficit_speed +=
                        std::max(0.0, inflow[upstream]) * fraction;
                }
                rotor_sum += std::max(0.0, free_speed - deficit_speed);
            }
            inflow[downstream] =
                rotor_sum / static_cast<double>(kRotorSamples);
            const double target_yaw =
                layout[downstream].yaw_deg[wind_index]
                * std::numbers::pi / 180.0;
            state_power_mw +=
                power_mw(inflow[downstream])
                * std::max(0.0, std::cos(target_yaw));
        }
        const double contribution_gwh =
            8.76 * wind.probability * state_power_mw;
        result.wind_aep_contribution_gwh[wind_index] =
            contribution_gwh;
        expected_power_mw += wind.probability * state_power_mw;
    }
    result.aep_gwh = 8.76 * expected_power_mw;
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<TurbineDecision>>& layouts,
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

std::vector<TurbineDecision> Problem::reference_layout() const {
    const int columns = turbine_count_ == 36 ? 6 : 5;
    const int rows = columns;
    std::vector<TurbineDecision> result;
    result.reserve(static_cast<std::size_t>(turbine_count_));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            TurbineDecision decision;
            decision.x_m =
                side_length_m_
                * static_cast<double>(column)
                / static_cast<double>(columns - 1);
            decision.y_m =
                side_length_m_
                * static_cast<double>(row)
                / static_cast<double>(rows - 1);
            decision.yaw_deg.assign(
                static_cast<std::size_t>(wind_state_count()), 0.0
            );
            result.push_back(std::move(decision));
        }
    }
    return result;
}

void Problem::repair(
    std::vector<TurbineDecision>& layout,
    const std::uint64_t seed,
    const std::uint64_t generation,
    const std::uint64_t particle
) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T85 repair cardinality mismatch");
    }
    const fode::CounterRng rng(seed);
    for (auto& decision : layout) {
        decision.x_m = std::clamp(decision.x_m, 0.0, side_length_m_);
        decision.y_m = std::clamp(decision.y_m, 0.0, side_length_m_);
        for (double& yaw : decision.yaw_deg) {
            yaw = std::clamp(yaw, kYawMinDeg, kYawMaxDeg);
        }
    }
    const double required = turbine_.diameter_m * (1.0 + 1.0e-9);
    for (int pass = 0; pass < 12; ++pass) {
        bool clean = true;
        for (std::size_t right = 1; right < layout.size(); ++right) {
            for (std::size_t left = 0; left < right; ++left) {
                double dx = layout[right].x_m - layout[left].x_m;
                double dy = layout[right].y_m - layout[left].y_m;
                double distance = std::hypot(dx, dy);
                if (distance >= required) {
                    continue;
                }
                clean = false;
                if (!(distance > 1.0e-12)) {
                    const double angle =
                        2.0 * std::numbers::pi
                        * rng.uniform(
                            generation,
                            91,
                            particle,
                            static_cast<std::uint64_t>(right),
                            static_cast<std::uint64_t>(
                                pass * turbine_count_
                                + static_cast<int>(left)
                            )
                        );
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    distance = 1.0;
                }
                const double shift = required - distance;
                const double unit_x = dx / distance;
                const double unit_y = dy / distance;
                layout[right].x_m = std::clamp(
                    layout[right].x_m + shift * unit_x,
                    0.0,
                    side_length_m_
                );
                layout[right].y_m = std::clamp(
                    layout[right].y_m + shift * unit_y,
                    0.0,
                    side_length_m_
                );
            }
        }
        if (clean) {
            return;
        }
    }
}

RunResult run(
    const Problem& problem,
    const RunConfig& config
) {
    if (
        config.workers <= 0
        || config.population < 20
        || config.physical_fes_limit
            < static_cast<std::uint64_t>(config.population)
    ) {
        throw std::invalid_argument("invalid T85 run configuration");
    }
    const auto total_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int dimension = problem.decision_dimension();
    const int wind_count = problem.wind_state_count();
    const int stride = wind_count + 2;
    std::vector<Particle> population(
        static_cast<std::size_t>(config.population)
    );

    auto algorithm_started = Clock::now();
    executor.parallel_for(0, config.population, [&](const int index) {
        auto layout = problem.reference_layout();
        if (index != 0) {
            for (int turbine = 0;
                 turbine < problem.turbine_count();
                 ++turbine) {
                auto& item = layout[static_cast<std::size_t>(turbine)];
                item.x_m = problem.side_length_m()
                    * rng.uniform(
                        0,
                        10,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(turbine),
                        0
                    );
                item.y_m = problem.side_length_m()
                    * rng.uniform(
                        0,
                        10,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(turbine),
                        1
                    );
                for (int wind = 0; wind < wind_count; ++wind) {
                    item.yaw_deg[static_cast<std::size_t>(wind)] =
                        kYawMinDeg
                        + (kYawMaxDeg - kYawMinDeg)
                            * rng.uniform(
                                0,
                                10,
                                static_cast<std::uint64_t>(index),
                                static_cast<std::uint64_t>(turbine),
                                static_cast<std::uint64_t>(wind + 2)
                            );
                }
            }
            problem.repair(
                layout,
                config.seed,
                0,
                static_cast<std::uint64_t>(index)
            );
        }
        Particle& particle =
            population[static_cast<std::size_t>(index)];
        particle.position = flatten(layout, wind_count);
        particle.velocity.resize(
            static_cast<std::size_t>(dimension)
        );
        for (int coordinate = 0; coordinate < dimension; ++coordinate) {
            const int component = coordinate % stride;
            const double range = component < 2
                ? problem.side_length_m()
                : kYawMaxDeg - kYawMinDeg;
            particle.velocity[static_cast<std::size_t>(coordinate)] =
                0.1 * range
                * (
                    2.0
                        * rng.uniform(
                            0,
                            11,
                            static_cast<std::uint64_t>(index),
                            static_cast<std::uint64_t>(coordinate),
                            0
                        )
                    - 1.0
                );
        }
    });
    double algorithm_seconds = elapsed_seconds(algorithm_started);
    double evaluator_seconds = 0.0;

    auto evaluate_particles = [&](std::vector<Particle>& particles) {
        std::vector<std::vector<TurbineDecision>> layouts;
        layouts.reserve(particles.size());
        for (const Particle& particle : particles) {
            layouts.push_back(
                expand(
                    particle.position,
                    problem.turbine_count(),
                    wind_count
                )
            );
        }
        const auto started = Clock::now();
        const auto evaluations =
            problem.evaluate_population(layouts, executor);
        evaluator_seconds += elapsed_seconds(started);
        for (std::size_t index = 0; index < particles.size(); ++index) {
            particles[index].evaluation = evaluations[index];
        }
    };

    evaluate_particles(population);
    std::uint64_t physical_fes =
        static_cast<std::uint64_t>(config.population);
    auto best_index = [&]() {
        int index = 0;
        for (int candidate = 1;
             candidate < config.population;
             ++candidate) {
            if (
                better(
                    population[static_cast<std::size_t>(candidate)]
                        .evaluation,
                    population[static_cast<std::size_t>(index)]
                        .evaluation
                )
            ) {
                index = candidate;
            }
        }
        return index;
    };
    const double initial_best =
        population[static_cast<std::size_t>(best_index())]
            .evaluation.aep_gwh;
    const int minimum_granularity = 10;
    const int maximum_granularity = std::max(
        minimum_granularity,
        static_cast<int>(
            std::floor(std::sqrt(static_cast<double>(config.population)))
        )
    );
    int granularity = rng.integer(
        minimum_granularity,
        maximum_granularity + 1,
        0,
        12,
        0
    );
    std::uint64_t generation = 0;

    while (physical_fes < config.physical_fes_limit) {
        ++generation;
        algorithm_started = Clock::now();
        const int global_best = best_index();
        int global_worst = 0;
        for (int candidate = 1;
             candidate < config.population;
             ++candidate) {
            if (
                better(
                    population[static_cast<std::size_t>(global_worst)]
                        .evaluation,
                    population[static_cast<std::size_t>(candidate)]
                        .evaluation
                )
            ) {
                global_worst = candidate;
            }
        }

        std::vector<double> projection(
            static_cast<std::size_t>(config.population), 0.0
        );
        executor.parallel_for(
            0,
            config.population,
            [&](const int particle_index) {
                const auto normalized = normalized_position(
                    population[static_cast<std::size_t>(particle_index)],
                    problem
                );
                double sum = 0.0;
                for (int coordinate = 0;
                     coordinate < dimension;
                     ++coordinate) {
                    const double random_vector_component =
                        2.0
                            * rng.uniform(
                                generation,
                                20,
                                0,
                                static_cast<std::uint64_t>(coordinate),
                                0
                            )
                        - 1.0;
                    sum +=
                        normalized[static_cast<std::size_t>(coordinate)]
                        * random_vector_component;
                }
                projection[static_cast<std::size_t>(particle_index)] = sum;
            }
        );
        const auto extrema = std::minmax_element(
            projection.begin(), projection.end()
        );
        const int bucket_count = std::max(
            1,
            static_cast<int>(
                std::lround(0.1 * static_cast<double>(config.population))
            )
        );
        const double bucket_width =
            (*extrema.second - *extrema.first)
            / static_cast<double>(bucket_count);
        if (bucket_width > 0.0 && generation > 1U) {
            const double shift = bucket_width
                * rng.uniform(generation, 21, 0, 0, 0);
            const auto bucket = [&](const int index) {
                return static_cast<long long>(
                    std::floor(
                        (
                            projection[static_cast<std::size_t>(index)]
                            + shift
                        ) / bucket_width
                    )
                );
            };
            const long long best_bucket = bucket(global_best);
            const long long worst_bucket = bucket(global_worst);
            int near_best = 0;
            int near_worst = 0;
            for (int index = 0; index < config.population; ++index) {
                const long long value = bucket(index);
                near_best += value == best_bucket ? 1 : 0;
                near_worst += value == worst_bucket ? 1 : 0;
            }
            const double logistic =
                std::tanh(
                    static_cast<double>(near_worst - near_best)
                );
            granularity -= static_cast<int>(std::lround(logistic));
            granularity = std::clamp(
                granularity,
                minimum_granularity,
                maximum_granularity
            );
        }

        std::vector<int> permutation(
            static_cast<std::size_t>(config.population)
        );
        std::iota(permutation.begin(), permutation.end(), 0);
        std::stable_sort(
            permutation.begin(),
            permutation.end(),
            [&](const int left, const int right) {
                const double left_key = rng.uniform(
                    generation,
                    22,
                    static_cast<std::uint64_t>(left)
                );
                const double right_key = rng.uniform(
                    generation,
                    22,
                    static_cast<std::uint64_t>(right)
                );
                if (left_key != right_key) {
                    return left_key < right_key;
                }
                return left < right;
            }
        );
        const int group_count =
            std::max(1, config.population / granularity);
        const std::uint64_t remaining =
            config.physical_fes_limit - physical_fes;
        const int active_groups = std::min(
            group_count,
            static_cast<int>(remaining)
        );
        std::vector<int> worst_indices(
            static_cast<std::size_t>(active_groups)
        );
        std::vector<int> best_indices(
            static_cast<std::size_t>(active_groups)
        );
        for (int group = 0; group < active_groups; ++group) {
            const int begin = group * granularity;
            const int end = group == group_count - 1
                ? config.population
                : std::min(config.population, begin + granularity);
            int local_best =
                permutation[static_cast<std::size_t>(begin)];
            int local_worst = local_best;
            for (int position = begin + 1;
                 position < end;
                 ++position) {
                const int candidate =
                    permutation[static_cast<std::size_t>(position)];
                if (
                    better(
                        population[static_cast<std::size_t>(candidate)]
                            .evaluation,
                        population[static_cast<std::size_t>(local_best)]
                            .evaluation
                    )
                ) {
                    local_best = candidate;
                }
                if (
                    better(
                        population[static_cast<std::size_t>(local_worst)]
                            .evaluation,
                        population[static_cast<std::size_t>(candidate)]
                            .evaluation
                    )
                ) {
                    local_worst = candidate;
                }
            }
            best_indices[static_cast<std::size_t>(group)] = local_best;
            worst_indices[static_cast<std::size_t>(group)] = local_worst;
        }

        std::vector<Particle> candidates(
            static_cast<std::size_t>(active_groups)
        );
        executor.parallel_for(0, active_groups, [&](const int group) {
            const int local_best =
                best_indices[static_cast<std::size_t>(group)];
            const int local_worst =
                worst_indices[static_cast<std::size_t>(group)];
            const Particle& source =
                population[static_cast<std::size_t>(local_worst)];
            Particle& candidate =
                candidates[static_cast<std::size_t>(group)];
            candidate.position.resize(
                static_cast<std::size_t>(dimension)
            );
            candidate.velocity.resize(
                static_cast<std::size_t>(dimension)
            );
            for (int coordinate = 0;
                 coordinate < dimension;
                 ++coordinate) {
                const auto offset =
                    static_cast<std::size_t>(coordinate);
                const double omega = rng.uniform(
                    generation,
                    23,
                    static_cast<std::uint64_t>(group),
                    static_cast<std::uint64_t>(coordinate),
                    0
                );
                const double r_one = rng.uniform(
                    generation,
                    23,
                    static_cast<std::uint64_t>(group),
                    static_cast<std::uint64_t>(coordinate),
                    1
                );
                const double r_two = rng.uniform(
                    generation,
                    23,
                    static_cast<std::uint64_t>(group),
                    static_cast<std::uint64_t>(coordinate),
                    2
                );
                candidate.velocity[offset] =
                    omega * source.velocity[offset]
                    + kCOne * r_one
                        * (
                            population[
                                static_cast<std::size_t>(local_best)
                            ].position[offset]
                            - source.position[offset]
                        )
                    + kCTwo * r_two
                        * (
                            population[
                                static_cast<std::size_t>(global_best)
                            ].position[offset]
                            - source.position[offset]
                        );
                candidate.position[offset] =
                    source.position[offset] + candidate.velocity[offset];
            }
            auto repaired = expand(
                candidate.position,
                problem.turbine_count(),
                wind_count
            );
            problem.repair(
                repaired,
                config.seed,
                generation,
                static_cast<std::uint64_t>(group)
            );
            candidate.position = flatten(repaired, wind_count);
        });
        algorithm_seconds += elapsed_seconds(algorithm_started);
        evaluate_particles(candidates);
        algorithm_started = Clock::now();
        executor.parallel_for(0, active_groups, [&](const int group) {
            population[
                static_cast<std::size_t>(
                    worst_indices[static_cast<std::size_t>(group)]
                )
            ] = std::move(candidates[static_cast<std::size_t>(group)]);
        });
        physical_fes += static_cast<std::uint64_t>(active_groups);
        algorithm_seconds += elapsed_seconds(algorithm_started);
    }

    const int final_best = best_index();
    const auto receipt = executor.work_receipt();
    RunResult result;
    result.problem_id = problem.id();
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.population = config.population;
    result.physical_fes = physical_fes;
    result.generations = generation;
    result.final_subpopulation_size = granularity;
    result.initial_best_aep_gwh = initial_best;
    result.best_aep_gwh =
        population[static_cast<std::size_t>(final_best)]
            .evaluation.aep_gwh;
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds = algorithm_seconds;
    result.end_to_end_seconds = elapsed_seconds(total_started);
    result.scientific_hash = scientific_hash(
        population[static_cast<std::size_t>(final_best)],
        physical_fes,
        generation,
        granularity
    );
    result.best_layout = expand(
        population[static_cast<std::size_t>(final_best)].position,
        problem.turbine_count(),
        wind_count
    );
    return result;
}

}  // namespace core99::t85
