/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T82 MTG/turbulence evaluator, NSGA-II, and CPU-HPC path
Paper/DOI: Wind Farm Layout Optimization to Minimize the Wake-Induced
Turbulence Effect on Wind Turbines; 10.1016/j.apenergy.2022.119599
Public source, missing assets, paper conflict, reconstruction decisions,
semantic IDs, and claim boundary:
hpc/core99_cpp/include/core99/cao_t82.hpp
HPC design: wind states and equal-area rotor samples are immutable; every
population evaluation, offspring construction, and dominance row is
partitioned over one persistent worker team; each layout remains internally
serial to avoid nested oversubscription; reductions and counter-keyed random
events retain bitwise one/all-core trajectories
Controlling contract: shared/contracts/core99_t82_cao_2022.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/cao_t82.hpp"

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

namespace core99::t82 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kIdealSideM = 2000.0;
constexpr double kZhuangheXMinM = 806.0;
constexpr double kZhuangheXMaxM = 8360.0;
constexpr double kZhuangheYMinM = 650.0;
constexpr double kZhuangheNotchXMaxM = 4829.0;
constexpr double kZhuangheLowYMaxM = 3014.0;
constexpr double kZhuangheUpperYMinM = 5672.0;
constexpr double kSqrtTwoLogTwo = 1.1774100225154747;
constexpr double kCrossoverProbability = 0.9;
constexpr double kSbxIndex = 20.0;
constexpr double kMutationIndex = 20.0;
constexpr int kRotorSamples = 8;

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double squared_distance(const Turbine& left, const Turbine& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
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

double mtg_deficit_fraction(
    double downstream_m,
    double radial_m,
    double rotor_radius_m,
    double thrust,
    double wake_expansion
) {
    if (!(downstream_m > 0.0)) {
        return 0.0;
    }
    const double expansion =
        wake_expansion * downstream_m / rotor_radius_m + 1.0;
    const double radical = std::clamp(
        1.0 - 2.0 * thrust / (expansion * expansion),
        0.0,
        1.0
    );
    return (1.0 - std::sqrt(radical))
        * std::exp(
            -2.0 * radial_m * radial_m
            / (expansion * expansion * rotor_radius_m * rotor_radius_m)
        );
}

double added_turbulence(
    double downstream_m,
    double crosswind_m,
    double vertical_m,
    double diameter_m,
    double thrust,
    double ambient_turbulence,
    double upstream_inflow_turbulence
) {
    if (!(downstream_m > 0.0)) {
        return 0.0;
    }
    const double root = std::sqrt(std::max(1.0e-12, 1.0 - thrust));
    const double epsilon = 0.2 * diameter_m
        * std::sqrt(0.5 * (1.0 - root) / root);
    const double wake_growth =
        0.3837 * upstream_inflow_turbulence + 0.003678;
    const double sigma_y = wake_growth * downstream_m + epsilon;
    const double half_width = kSqrtTwoLogTwo * sigma_y;
    const double sigma_t = sigma_y / kSqrtTwoLogTwo;
    const double normalized_distance = downstream_m / diameter_m;
    const double denominator =
        2.3 * std::pow(thrust, -1.2)
        + std::pow(ambient_turbulence, 0.1) * normalized_distance
        + 0.7 * std::pow(thrust, -3.2)
            * std::pow(ambient_turbulence, -0.45)
            * std::pow(1.0 + normalized_distance, -2.0);
    const double maximum = 1.0 / denominator;
    const double radius = std::hypot(crosswind_m, vertical_m);
    double shape = 0.0;
    if (radius < half_width) {
        shape = 1.0
            - 0.15
                * (
                    1.0
                    + std::cos(std::numbers::pi * radius / half_width)
                );
    } else {
        const double offset = radius - half_width;
        shape = std::exp(
            -offset * offset / (2.0 * sigma_t * sigma_t)
        );
    }
    const double k1 = radius < half_width
        ? std::sin(
            0.5 * std::numbers::pi * radius / half_width
        )
        : 1.0;
    const double gaussian = std::exp(
        -(radius - half_width) * (radius - half_width)
        / (2.0 * sigma_t * sigma_t)
    );
    const double sine_alpha =
        radius > 0.0 ? vertical_m / radius : 0.0;
    const double ground_factor =
        vertical_m >= 0.0 ? 0.23 : -1.23;
    const double ground = ambient_turbulence
        * ground_factor * sine_alpha * k1 * gaussian;
    return std::max(0.0, maximum * shape + ground);
}

double weibull_cdf(double speed, double scale) {
    if (!(speed > 0.0)) {
        return 0.0;
    }
    return 1.0 - std::exp(-std::pow(speed / scale, 2.0));
}

double zhuanghe_ambient_turbulence(double speed) {
    return std::clamp(
        0.07
            + 0.88 * std::exp(-speed / 2.0)
            + 0.018
                * std::exp(
                    -0.5 * std::pow((speed - 22.5) / 1.2, 2.0)
                ),
        0.06,
        0.95
    );
}

bool better_constraint_state(
    const Evaluation& left,
    const Evaluation& right
) {
    if (left.feasible != right.feasible) {
        return left.feasible;
    }
    const double left_violation =
        left.spacing_violation_m + left.boundary_violation_m;
    const double right_violation =
        right.spacing_violation_m + right.boundary_violation_m;
    return left_violation < right_violation;
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
        left.expected_power_kw >= right.expected_power_kw
        && left.maximum_comprehensive_turbulence
            <= right.maximum_comprehensive_turbulence;
    const bool strict =
        left.expected_power_kw > right.expected_power_kw
        || left.maximum_comprehensive_turbulence
            < right.maximum_comprehensive_turbulence;
    return no_worse && strict;
}

struct Individual {
    std::vector<Turbine> layout;
    Evaluation evaluation;
    int rank = 0;
    double crowding = 0.0;
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
        auto& row = outgoing[static_cast<std::size_t>(left)];
        int degree = 0;
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

void assign_crowding(
    std::vector<Individual>& population,
    const std::vector<int>& front
) {
    for (const int index : front) {
        population[static_cast<std::size_t>(index)].crowding = 0.0;
    }
    if (front.empty()) {
        return;
    }
    if (front.size() <= 2U) {
        for (const int index : front) {
            population[static_cast<std::size_t>(index)].crowding =
                std::numeric_limits<double>::infinity();
        }
        return;
    }
    for (int objective = 0; objective < 2; ++objective) {
        std::vector<int> order = front;
        auto value = [&](const int index) {
            const auto& item =
                population[static_cast<std::size_t>(index)].evaluation;
            return objective == 0
                ? -item.expected_power_kw
                : item.maximum_comprehensive_turbulence;
        };
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int left, const int right) {
                if (value(left) != value(right)) {
                    return value(left) < value(right);
                }
                return left < right;
            }
        );
        population[static_cast<std::size_t>(order.front())].crowding =
            std::numeric_limits<double>::infinity();
        population[static_cast<std::size_t>(order.back())].crowding =
            std::numeric_limits<double>::infinity();
        const double range = value(order.back()) - value(order.front());
        if (!(range > 0.0)) {
            continue;
        }
        for (std::size_t position = 1; position + 1 < order.size();
             ++position) {
            auto& item =
                population[static_cast<std::size_t>(order[position])];
            if (std::isfinite(item.crowding)) {
                item.crowding +=
                    (value(order[position + 1])
                     - value(order[position - 1]))
                    / range;
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

bool tournament_better(
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
    if (better_constraint_state(left.evaluation, right.evaluation)) {
        return true;
    }
    if (better_constraint_state(right.evaluation, left.evaluation)) {
        return false;
    }
    return left_index < right_index;
}

int tournament(
    const std::vector<Individual>& population,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t individual,
    std::uint64_t draw
) {
    const int count = static_cast<int>(population.size());
    const int left = rng.integer(
        0, count, generation, 20, individual, draw, 0
    );
    const int right = rng.integer(
        0, count, generation, 20, individual, draw, 1
    );
    return tournament_better(
        population[static_cast<std::size_t>(left)],
        population[static_cast<std::size_t>(right)],
        left,
        right
    ) ? left : right;
}

double sbx_value(double left, double right, double random) {
    const double beta = random <= 0.5
        ? std::pow(2.0 * random, 1.0 / (kSbxIndex + 1.0))
        : std::pow(
            1.0 / (2.0 * (1.0 - random)),
            1.0 / (kSbxIndex + 1.0)
        );
    return 0.5 * ((1.0 + beta) * left + (1.0 - beta) * right);
}

double polynomial_delta(double random) {
    return random < 0.5
        ? std::pow(2.0 * random, 1.0 / (kMutationIndex + 1.0)) - 1.0
        : 1.0
            - std::pow(
                2.0 * (1.0 - random),
                1.0 / (kMutationIndex + 1.0)
            );
}

bool candidate_separated(
    const Problem& problem,
    const std::vector<Turbine>& layout,
    std::size_t current
) {
    for (std::size_t other = 0; other < current; ++other) {
        const double required =
            problem.case_id() == CaseId::zhuanghe
            ? 4.0
                * std::max(
                    problem.diameter(layout[current].type),
                    problem.diameter(layout[other].type)
                )
            : 5.0 * problem.diameter(0);
        if (
            squared_distance(layout[current], layout[other])
            <= required * required
        ) {
            return false;
        }
    }
    return true;
}

void repair_layout(
    const Problem& problem,
    std::vector<Turbine>& layout,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t individual
) {
    const std::vector<Turbine> fallback = problem.reference_layout();
    for (std::size_t turbine = 0; turbine < layout.size(); ++turbine) {
        if (problem.case_id() != CaseId::zhuanghe) {
            layout[turbine].x_m =
                std::clamp(layout[turbine].x_m, 0.0, kIdealSideM);
            layout[turbine].y_m =
                std::clamp(layout[turbine].y_m, 0.0, kIdealSideM);
        }
        if (
            problem.inside(layout[turbine].x_m, layout[turbine].y_m)
            && candidate_separated(problem, layout, turbine)
        ) {
            continue;
        }
        bool placed = false;
        for (std::uint64_t attempt = 0; attempt < 512U; ++attempt) {
            const double x_span = problem.case_id() == CaseId::zhuanghe
                ? kZhuangheXMaxM - kZhuangheXMinM
                : kIdealSideM;
            const double y_span = problem.case_id() == CaseId::zhuanghe
                ? 9300.0
                : kIdealSideM;
            layout[turbine].x_m =
                (problem.case_id() == CaseId::zhuanghe
                    ? kZhuangheXMinM : 0.0)
                + x_span
                    * rng.uniform(
                        generation,
                        30,
                        individual,
                        turbine,
                        2U * attempt
                    );
            layout[turbine].y_m =
                (problem.case_id() == CaseId::zhuanghe
                    ? kZhuangheYMinM : 0.0)
                + y_span
                    * rng.uniform(
                        generation,
                        30,
                        individual,
                        turbine,
                        2U * attempt + 1U
                    );
            if (
                problem.inside(layout[turbine].x_m, layout[turbine].y_m)
                && candidate_separated(problem, layout, turbine)
            ) {
                placed = true;
                break;
            }
        }
        if (!placed) {
            layout = fallback;
            return;
        }
    }
}

std::uint64_t hash_mix(std::uint64_t state, std::uint64_t value) {
    state ^= value + 0x9e3779b97f4a7c15ULL
        + (state << 6U) + (state >> 2U);
    return state;
}

std::uint64_t scientific_hash(
    const std::vector<Individual>& population
) {
    std::uint64_t state = 0x82ca02022119599ULL;
    for (const auto& item : population) {
        state = hash_mix(
            state,
            std::bit_cast<std::uint64_t>(
                item.evaluation.expected_power_kw
            )
        );
        state = hash_mix(
            state,
            std::bit_cast<std::uint64_t>(
                item.evaluation.maximum_comprehensive_turbulence
            )
        );
        for (const auto& turbine : item.layout) {
            state = hash_mix(
                state, std::bit_cast<std::uint64_t>(turbine.x_m)
            );
            state = hash_mix(
                state, std::bit_cast<std::uint64_t>(turbine.y_m)
            );
            state = hash_mix(
                state, static_cast<std::uint64_t>(turbine.type)
            );
        }
    }
    return state;
}

}  // namespace

Problem::Problem(const CaseId id) : case_id_(id) {
    if (id == CaseId::ideal_single) {
        id_ = "t82_ideal_case_i_n30";
        turbine_count_ = 30;
        turbine_types_.push_back({40.0, 60.0, 0.88, 0.0, 12.0});
    } else if (id == CaseId::ideal_multi) {
        id_ = "t82_ideal_case_ii_n39";
        turbine_count_ = 39;
        turbine_types_.push_back({40.0, 60.0, 0.88, 0.0, 12.0});
    } else {
        id_ = "t82_zhuanghe_n72";
        turbine_count_ = 72;
        turbine_types_ = {
            {121.0, 100.0, 0.8, 3000.0, 12.0},
            {140.0, 100.0, 0.8, 3300.0, 12.0},
            {171.0, 100.0, 0.8, 6450.0, 12.0},
        };
    }
    build_wind_states();
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
    return static_cast<int>(wind_states_.size());
}

int Problem::paper_population() const noexcept {
    return 100;
}

int Problem::paper_generations() const noexcept {
    return 20;
}

int Problem::paper_repeats() const noexcept {
    return 25;
}

double Problem::boundary_violation(
    const double x_m,
    const double y_m
) const noexcept {
    if (case_id_ != CaseId::zhuanghe) {
        const double dx =
            x_m < 0.0 ? -x_m : std::max(0.0, x_m - kIdealSideM);
        const double dy =
            y_m < 0.0 ? -y_m : std::max(0.0, y_m - kIdealSideM);
        return std::hypot(dx, dy);
    }
    if (inside(x_m, y_m)) {
        return 0.0;
    }
    const double clamped_x =
        std::clamp(x_m, kZhuangheXMinM, kZhuangheXMaxM);
    const double upper = 9792.036 - 0.206 * clamped_x;
    double clamped_y = std::clamp(y_m, kZhuangheYMinM, upper);
    if (
        clamped_x <= kZhuangheNotchXMaxM
        && clamped_y > kZhuangheLowYMaxM
        && clamped_y < kZhuangheUpperYMinM
    ) {
        clamped_y =
            clamped_y - kZhuangheLowYMaxM
                < kZhuangheUpperYMinM - clamped_y
            ? kZhuangheLowYMaxM : kZhuangheUpperYMinM;
    }
    return std::hypot(x_m - clamped_x, y_m - clamped_y);
}

bool Problem::inside(const double x_m, const double y_m) const noexcept {
    if (case_id_ != CaseId::zhuanghe) {
        return x_m >= 0.0 && x_m <= kIdealSideM
            && y_m >= 0.0 && y_m <= kIdealSideM;
    }
    if (
        x_m < kZhuangheXMinM || x_m > kZhuangheXMaxM
        || y_m < kZhuangheYMinM
        || y_m > 9792.036 - 0.206 * x_m
    ) {
        return false;
    }
    if (x_m <= kZhuangheNotchXMaxM) {
        return y_m <= kZhuangheLowYMaxM
            || y_m >= kZhuangheUpperYMinM;
    }
    return true;
}

double Problem::diameter(const int type) const noexcept {
    return turbine_types_[static_cast<std::size_t>(type)].diameter_m;
}

double Problem::power_kw(const int type, const double speed_mps) const {
    if (case_id_ != CaseId::zhuanghe) {
        return speed_mps > 0.0
            ? 0.3 * speed_mps * speed_mps * speed_mps
            : 0.0;
    }
    const auto& turbine = turbine_types_[static_cast<std::size_t>(type)];
    if (speed_mps < 3.0 || speed_mps > 25.0) {
        return 0.0;
    }
    if (speed_mps >= turbine.rated_speed_mps) {
        return turbine.rated_power_kw;
    }
    const double fraction =
        (speed_mps - 3.0) / (turbine.rated_speed_mps - 3.0);
    return turbine.rated_power_kw
        * fraction * fraction * fraction;
}

void Problem::build_wind_states() {
    if (case_id_ == CaseId::ideal_single) {
        wind_states_.push_back({0.0, 12.0, 0.1, 1.0});
        return;
    }
    if (case_id_ == CaseId::ideal_multi) {
        constexpr std::array<double, 36> total = {
            0.024, 0.024, 0.024, 0.024, 0.024, 0.024,
            0.024, 0.024, 0.024, 0.024, 0.024, 0.024,
            0.024, 0.024, 0.024, 0.024, 0.024, 0.024,
            0.024, 0.024, 0.024, 0.024, 0.024, 0.024,
            0.024, 0.024, 0.024, 0.026, 0.033, 0.036,
            0.048, 0.059, 0.047, 0.036, 0.032, 0.025,
        };
        constexpr std::array<double, 36> middle = {
            0.008, 0.008, 0.008, 0.008, 0.008, 0.008,
            0.008, 0.008, 0.008, 0.008, 0.008, 0.008,
            0.008, 0.008, 0.008, 0.008, 0.008, 0.008,
            0.008, 0.008, 0.008, 0.008, 0.008, 0.008,
            0.008, 0.008, 0.008, 0.010, 0.012, 0.015,
            0.014, 0.020, 0.014, 0.015, 0.012, 0.010,
        };
        double sum = std::accumulate(total.begin(), total.end(), 0.0);
        for (int direction = 0; direction < 36; ++direction) {
            const double green = 0.004;
            const double red = middle[static_cast<std::size_t>(direction)];
            const double yellow =
                total[static_cast<std::size_t>(direction)] - green - red;
            const double angle = 10.0 * static_cast<double>(direction);
            wind_states_.push_back({angle, 8.0, 0.1, green / sum});
            wind_states_.push_back({angle, 12.0, 0.1, red / sum});
            wind_states_.push_back({angle, 17.0, 0.1, yellow / sum});
        }
        return;
    }
    constexpr std::array<double, 16> direction_weights = {
        0.140, 0.060, 0.030, 0.025,
        0.025, 0.030, 0.040, 0.055,
        0.060, 0.060, 0.045, 0.025,
        0.020, 0.040, 0.075, 0.120,
    };
    constexpr std::array<double, 7> speed_edges = {
        0.0, 3.0, 5.0, 7.0, 9.0, 12.0, 25.0,
    };
    constexpr std::array<double, 6> speed_representatives = {
        1.8, 4.1, 6.0, 8.0, 10.4, 14.5,
    };
    const double direction_sum = std::accumulate(
        direction_weights.begin(), direction_weights.end(), 0.0
    );
    const double scale = 6.9 / std::tgamma(1.5);
    const double truncation = weibull_cdf(25.0, scale);
    for (int direction = 0; direction < 16; ++direction) {
        const double direction_probability =
            direction_weights[static_cast<std::size_t>(direction)]
            / direction_sum;
        for (int speed_bin = 0; speed_bin < 6; ++speed_bin) {
            const double speed_probability = (
                weibull_cdf(
                    speed_edges[static_cast<std::size_t>(speed_bin + 1)],
                    scale
                )
                - weibull_cdf(
                    speed_edges[static_cast<std::size_t>(speed_bin)],
                    scale
                )
            ) / truncation;
            const double speed =
                speed_representatives[static_cast<std::size_t>(speed_bin)];
            wind_states_.push_back({
                22.5 * static_cast<double>(direction),
                speed,
                zhuanghe_ambient_turbulence(speed),
                direction_probability * speed_probability,
            });
        }
    }
}

Evaluation Problem::evaluate(
    const std::vector<Turbine>& layout
) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T82 layout cardinality mismatch");
    }
    Evaluation result;
    for (std::size_t index = 0; index < layout.size(); ++index) {
        result.boundary_violation_m +=
            boundary_violation(layout[index].x_m, layout[index].y_m);
        const int type = layout[index].type;
        if (
            type < 0
            || type >= static_cast<int>(turbine_types_.size())
        ) {
            throw std::invalid_argument("T82 turbine type out of range");
        }
        for (std::size_t other = 0; other < index; ++other) {
            const double required = case_id_ == CaseId::zhuanghe
                ? 4.0
                    * std::max(
                        diameter(layout[index].type),
                        diameter(layout[other].type)
                    )
                : 5.0 * diameter(0);
            result.spacing_violation_m += std::max(
                0.0,
                required
                    - std::sqrt(squared_distance(layout[index], layout[other]))
            );
        }
    }
    result.feasible =
        result.boundary_violation_m <= 1.0e-9
        && result.spacing_violation_m <= 1.0e-9;

    std::vector<double> comprehensive(
        layout.size(), 0.0
    );
    std::vector<double> along(layout.size());
    std::vector<double> across(layout.size());
    std::vector<int> order(layout.size());
    std::iota(order.begin(), order.end(), 0);
    std::vector<double> inflow_turbulence(layout.size());
    for (const WindState& state : wind_states_) {
        const double angle =
            (270.0 - state.direction_deg)
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        for (std::size_t index = 0; index < layout.size(); ++index) {
            along[index] =
                cosine * layout[index].x_m + sine * layout[index].y_m;
            across[index] =
                -sine * layout[index].x_m + cosine * layout[index].y_m;
            inflow_turbulence[index] = state.ambient_turbulence;
        }
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int left, const int right) {
                if (
                    along[static_cast<std::size_t>(left)]
                    != along[static_cast<std::size_t>(right)]
                ) {
                    return along[static_cast<std::size_t>(left)]
                        < along[static_cast<std::size_t>(right)];
                }
                return left < right;
            }
        );
        double state_power = 0.0;
        for (std::size_t downstream_position = 0;
             downstream_position < order.size();
             ++downstream_position) {
            const int downstream =
                order[downstream_position];
            const std::size_t downstream_index =
                static_cast<std::size_t>(downstream);
            const auto& downstream_type = turbine_types_[
                static_cast<std::size_t>(layout[downstream_index].type)
            ];
            std::array<double, kRotorSamples> maximum_added{};
            double squared_velocity_deficit = 0.0;
            for (std::size_t upstream_position = 0;
                 upstream_position < downstream_position;
                 ++upstream_position) {
                const int upstream = order[upstream_position];
                const std::size_t upstream_index =
                    static_cast<std::size_t>(upstream);
                const double distance =
                    along[downstream_index] - along[upstream_index];
                const double cross =
                    across[downstream_index] - across[upstream_index];
                const auto& upstream_type = turbine_types_[
                    static_cast<std::size_t>(layout[upstream_index].type)
                ];
                const double wake_expansion = 2.0
                    * (
                        0.3837 * inflow_turbulence[upstream_index]
                        + 0.003678
                    );
                double mean_squared_pair_deficit = 0.0;
                for (int sample = 0; sample < kRotorSamples; ++sample) {
                    const auto unit =
                        kRotorUnitSamples[static_cast<std::size_t>(sample)];
                    const double sample_cross =
                        cross
                        + unit.first
                            * 0.5 * downstream_type.diameter_m;
                    const double sample_vertical =
                        downstream_type.hub_height_m
                        - upstream_type.hub_height_m
                        + unit.second
                            * 0.5 * downstream_type.diameter_m;
                    const double radial =
                        std::hypot(sample_cross, sample_vertical);
                    const double deficit = mtg_deficit_fraction(
                        distance,
                        radial,
                        0.5 * upstream_type.diameter_m,
                        upstream_type.thrust,
                        wake_expansion
                    );
                    mean_squared_pair_deficit +=
                        deficit * deficit
                        / static_cast<double>(kRotorSamples);
                    maximum_added[static_cast<std::size_t>(sample)] =
                        std::max(
                            maximum_added[
                                static_cast<std::size_t>(sample)
                            ],
                            added_turbulence(
                                distance,
                                sample_cross,
                                sample_vertical,
                                upstream_type.diameter_m,
                                upstream_type.thrust,
                                state.ambient_turbulence,
                                inflow_turbulence[upstream_index]
                            )
                        );
                }
                squared_velocity_deficit += mean_squared_pair_deficit;
            }
            double mean_turbulence = 0.0;
            for (const double added : maximum_added) {
                mean_turbulence += std::hypot(
                    added, state.ambient_turbulence
                ) / static_cast<double>(kRotorSamples);
            }
            inflow_turbulence[downstream_index] = mean_turbulence;
            comprehensive[downstream_index] +=
                state.probability * mean_turbulence;
            const double inflow_speed = state.speed_mps
                * (
                    1.0
                    - std::min(
                        0.999,
                        std::sqrt(squared_velocity_deficit)
                    )
                );
            state_power += power_kw(
                layout[downstream_index].type, inflow_speed
            );
        }
        result.expected_power_kw += state.probability * state_power;
    }
    result.maximum_comprehensive_turbulence =
        *std::max_element(comprehensive.begin(), comprehensive.end());
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
    result.reserve(static_cast<std::size_t>(turbine_count_));
    if (case_id_ != CaseId::zhuanghe) {
        for (int row = 0; row < 10; ++row) {
            for (int column = 0; column < 10; ++column) {
                result.push_back({
                    50.0 + 210.0 * static_cast<double>(column),
                    50.0 + 210.0 * static_cast<double>(row),
                    0,
                });
                if (
                    static_cast<int>(result.size()) == turbine_count_
                ) {
                    return result;
                }
            }
        }
    } else {
        for (int row = 0; row < 12; ++row) {
            for (int column = 0; column < 10; ++column) {
                Turbine candidate{
                    850.0 + 800.0 * static_cast<double>(column),
                    700.0 + 800.0 * static_cast<double>(row),
                    1,
                };
                if (!inside(candidate.x_m, candidate.y_m)) {
                    continue;
                }
                if (result.size() < 2U) {
                    candidate.type = 0;
                } else if (result.size() >= 51U) {
                    candidate.type = 2;
                }
                result.push_back(candidate);
                if (result.size() == 72U) {
                    return result;
                }
            }
        }
    }
    throw std::runtime_error("T82 reference layout construction failed");
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers <= 0
        || config.population < 4
        || config.generations < 0
    ) {
        throw std::invalid_argument("invalid T82 run configuration");
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
            const double scale =
                problem.case_id() == CaseId::zhuanghe ? 180.0 : 55.0;
            for (std::size_t turbine = 0; turbine < layout.size();
                 ++turbine) {
                layout[turbine].x_m += scale
                    * rng.normal(0, 1, static_cast<std::uint64_t>(index),
                                 turbine, 0);
                layout[turbine].y_m += scale
                    * rng.normal(0, 1, static_cast<std::uint64_t>(index),
                                 turbine, 1);
            }
            repair_layout(
                problem,
                layout,
                rng,
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
    rank_and_crowding(population, executor);
    double algorithm_seconds = elapsed_seconds(total_started)
        - evaluator_seconds;
    for (int generation = 0; generation < config.generations;
         ++generation) {
        const auto algorithm_started = Clock::now();
        std::vector<Individual> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](const int index) {
            const auto generation_key =
                static_cast<std::uint64_t>(generation + 1);
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
            if (
                rng.uniform(
                    generation_key, 21, individual_key, 0, 0
                ) < kCrossoverProbability
            ) {
                for (std::size_t turbine = 0; turbine < child.size();
                     ++turbine) {
                    child[turbine].x_m = sbx_value(
                        child[turbine].x_m,
                        mate[turbine].x_m,
                        rng.uniform(
                            generation_key,
                            22,
                            individual_key,
                            turbine,
                            0
                        )
                    );
                    child[turbine].y_m = sbx_value(
                        child[turbine].y_m,
                        mate[turbine].y_m,
                        rng.uniform(
                            generation_key,
                            22,
                            individual_key,
                            turbine,
                            1
                        )
                    );
                }
            }
            const double mutation_probability =
                1.0 / (2.0 * static_cast<double>(child.size()));
            const double x_range =
                problem.case_id() == CaseId::zhuanghe
                ? kZhuangheXMaxM - kZhuangheXMinM : kIdealSideM;
            const double y_range =
                problem.case_id() == CaseId::zhuanghe
                ? 9300.0 : kIdealSideM;
            for (std::size_t turbine = 0; turbine < child.size();
                 ++turbine) {
                if (
                    rng.uniform(
                        generation_key,
                        23,
                        individual_key,
                        turbine,
                        0
                    ) < mutation_probability
                ) {
                    child[turbine].x_m += x_range
                        * polynomial_delta(rng.uniform(
                            generation_key,
                            23,
                            individual_key,
                            turbine,
                            1
                        ));
                }
                if (
                    rng.uniform(
                        generation_key,
                        23,
                        individual_key,
                        turbine,
                        2
                    ) < mutation_probability
                ) {
                    child[turbine].y_m += y_range
                        * polynomial_delta(rng.uniform(
                            generation_key,
                            23,
                            individual_key,
                            turbine,
                            3
                        ));
                }
            }
            repair_layout(
                problem,
                child,
                rng,
                generation_key,
                individual_key
            );
            offspring[static_cast<std::size_t>(index)].layout =
                std::move(child);
        });
        algorithm_seconds += elapsed_seconds(algorithm_started);
        evaluate_individuals(offspring);
        const auto selection_started = Clock::now();
        std::vector<Individual> merged;
        merged.reserve(population.size() + offspring.size());
        for (auto& item : population) {
            merged.push_back(std::move(item));
        }
        for (auto& item : offspring) {
            merged.push_back(std::move(item));
        }
        const auto fronts = assign_rank(merged, executor);
        for (const auto& front : fronts) {
            assign_crowding(merged, front);
        }
        population.clear();
        population.reserve(static_cast<std::size_t>(config.population));
        for (const auto& front : fronts) {
            if (
                population.size() + front.size()
                <= static_cast<std::size_t>(config.population)
            ) {
                for (const int index : front) {
                    population.push_back(
                        std::move(merged[static_cast<std::size_t>(index)])
                    );
                }
                continue;
            }
            std::vector<int> order = front;
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](const int left, const int right) {
                    const auto& a =
                        merged[static_cast<std::size_t>(left)];
                    const auto& b =
                        merged[static_cast<std::size_t>(right)];
                    if (a.crowding != b.crowding) {
                        return a.crowding > b.crowding;
                    }
                    return left < right;
                }
            );
            for (const int index : order) {
                if (
                    population.size()
                    == static_cast<std::size_t>(config.population)
                ) {
                    break;
                }
                population.push_back(
                    std::move(merged[static_cast<std::size_t>(index)])
                );
            }
            break;
        }
        rank_and_crowding(population, executor);
        algorithm_seconds += elapsed_seconds(selection_started);
    }
    std::stable_sort(
        population.begin(),
        population.end(),
        [](const Individual& left, const Individual& right) {
            if (left.rank != right.rank) {
                return left.rank < right.rank;
            }
            if (
                left.evaluation.maximum_comprehensive_turbulence
                != right.evaluation.maximum_comprehensive_turbulence
            ) {
                return
                    left.evaluation.maximum_comprehensive_turbulence
                    < right.evaluation.maximum_comprehensive_turbulence;
            }
            return left.evaluation.expected_power_kw
                > right.evaluation.expected_power_kw;
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
            item.evaluation.expected_power_kw,
            item.evaluation.maximum_comprehensive_turbulence,
            item.layout,
        });
    }
    return result;
}

}  // namespace core99::t82
