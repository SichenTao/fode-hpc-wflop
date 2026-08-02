/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared discrete-grid Jensen-overlap wake kernel
Paper title: not_applicable_shared_infrastructure
Public asset: project-native clean-room implementation.
Missing/completion: no embedded paper defaults; consumers provide contracts.
Reconstruction: not applicable shared infrastructure.
Semantic IDs: not_applicable_shared_infrastructure.
Contract: consuming paper contracts.
Claim boundary: reusable evaluator kernel only.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/discrete_grid_wake.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace core99::gridwake {
namespace {

double overlap_area(
    const double separation,
    const double first_radius,
    const double second_radius
) {
    if (separation >= first_radius + second_radius) return 0.0;
    if (separation <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double distance = std::max(separation, 1.0e-12);
    const double first_angle = std::acos(std::clamp(
        (distance * distance + first_radius * first_radius
            - second_radius * second_radius)
            / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
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
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radicand);
}

double power_kw(const Turbine& turbine, const double speed_mps) {
    if (speed_mps < turbine.cut_in_mps || speed_mps >= turbine.cut_out_mps) {
        return 0.0;
    }
    if (speed_mps >= turbine.rated_mps) return turbine.rated_power_kw;
    const double denominator =
        std::pow(turbine.rated_mps, 3.0)
        - std::pow(turbine.cut_in_mps, 3.0);
    const double fraction =
        (std::pow(speed_mps, 3.0) - std::pow(turbine.cut_in_mps, 3.0))
        / denominator;
    return turbine.rated_power_kw * std::clamp(fraction, 0.0, 1.0);
}

}  // namespace

Problem::Problem(Configuration configuration)
    : configuration_(std::move(configuration)) {
    if (
        configuration_.rows < 1
        || configuration_.columns < 1
        || configuration_.turbine_count < 1
        || configuration_.turbine_count
            > configuration_.rows * configuration_.columns
        || configuration_.cell_width_m <= 0.0
        || configuration_.turbine.rotor_diameter_m <= 0.0
        || configuration_.turbine.wake_expansion <= 0.0
        || configuration_.turbine.thrust_coefficient <= 0.0
        || configuration_.turbine.thrust_coefficient >= 1.0
        || configuration_.wind_states.empty()
    ) {
        throw std::invalid_argument("invalid shared grid-wake configuration");
    }
    const double probability = std::accumulate(
        configuration_.wind_states.begin(),
        configuration_.wind_states.end(),
        0.0,
        [](const double sum, const WindState& state) {
            return sum + state.probability;
        }
    );
    if (std::abs(probability - 1.0) > 1.0e-8) {
        throw std::invalid_argument("grid-wake probabilities do not sum to one");
    }
}

const Configuration& Problem::configuration() const noexcept {
    return configuration_;
}

int Problem::candidate_count() const noexcept {
    return configuration_.rows * configuration_.columns;
}

bool Problem::feasible(const Layout& layout) const noexcept {
    return static_cast<int>(layout.size()) == configuration_.turbine_count
        && std::is_sorted(layout.begin(), layout.end())
        && !layout.empty()
        && layout.front() >= 0
        && layout.back() < candidate_count()
        && std::adjacent_find(layout.begin(), layout.end()) == layout.end();
}

Evaluation Problem::evaluate(const Layout& layout) const {
    Evaluation result;
    result.turbine_power_kw.assign(
        static_cast<std::size_t>(configuration_.turbine_count),
        0.0
    );
    if (!feasible(layout)) return result;
    const auto& turbine = configuration_.turbine;
    const double rotor_radius = 0.5 * turbine.rotor_diameter_m;
    for (const auto& wind : configuration_.wind_states) {
        result.ideal_power_kw +=
            wind.probability * configuration_.turbine_count
            * power_kw(turbine, wind.speed_mps);
        const double radians =
            wind.from_degrees * std::numbers::pi / 180.0;
        std::vector<double> rotated_x(
            static_cast<std::size_t>(configuration_.turbine_count)
        );
        std::vector<double> rotated_y(
            static_cast<std::size_t>(configuration_.turbine_count)
        );
        std::vector<int> order(
            static_cast<std::size_t>(configuration_.turbine_count)
        );
        for (int index = 0; index < configuration_.turbine_count; ++index) {
            const int node = layout[static_cast<std::size_t>(index)];
            const double x =
                (static_cast<double>(node % configuration_.columns) + 0.5)
                * configuration_.cell_width_m;
            const double y =
                (static_cast<double>(node / configuration_.columns) + 0.5)
                * configuration_.cell_width_m;
            rotated_x[static_cast<std::size_t>(index)] =
                std::cos(radians) * x - std::sin(radians) * y;
            rotated_y[static_cast<std::size_t>(index)] =
                std::sin(radians) * x + std::cos(radians) * y;
            order[static_cast<std::size_t>(index)] = index;
        }
        std::sort(
            order.begin(),
            order.end(),
            [&](const int first, const int second) {
                return rotated_y[static_cast<std::size_t>(first)]
                    > rotated_y[static_cast<std::size_t>(second)];
            }
        );
        std::vector<double> squared_deficit(
            static_cast<std::size_t>(configuration_.turbine_count),
            0.0
        );
        for (int downstream_position = 0;
             downstream_position < configuration_.turbine_count;
             ++downstream_position) {
            const int downstream =
                order[static_cast<std::size_t>(downstream_position)];
            for (int upstream_position = 0;
                 upstream_position < downstream_position;
                 ++upstream_position) {
                const int upstream =
                    order[static_cast<std::size_t>(upstream_position)];
                const double streamwise =
                    rotated_y[static_cast<std::size_t>(upstream)]
                    - rotated_y[static_cast<std::size_t>(downstream)];
                if (streamwise <= 0.0) continue;
                const double crosswise = std::abs(
                    rotated_x[static_cast<std::size_t>(upstream)]
                    - rotated_x[static_cast<std::size_t>(downstream)]
                );
                const double wake_radius =
                    rotor_radius + turbine.wake_expansion * streamwise;
                const double overlap = overlap_area(
                    crosswise,
                    wake_radius,
                    rotor_radius
                );
                const double overlap_fraction =
                    overlap / (std::numbers::pi * rotor_radius * rotor_radius);
                const double single_deficit =
                    (1.0 - std::sqrt(1.0 - turbine.thrust_coefficient))
                    * std::pow(
                        turbine.rotor_diameter_m
                        / (
                            turbine.rotor_diameter_m
                            + 2.0 * turbine.wake_expansion * streamwise
                        ),
                        2.0
                    )
                    * overlap_fraction;
                squared_deficit[static_cast<std::size_t>(downstream)]
                    += single_deficit * single_deficit;
            }
            const double speed = wind.speed_mps * (
                1.0 - std::sqrt(
                    squared_deficit[static_cast<std::size_t>(downstream)]
                )
            );
            result.turbine_power_kw[static_cast<std::size_t>(downstream)]
                += wind.probability * power_kw(turbine, speed);
        }
    }
    result.expected_power_kw = std::accumulate(
        result.turbine_power_kw.begin(),
        result.turbine_power_kw.end(),
        0.0
    );
    result.conversion_efficiency_percent =
        100.0 * result.expected_power_kw / result.ideal_power_kw;
    result.cost_of_energy =
        turbine_cost(configuration_.turbine_count)
        / result.expected_power_kw;
    result.feasible =
        std::isfinite(result.expected_power_kw)
        && result.expected_power_kw > 0.0;
    return result;
}

double turbine_cost(const int turbine_count) {
    return static_cast<double>(turbine_count) * (
        2.0 / 3.0
        + (1.0 / 3.0)
            * std::exp(
                -0.00174 * turbine_count * turbine_count
            )
    );
}

}  // namespace core99::gridwake
