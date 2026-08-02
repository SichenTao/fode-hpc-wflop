/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T18 pure-C++ WindFLO evaluator, Cal validation and SOHO
CPU-HPC reconstruction
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Public assets, missing fields, source/paper conflicts, modeling corrections,
declared completions, semantic IDs and claim boundary:
include/core99/reddy_t18.hpp.
Literal public data: include/core99/t18_reddy_data.hpp and
include/core99/t18_reddy_terrain.inc.
HPC realization: a persistent full-core executor creates and evaluates each
complete SOHO offspring independently, writes fixed candidate/receipt slots,
then performs deterministic ordered environmental selection. Wind scenarios,
terrain grids, disk quadrature and turbine curves are immutable. Validation
matrix entries are independent and use fixed-index output. No nested teams or
schedule-dependent random stream is used.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/reddy_t18.hpp"

#include "core99/t18_reddy_data.hpp"
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t18 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int turbine_count = 25;
constexpr double farm_side_m = 2000.0;
constexpr double rho_kg_m3 = 1.2;
constexpr double reference_height_m = 105.0;
constexpr double reference_diameter_m = 90.0;
constexpr double reference_farm_capacity_mw = 75.0;
constexpr double reference_farm_capacity_mwh = 657000.0;
constexpr double cost_per_kw = 1230.0;
constexpr double turbulence_intensity = 0.083;
constexpr double golden_angle = 2.3999632297286533222;
constexpr int terrain_grid_side = 65;

struct WindScenario {
    double speed_mps = 0.0;
    double downwind_x = 0.0;
    double downwind_y = 0.0;
    double probability = 0.0;
};

struct FlowTurbine {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double diameter = 0.0;
    double height = 0.0;
};

struct EvalReceipt {
    Evaluation evaluation;
    std::uint64_t wind_scenarios = 0;
    std::uint64_t pair_checks = 0;
    std::uint64_t disk_samples = 0;
};

struct Candidate {
    std::vector<double> variables;
    EvalReceipt receipt;
};

struct OptimizerReceipt {
    RoleResult role;
    std::uint64_t objective_evaluations = 0;
    std::uint64_t wind_scenarios = 0;
    std::uint64_t pair_checks = 0;
    std::uint64_t disk_samples = 0;
    double evaluator_seconds = 0.0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t hash_double(const double value) {
    return std::bit_cast<std::uint64_t>(value);
}

template <std::size_t N>
double interpolate(
    const std::array<double, N>& x,
    const std::array<double, N>& y,
    const double value
) {
    if (value <= x.front()) return y.front();
    if (value >= x.back()) return y.back();
    const auto upper = std::upper_bound(x.begin(), x.end(), value);
    const std::size_t right = static_cast<std::size_t>(upper - x.begin());
    const std::size_t left = right - 1U;
    const double fraction = (value - x[left]) / (x[right] - x[left]);
    return y[left] + fraction * (y[right] - y[left]);
}

double induction_from_cp(const double cp_value) {
    const double cp = std::clamp(cp_value, 0.0, 16.0 / 27.0);
    double low = 0.0;
    double high = 1.0 / 3.0;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double middle = 0.5 * (low + high);
        const double estimate = 4.0 * middle
            * (1.0 - middle) * (1.0 - middle);
        if (estimate < cp) low = middle;
        else high = middle;
    }
    return 0.5 * (low + high);
}

double v90_cp(const double speed) {
    if (speed < data::v90_speed_mps.front() || speed > 25.0) return 0.0;
    return interpolate(data::v90_speed_mps, data::v90_cp, speed);
}

double v90_ct(const double speed) {
    const double a = induction_from_cp(v90_cp(speed));
    return std::clamp(4.0 * a * (1.0 - a), 0.01, 0.95);
}

double cal_ct(const double speed) {
    return interpolate(data::cal_ct_speed_mps, data::cal_ct, speed);
}

double turbine_power_w(
    const double speed,
    const double diameter,
    const bool v90_curve
) {
    const double cp = v90_curve
        ? v90_cp(speed)
        : interpolate(data::cal_speed_mps, data::cal_cp, speed);
    if (v90_curve && cp <= 0.0) return 0.0;
    const double area = std::numbers::pi * diameter * diameter / 4.0;
    return 0.5 * rho_kg_m3 * cp * speed * speed * speed * area;
}

double circle_overlap(
    const double radius_a,
    const double radius_b,
    const double separation
) {
    if (separation >= radius_a + radius_b) return 0.0;
    if (separation <= std::abs(radius_a - radius_b)) {
        const double radius = std::min(radius_a, radius_b);
        return std::numbers::pi * radius * radius;
    }
    const double alpha = 2.0 * std::acos(std::clamp(
        (separation * separation + radius_a * radius_a
         - radius_b * radius_b) / (2.0 * separation * radius_a),
        -1.0, 1.0
    ));
    const double beta = 2.0 * std::acos(std::clamp(
        (separation * separation + radius_b * radius_b
         - radius_a * radius_a) / (2.0 * separation * radius_b),
        -1.0, 1.0
    ));
    return 0.5 * radius_a * radius_a * (alpha - std::sin(alpha))
        + 0.5 * radius_b * radius_b * (beta - std::sin(beta));
}

double convex_hull_area(std::vector<std::pair<double, double>> points) {
    if (points.size() < 3U) return 0.0;
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3U) return 0.0;
    auto cross = [](const auto& origin, const auto& a, const auto& b) {
        return (a.first - origin.first) * (b.second - origin.second)
            - (a.second - origin.second) * (b.first - origin.first);
    };
    std::vector<std::pair<double, double>> hull;
    hull.reserve(points.size() * 2U);
    for (const auto& point : points) {
        while (hull.size() >= 2U
               && cross(hull[hull.size() - 2U], hull.back(), point) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const std::size_t lower = hull.size();
    for (auto iterator = points.rbegin() + 1; iterator != points.rend(); ++iterator) {
        while (hull.size() > lower
               && cross(hull[hull.size() - 2U], hull.back(), *iterator) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(*iterator);
    }
    double twice_area = 0.0;
    for (std::size_t index = 0; index + 1U < hull.size(); ++index) {
        twice_area += hull[index].first * hull[index + 1U].second
            - hull[index + 1U].first * hull[index].second;
    }
    return 0.5 * std::abs(twice_area);
}

bool solve_dense(std::vector<double>& matrix, std::vector<double>& rhs) {
    const int n = static_cast<int>(rhs.size());
    for (int column = 0; column < n; ++column) {
        int pivot = column;
        for (int row = column + 1; row < n; ++row) {
            if (std::abs(matrix[static_cast<std::size_t>(row * n + column)])
                > std::abs(matrix[static_cast<std::size_t>(pivot * n + column)])) {
                pivot = row;
            }
        }
        const double pivot_value = matrix[static_cast<std::size_t>(pivot * n + column)];
        if (std::abs(pivot_value) < 1.0e-12) return false;
        if (pivot != column) {
            for (int item = column; item < n; ++item) {
                std::swap(
                    matrix[static_cast<std::size_t>(column * n + item)],
                    matrix[static_cast<std::size_t>(pivot * n + item)]
                );
            }
            std::swap(rhs[static_cast<std::size_t>(column)], rhs[static_cast<std::size_t>(pivot)]);
        }
        for (int row = column + 1; row < n; ++row) {
            const double factor = matrix[static_cast<std::size_t>(row * n + column)]
                / matrix[static_cast<std::size_t>(column * n + column)];
            matrix[static_cast<std::size_t>(row * n + column)] = 0.0;
            for (int item = column + 1; item < n; ++item) {
                matrix[static_cast<std::size_t>(row * n + item)] -= factor
                    * matrix[static_cast<std::size_t>(column * n + item)];
            }
            rhs[static_cast<std::size_t>(row)] -= factor
                * rhs[static_cast<std::size_t>(column)];
        }
    }
    for (int row = n - 1; row >= 0; --row) {
        double value = rhs[static_cast<std::size_t>(row)];
        for (int column = row + 1; column < n; ++column) {
            value -= matrix[static_cast<std::size_t>(row * n + column)]
                * rhs[static_cast<std::size_t>(column)];
        }
        rhs[static_cast<std::size_t>(row)] = value
            / matrix[static_cast<std::size_t>(row * n + row)];
    }
    return true;
}

double source_idw(const double x, const double y) {
    struct Distance {
        double squared = 0.0;
        std::size_t index = 0;
    };
    std::array<Distance, 24> nearest{};
    nearest.fill({std::numeric_limits<double>::infinity(), 0U});
    for (std::size_t index = 0; index < data::awec_terrain.size(); ++index) {
        const auto& point = data::awec_terrain[index];
        const double dx = point.x_m - x;
        const double dy = point.y_m - y;
        const double squared = dx * dx + dy * dy;
        if (squared < 1.0e-18) return point.elevation_m;
        auto worst = std::max_element(
            nearest.begin(), nearest.end(),
            [](const auto& left, const auto& right) {
                return left.squared < right.squared;
            }
        );
        if (squared < worst->squared) *worst = {squared, index};
    }
    double numerator = 0.0;
    double denominator = 0.0;
    for (const auto& item : nearest) {
        const double weight = 1.0 / (item.squared * item.squared);
        numerator += weight * data::awec_terrain[item.index].elevation_m;
        denominator += weight;
    }
    return numerator / denominator;
}

double paper_local_rbf(const double x, const double y) {
    struct Distance {
        double squared = 0.0;
        std::size_t index = 0;
    };
    constexpr int neighbors = 16;
    std::array<Distance, neighbors> nearest{};
    nearest.fill({std::numeric_limits<double>::infinity(), 0U});
    for (std::size_t index = 0; index < data::awec_terrain.size(); ++index) {
        const auto& point = data::awec_terrain[index];
        const double dx = point.x_m - x;
        const double dy = point.y_m - y;
        const double squared = dx * dx + dy * dy;
        if (squared < 1.0e-18) return point.elevation_m;
        auto worst = std::max_element(
            nearest.begin(), nearest.end(),
            [](const auto& left, const auto& right) {
                return left.squared < right.squared;
            }
        );
        if (squared < worst->squared) *worst = {squared, index};
    }
    std::sort(nearest.begin(), nearest.end(), [](const auto& left, const auto& right) {
        return std::tie(left.squared, left.index) < std::tie(right.squared, right.index);
    });
    auto kernel = [](const double squared) {
        return std::sqrt(squared + 25.0);
    };
    std::vector<double> matrix(static_cast<std::size_t>(neighbors * neighbors));
    std::vector<double> weights(static_cast<std::size_t>(neighbors));
    for (int row = 0; row < neighbors; ++row) {
        const auto& left = data::awec_terrain[nearest[static_cast<std::size_t>(row)].index];
        weights[static_cast<std::size_t>(row)] = left.elevation_m;
        for (int column = 0; column < neighbors; ++column) {
            const auto& right = data::awec_terrain[nearest[static_cast<std::size_t>(column)].index];
            const double dx = left.x_m - right.x_m;
            const double dy = left.y_m - right.y_m;
            matrix[static_cast<std::size_t>(row * neighbors + column)] =
                kernel(dx * dx + dy * dy)
                + (row == column ? 1.0e-8 : 0.0);
        }
    }
    if (!solve_dense(matrix, weights)) return source_idw(x, y);
    double result = 0.0;
    for (int index = 0; index < neighbors; ++index) {
        const auto& point = data::awec_terrain[nearest[static_cast<std::size_t>(index)].index];
        const double dx = point.x_m - x;
        const double dy = point.y_m - y;
        result += weights[static_cast<std::size_t>(index)]
            * kernel(dx * dx + dy * dy);
    }
    return std::isfinite(result) ? result : source_idw(x, y);
}

std::vector<WindScenario> build_scenarios() {
    const double direction_total = std::accumulate(
        data::digitized_direction_totals.begin(),
        data::digitized_direction_totals.end(), 0.0
    );
    const double speed_total = std::accumulate(
        data::digitized_speed_bin_weights.begin(),
        data::digitized_speed_bin_weights.end(), 0.0
    );
    std::vector<WindScenario> scenarios;
    scenarios.reserve(data::direction_degrees.size()
                      * data::speed_bin_midpoints_mps.size());
    for (std::size_t direction = 0; direction < data::direction_degrees.size(); ++direction) {
        const double theta = data::direction_degrees[direction]
            * std::numbers::pi / 180.0;
        for (std::size_t speed = 0; speed < data::speed_bin_midpoints_mps.size(); ++speed) {
            scenarios.push_back({
                data::speed_bin_midpoints_mps[speed],
                std::cos(theta), std::sin(theta),
                data::digitized_direction_totals[direction] / direction_total
                    * data::digitized_speed_bin_weights[speed] / speed_total,
            });
        }
    }
    return scenarios;
}

struct WakeShape {
    double radius_y = 0.0;
    double radius_z = 0.0;
    double sigma_y = 0.0;
    double sigma_z = 0.0;
    double larsen_x0 = 0.0;
    double larsen_c1 = 0.0;
};

WakeShape wake_shape(
    const WakeModel model,
    const double diameter,
    const double ct,
    const double axial,
    const bool validation
) {
    WakeShape shape;
    const double beta = (1.0 + std::sqrt(std::max(1.0e-12, 1.0 - ct)))
        / (2.0 * std::sqrt(std::max(1.0e-12, 1.0 - ct)));
    if (model == WakeModel::jensen) {
        const double expansion = validation ? 0.075 : 0.045;
        shape.radius_y = 0.5 * diameter + expansion * axial;
        shape.radius_z = shape.radius_y;
    } else if (model == WakeModel::frandsen) {
        shape.radius_y = 0.5 * diameter * std::sqrt(beta)
            * (1.0 + 0.1 * axial / diameter);
        shape.radius_z = shape.radius_y;
    } else if (model == WakeModel::larsen) {
        const double r96d = 0.435449861
            * std::exp(0.797853685 * ct * ct - 0.124807893 * ct + 0.136821858)
            * (15.6298 * turbulence_intensity + 1.0) * diameter;
        const double denominator = std::pow(
            2.0 * r96d / (std::sqrt(beta) * diameter), 3.0
        ) - 1.0;
        shape.larsen_x0 = 9.6 * diameter
            / std::max(denominator, 1.0e-9);
        const double area = std::numbers::pi * diameter * diameter / 4.0;
        shape.larsen_c1 = std::pow(105.0 / (2.0 * std::numbers::pi), -0.5)
            * std::pow(0.5 * std::sqrt(beta) * diameter, 2.5)
            * std::pow(std::max(ct * area * shape.larsen_x0, 1.0e-18), -5.0 / 6.0);
        shape.radius_y = std::pow(
            105.0 * shape.larsen_c1 * shape.larsen_c1
                / (2.0 * std::numbers::pi),
            0.2
        ) * std::pow(
            std::max(ct * area * (axial + shape.larsen_x0), 1.0e-18),
            1.0 / 3.0
        );
        shape.radius_z = shape.radius_y;
    } else if (model == WakeModel::ishihara) {
        const double k = 0.11 * std::pow(ct, 1.07)
            * std::pow(turbulence_intensity, 0.2);
        const double epsilon = 0.23 * std::pow(ct, -0.25)
            * std::pow(turbulence_intensity, 0.17);
        shape.sigma_y = diameter * (k * axial / diameter + epsilon);
        shape.sigma_z = shape.sigma_y;
        // WindFLO returns 2*sigma as the wake diameter; its overlap and
        // inside-wake routines divide that diameter by two.
        shape.radius_y = shape.sigma_y;
        shape.radius_z = shape.radius_y;
    } else {
        const double epsilon = 0.2 * std::sqrt(beta);
        shape.sigma_y = diameter * (0.045 * axial / diameter + epsilon);
        shape.sigma_z = model == WakeModel::xa
            ? diameter * (0.0315 * axial / diameter + epsilon)
            : shape.sigma_y;
        shape.radius_y = shape.sigma_y;
        shape.radius_z = shape.sigma_z;
    }
    shape.radius_y = std::max(shape.radius_y, 1.0e-9);
    shape.radius_z = std::max(shape.radius_z, 1.0e-9);
    shape.sigma_y = std::max(shape.sigma_y, 1.0e-9);
    shape.sigma_z = std::max(shape.sigma_z, 1.0e-9);
    return shape;
}

double local_deficit(
    const WakeModel model,
    const WakeShape& shape,
    const double diameter,
    const double ct,
    const double axial,
    const double cross,
    const double vertical
) {
    if ((cross * cross) / (shape.radius_y * shape.radius_y)
            + (vertical * vertical) / (shape.radius_z * shape.radius_z) > 1.0) {
        return 0.0;
    }
    if (model == WakeModel::jensen) {
        const double radius_ratio = (0.5 * diameter) / shape.radius_y;
        return (1.0 - std::sqrt(std::max(0.0, 1.0 - ct)))
            * radius_ratio * radius_ratio;
    }
    if (model == WakeModel::frandsen) {
        const double turbine_area = std::numbers::pi * diameter * diameter / 4.0;
        const double wake_area = std::numbers::pi * shape.radius_y * shape.radius_y;
        return 0.5 * (1.0 - std::sqrt(std::max(
            0.0, 1.0 - 2.0 * turbine_area / wake_area * ct
        )));
    }
    const double radius = std::hypot(cross, vertical);
    if (model == WakeModel::larsen) {
        const double area = std::numbers::pi * diameter * diameter / 4.0;
        const double first = std::pow(
            std::max(ct * area / std::pow(axial + shape.larsen_x0, 2.0), 1.0e-18),
            1.0 / 3.0
        );
        const double inner = std::pow(radius, 1.5)
            * std::pow(
                std::max(3.0 * shape.larsen_c1 * shape.larsen_c1 * ct
                    * area * (axial + shape.larsen_x0), 1.0e-18),
                -0.5
            )
            - std::pow(35.0 / (2.0 * std::numbers::pi), 0.3)
                * std::pow(
                    std::max(3.0 * shape.larsen_c1 * shape.larsen_c1, 1.0e-18),
                    -0.2
                );
        return -(1.0 / 9.0) * first * inner * inner;
    }
    if (model == WakeModel::ishihara) {
        const double a = 0.93 * std::pow(ct, -0.75)
            * std::pow(turbulence_intensity, 0.17);
        const double b = 0.42 * std::pow(ct, 0.6)
            * std::pow(turbulence_intensity, 0.2);
        const double c = 0.15 * std::pow(ct, -0.25)
            * std::pow(turbulence_intensity, -0.7);
        const double denominator = a + b * axial / diameter
            + c * std::pow(1.0 + axial / diameter, -2.0);
        return std::exp(-0.5 * radius * radius
                        / (shape.sigma_y * shape.sigma_y))
            / (denominator * denominator);
    }
    const double amplitude = 1.0 - std::sqrt(std::max(
        0.0, 1.0 - ct * diameter * diameter
            / (8.0 * shape.sigma_y * shape.sigma_z)
    ));
    return amplitude * std::exp(-0.5 * (
        cross * cross / (shape.sigma_y * shape.sigma_y)
        + vertical * vertical / (shape.sigma_z * shape.sigma_z)
    ));
}

struct PairReceipt {
    double overlap_ratio = 0.0;
    double raw_delta = 0.0;
    double linear_delta = 0.0;
    double quadratic_delta_squared = 0.0;
    double energy_loss = 0.0;
    std::uint64_t samples = 0;
};

std::array<double, 2> source_sobol_2d(const std::uint64_t index) {
    const std::uint64_t gray = index ^ (index >> 1U);
    std::uint64_t first = 0U;
    std::uint64_t second = 0U;
    std::uint64_t second_direction = 1ULL << 63U;
    for (unsigned bit = 0; bit < 63U; ++bit) {
        if ((gray & (1ULL << bit)) != 0U) {
            first ^= 1ULL << (63U - bit);
            second ^= second_direction;
        }
        second_direction ^= second_direction >> 1U;
    }
    return {{
        static_cast<double>(std::ldexp(static_cast<long double>(first), -64)),
        static_cast<double>(std::ldexp(static_cast<long double>(second), -64)),
    }};
}

PairReceipt pair_delta(
    const WakeModel model,
    const FlowTurbine& upstream,
    const FlowTurbine& downstream,
    const double axial,
    const double cross,
    const double vertical,
    const double upstream_speed,
    const double downstream_ambient,
    const double ct,
    const DiskSampling sampling,
    const int quadrature_points,
    const bool validation
) {
    const WakeShape shape = wake_shape(
        model, upstream.diameter, ct, axial, validation
    );
    const double downstream_radius = 0.5 * downstream.diameter;
    if (model == WakeModel::jensen || model == WakeModel::frandsen) {
        const double overlap = circle_overlap(
            shape.radius_y, downstream_radius, std::hypot(cross, vertical)
        );
        const double ratio = overlap
            / (std::numbers::pi * downstream_radius * downstream_radius);
        const double deficit = local_deficit(
            model, shape, upstream.diameter, ct, axial, cross, vertical
        );
        const double wake_speed = (1.0 - deficit) * upstream_speed;
        const double delta = downstream_ambient - wake_speed;
        return {
            ratio,
            delta,
            ratio * delta,
            ratio * delta * delta,
            ratio * (downstream_ambient * downstream_ambient
                - wake_speed * wake_speed),
            0,
        };
    }
    double sum_linear = 0.0;
    double sum_quadratic = 0.0;
    double sum_energy = 0.0;
    int inside = 0;
    for (int sample = 0; sample < quadrature_points; ++sample) {
        double radius = 0.0;
        double angle = 0.0;
        if (sampling == DiskSampling::source_uniform_radius) {
            const auto point = source_sobol_2d(
                static_cast<std::uint64_t>(sample + 2)
            );
            radius = downstream_radius * point[1];
            angle = 2.0 * std::numbers::pi * point[0];
        } else {
            const double unit = (static_cast<double>(sample) + 0.5)
                / static_cast<double>(quadrature_points);
            radius = downstream_radius * std::sqrt(unit);
            angle = golden_angle * static_cast<double>(sample);
        }
        const double sample_cross = cross + radius * std::cos(angle);
        const double sample_vertical = vertical + radius * std::sin(angle);
        const double deficit = local_deficit(
            model, shape, upstream.diameter, ct, axial,
            sample_cross, sample_vertical
        );
        if (deficit != 0.0) {
            const double wake_speed = (1.0 - deficit) * upstream_speed;
            const double delta = downstream_ambient - wake_speed;
            sum_linear += delta;
            sum_quadratic += delta * delta;
            sum_energy += downstream_ambient * downstream_ambient
                - wake_speed * wake_speed;
            ++inside;
        }
    }
    if (sampling == DiskSampling::source_uniform_radius) {
        const double overlap = circle_overlap(
            shape.radius_y, downstream_radius, std::hypot(cross, vertical)
        );
        const double ratio = overlap
            / (std::numbers::pi * downstream_radius * downstream_radius);
        const double raw_delta = inside > 0
            ? sum_linear / static_cast<double>(inside) : 0.0;
        const double raw_quadratic = inside > 0
            ? sum_quadratic / static_cast<double>(inside) : 0.0;
        const double raw_energy = inside > 0
            ? sum_energy / static_cast<double>(inside) : 0.0;
        return {
            ratio,
            raw_delta,
            ratio * raw_delta,
            ratio * raw_quadratic,
            ratio * raw_energy,
            static_cast<std::uint64_t>(quadrature_points),
        };
    }
    const double denominator = static_cast<double>(quadrature_points);
    return {
        1.0,
        sum_linear / denominator,
        sum_linear / denominator,
        sum_quadratic / denominator,
        sum_energy / denominator,
        static_cast<std::uint64_t>(quadrature_points),
    };
}

double merge_speed(
    const double ambient,
    const std::vector<PairReceipt>& pairs,
    const MergeScheme scheme,
    const DiskSampling sampling
) {
    if (pairs.empty()) return ambient;
    double speed = ambient;
    if (scheme == MergeScheme::linear) {
        double delta = 0.0;
        for (const auto& pair : pairs) delta += pair.linear_delta;
        speed = ambient - delta;
    } else if (scheme == MergeScheme::quadratic) {
        double squared = 0.0;
        for (const auto& pair : pairs) {
            squared += pair.quadratic_delta_squared;
        }
        speed = ambient - std::sqrt(squared);
    } else if (scheme == MergeScheme::energy) {
        double loss = 0.0;
        for (const auto& pair : pairs) loss += pair.energy_loss;
        speed = std::sqrt(std::abs(ambient * ambient - loss));
    } else if (sampling == DiskSampling::source_uniform_radius) {
        double deficit = 0.0;
        for (const auto& pair : pairs) {
            deficit = pair.overlap_ratio
                * std::max(deficit, pair.raw_delta);
        }
        speed = ambient - deficit;
    } else {
        double deficit = 0.0;
        for (const auto& pair : pairs) {
            deficit = std::max(deficit, pair.linear_delta);
        }
        speed = ambient - deficit;
    }
    return std::clamp(speed, 0.0, 2.0 * ambient);
}

double constraint_violation(const std::vector<Turbine>& layout) {
    double violation = 0.0;
    if (layout.size() != static_cast<std::size_t>(turbine_count)) {
        return 1.0e9;
    }
    for (const auto& turbine : layout) {
        violation += std::max(0.0, -turbine.x_m)
            + std::max(0.0, turbine.x_m - farm_side_m)
            + std::max(0.0, -turbine.y_m)
            + std::max(0.0, turbine.y_m - farm_side_m);
        violation += std::max(0.0, 70.0 - turbine.diameter_m)
            + std::max(0.0, turbine.diameter_m - 110.0)
            + std::max(0.0, 70.0 - turbine.height_m)
            + std::max(0.0, turbine.height_m - 110.0)
            + std::max(0.0, 0.5 * turbine.diameter_m - turbine.height_m);
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            const double distance = std::hypot(
                layout[left].x_m - layout[right].x_m,
                layout[left].y_m - layout[right].y_m
            );
            violation += std::max(
                0.0, std::max(layout[left].diameter_m, layout[right].diameter_m)
                    - distance
            );
        }
    }
    return violation;
}

double farm_cost(const std::vector<Turbine>& layout) {
    double result = 0.0;
    for (const auto& turbine : layout) {
        result += cost_per_kw * 1.510
            * std::pow(turbine.diameter_m, 1.521)
            * std::pow(turbine.height_m, 0.076);
    }
    return result;
}

double grid_interpolate(
    const std::vector<double>& grid,
    const double x,
    const double y
) {
    const double spacing = farm_side_m / static_cast<double>(terrain_grid_side - 1);
    const double gx = std::clamp(x / spacing, 0.0, static_cast<double>(terrain_grid_side - 1));
    const double gy = std::clamp(y / spacing, 0.0, static_cast<double>(terrain_grid_side - 1));
    const int x0 = std::min(static_cast<int>(gx), terrain_grid_side - 2);
    const int y0 = std::min(static_cast<int>(gy), terrain_grid_side - 2);
    const double tx = gx - static_cast<double>(x0);
    const double ty = gy - static_cast<double>(y0);
    auto at = [&](const int ix, const int iy) {
        return grid[static_cast<std::size_t>(iy * terrain_grid_side + ix)];
    };
    return (1.0 - tx) * (1.0 - ty) * at(x0, y0)
        + tx * (1.0 - ty) * at(x0 + 1, y0)
        + (1.0 - tx) * ty * at(x0, y0 + 1)
        + tx * ty * at(x0 + 1, y0 + 1);
}

std::vector<Turbine> decode(
    const std::vector<double>& variables,
    const DesignCase design_case
) {
    std::vector<Turbine> layout(static_cast<std::size_t>(turbine_count));
    if (design_case == DesignCase::case1_layout) {
        for (int index = 0; index < turbine_count; ++index) {
            layout[static_cast<std::size_t>(index)].x_m = variables[static_cast<std::size_t>(2 * index)];
            layout[static_cast<std::size_t>(index)].y_m = variables[static_cast<std::size_t>(2 * index + 1)];
        }
    } else {
        for (int index = 0; index < turbine_count; ++index) {
            auto& turbine = layout[static_cast<std::size_t>(index)];
            turbine.x_m = variables[static_cast<std::size_t>(4 * index)];
            turbine.y_m = variables[static_cast<std::size_t>(4 * index + 1)];
            turbine.diameter_m = variables[static_cast<std::size_t>(4 * index + 2)];
            turbine.height_m = variables[static_cast<std::size_t>(4 * index + 3)];
        }
    }
    return layout;
}

std::vector<double> encode(
    const std::vector<Turbine>& layout,
    const DesignCase design_case
) {
    std::vector<double> variables(
        static_cast<std::size_t>(design_case == DesignCase::case1_layout ? 50 : 100)
    );
    for (int index = 0; index < turbine_count; ++index) {
        const auto& turbine = layout[static_cast<std::size_t>(index)];
        if (design_case == DesignCase::case1_layout) {
            variables[static_cast<std::size_t>(2 * index)] = turbine.x_m;
            variables[static_cast<std::size_t>(2 * index + 1)] = turbine.y_m;
        } else {
            variables[static_cast<std::size_t>(4 * index)] = turbine.x_m;
            variables[static_cast<std::size_t>(4 * index + 1)] = turbine.y_m;
            variables[static_cast<std::size_t>(4 * index + 2)] = turbine.diameter_m;
            variables[static_cast<std::size_t>(4 * index + 3)] = turbine.height_m;
        }
    }
    return variables;
}

std::pair<double, double> bounds(
    const DesignCase design_case,
    const std::size_t coordinate
) {
    if (design_case == DesignCase::case1_layout || coordinate % 4U < 2U) {
        return {0.0, farm_side_m};
    }
    return {70.0, 110.0};
}

void repair(std::vector<double>& variables, const DesignCase design_case) {
    for (std::size_t coordinate = 0; coordinate < variables.size(); ++coordinate) {
        const auto [low, high] = bounds(design_case, coordinate);
        variables[coordinate] = std::clamp(variables[coordinate], low, high);
    }
}

bool better(const Candidate& left, const Candidate& right) {
    const double left_violation = left.receipt.evaluation.constraint_violation;
    const double right_violation = right.receipt.evaluation.constraint_violation;
    if (std::abs(left_violation - right_violation) > 1.0e-12) {
        return left_violation < right_violation;
    }
    const double left_aep = left.receipt.evaluation.annual_energy_mwh;
    const double right_aep = right.receipt.evaluation.annual_energy_mwh;
    if (std::abs(left_aep - right_aep) > 1.0e-9) return left_aep > right_aep;
    return std::lexicographical_compare(
        left.variables.begin(), left.variables.end(),
        right.variables.begin(), right.variables.end()
    );
}

std::uint64_t hash_result(const RunResult& result) {
    std::uint64_t hash = 0x18012020ULL;
    hash = mix_hash(hash, result.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.population));
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.generations));
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.validation.size()));
    for (const auto& item : result.validation) {
        hash = mix_hash(hash, hash_double(item.predicted_velocity_mps));
        hash = mix_hash(hash, hash_double(item.relative_error_percent));
    }
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.roles.size()));
    for (const auto& role : result.roles) {
        hash = mix_hash(hash, hash_double(role.evaluation.annual_energy_mwh));
        hash = mix_hash(hash, hash_double(role.evaluation.constraint_violation));
        for (const auto& turbine : role.layout) {
            hash = mix_hash(hash, hash_double(turbine.x_m));
            hash = mix_hash(hash, hash_double(turbine.y_m));
            hash = mix_hash(hash, hash_double(turbine.diameter_m));
            hash = mix_hash(hash, hash_double(turbine.height_m));
        }
        for (const int kernel : role.kernel_sequence) {
            hash = mix_hash(hash, static_cast<std::uint64_t>(kernel));
        }
    }
    return hash;
}

}  // namespace

struct Problem::Impl {
    std::vector<double> paper_rbf_grid;
    std::vector<double> source_idw_grid;
    std::vector<WindScenario> scenarios;
    double precompute_seconds = 0.0;

    Impl() {
        const auto start = Clock::now();
        paper_rbf_grid.resize(static_cast<std::size_t>(terrain_grid_side * terrain_grid_side));
        source_idw_grid.resize(static_cast<std::size_t>(terrain_grid_side * terrain_grid_side));
        const double spacing = farm_side_m / static_cast<double>(terrain_grid_side - 1);
        for (int row = 0; row < terrain_grid_side; ++row) {
            for (int column = 0; column < terrain_grid_side; ++column) {
                const double x = spacing * static_cast<double>(column);
                const double y = spacing * static_cast<double>(row);
                const std::size_t index = static_cast<std::size_t>(row * terrain_grid_side + column);
                paper_rbf_grid[index] = paper_local_rbf(x, y);
                source_idw_grid[index] = source_idw(x, y);
            }
        }
        scenarios = build_scenarios();
        precompute_seconds = elapsed_seconds(start);
    }

    [[nodiscard]] double terrain(
        const TerrainProfile profile,
        const double x,
        const double y
    ) const {
        return grid_interpolate(
            profile == TerrainProfile::paper_local_rbf
                ? paper_rbf_grid : source_idw_grid,
            x, y
        );
    }

    [[nodiscard]] double scenario_power_w(
        const std::vector<Turbine>& layout,
        const WakeModel wake,
        const MergeScheme merge,
        const TerrainProfile terrain_profile,
        const DiskSampling sampling,
        const int quadrature_points,
        const WindScenario& scenario,
        std::uint64_t& pair_checks,
        std::uint64_t& disk_samples
    ) const {
        std::vector<FlowTurbine> turbines(layout.size());
        std::vector<double> downwind(layout.size());
        std::vector<double> crosswind(layout.size());
        for (std::size_t index = 0; index < layout.size(); ++index) {
            const auto& item = layout[index];
            turbines[index] = {
                item.x_m, item.y_m,
                terrain(terrain_profile, item.x_m, item.y_m),
                item.diameter_m, item.height_m,
            };
            downwind[index] = item.x_m * scenario.downwind_x
                + item.y_m * scenario.downwind_y;
            crosswind[index] = -item.x_m * scenario.downwind_y
                + item.y_m * scenario.downwind_x;
        }
        std::vector<int> order(static_cast<int>(layout.size()));
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
            return std::tie(downwind[static_cast<std::size_t>(left)], left)
                < std::tie(downwind[static_cast<std::size_t>(right)], right);
        });
        std::vector<double> speeds(layout.size(), 0.0);
        for (std::size_t ranked = 0; ranked < order.size(); ++ranked) {
            const int downstream_index = order[ranked];
            const auto& downstream = turbines[static_cast<std::size_t>(downstream_index)];
            const double ambient = scenario.speed_mps * std::pow(
                downstream.height / reference_height_m, 0.143
            );
            std::vector<PairReceipt> pairs;
            pairs.reserve(ranked);
            for (std::size_t prior = 0; prior < ranked; ++prior) {
                const int upstream_index = order[prior];
                const auto& upstream = turbines[static_cast<std::size_t>(upstream_index)];
                const double horizontal_axial = downwind[static_cast<std::size_t>(downstream_index)]
                    - downwind[static_cast<std::size_t>(upstream_index)];
                if (horizontal_axial <= 1.0e-9) continue;
                ++pair_checks;
                const double terrain_delta = downstream.z - upstream.z;
                const double axial = std::hypot(horizontal_axial, terrain_delta);
                const double cross = crosswind[static_cast<std::size_t>(downstream_index)]
                    - crosswind[static_cast<std::size_t>(upstream_index)];
                const double vertical = downstream.height - upstream.height;
                const double upstream_speed = speeds[static_cast<std::size_t>(upstream_index)];
                const double ct = v90_ct(upstream_speed);
                const auto pair = pair_delta(
                    wake, upstream, downstream, axial, cross, vertical,
                    upstream_speed, ambient, ct, sampling,
                    quadrature_points, false
                );
                disk_samples += pair.samples;
                if (pair.overlap_ratio > 0.0) {
                    pairs.push_back(pair);
                }
            }
            speeds[static_cast<std::size_t>(downstream_index)] = merge_speed(
                ambient, pairs, merge, sampling
            );
        }
        double power = 0.0;
        for (std::size_t index = 0; index < layout.size(); ++index) {
            power += turbine_power_w(
                speeds[index], layout[index].diameter_m, true
            );
        }
        return power;
    }

    [[nodiscard]] EvalReceipt evaluate_receipt(
        const std::vector<Turbine>& layout,
        const WakeModel wake,
        const TerrainProfile terrain_profile,
        const DiskSampling sampling,
        const int quadrature_points
    ) const {
        if (quadrature_points < 1) {
            throw std::invalid_argument("T18 disk quadrature must be positive");
        }
        EvalReceipt receipt;
        auto& result = receipt.evaluation;
        result.constraint_violation = constraint_violation(layout);
        result.feasible = result.constraint_violation <= 1.0e-12;
        std::vector<std::pair<double, double>> points;
        points.reserve(layout.size());
        for (const auto& turbine : layout) points.emplace_back(turbine.x_m, turbine.y_m);
        result.land_used_km2 = convex_hull_area(std::move(points)) / 1.0e6;
        result.farm_cost_usd = farm_cost(layout);

        double expected_power_w = 0.0;
        for (const auto& scenario : scenarios) {
            expected_power_w += scenario.probability * scenario_power_w(
                layout, wake, MergeScheme::quadratic, terrain_profile,
                sampling, quadrature_points, scenario,
                receipt.pair_checks, receipt.disk_samples
            );
        }
        receipt.wind_scenarios = scenarios.size();
        result.annual_energy_mwh = expected_power_w * 8760.0 / 1.0e6;
        result.normalized_aep = result.annual_energy_mwh
            / reference_farm_capacity_mwh;

        WindScenario reporting;
        reporting.speed_mps = 10.0;
        reporting.downwind_x = 0.0;
        reporting.downwind_y = -1.0;
        reporting.probability = 1.0;
        result.farm_power_mw = scenario_power_w(
            layout, wake, MergeScheme::quadratic, terrain_profile,
            sampling, quadrature_points, reporting,
            receipt.pair_checks, receipt.disk_samples
        ) / 1.0e6;
        result.farm_efficiency = result.farm_power_mw
            / reference_farm_capacity_mw;
        result.coe_usd_kwh = result.annual_energy_mwh > 0.0
            ? result.farm_cost_usd / (result.annual_energy_mwh * 1000.0)
            : std::numeric_limits<double>::infinity();
        return receipt;
    }

    [[nodiscard]] double validation_probe_velocity(
        const WakeModel wake,
        const MergeScheme merge,
        const double probe_x,
        const DiskSampling sampling,
        const int quadrature_points
    ) const {
        std::vector<FlowTurbine> turbines;
        turbines.reserve(9);
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                turbines.push_back({
                    0.84 * static_cast<double>(column),
                    0.36 * static_cast<double>(row),
                    0.0, 0.12, 0.12,
                });
            }
        }
        constexpr std::array<double, 4> nodes{{
            -0.339981043584856, -0.861136311594053,
             0.339981043584856,  0.861136311594053,
        }};
        constexpr std::array<double, 4> weights{{
            0.652145154862546, 0.347854845137454,
            0.652145154862546, 0.347854845137454,
        }};
        auto ambient_average = [&](const double height, const double radius) {
            double value = 0.0;
            for (std::size_t radial = 0; radial < nodes.size(); ++radial) {
                for (std::size_t angle = 0; angle < nodes.size(); ++angle) {
                    const double r = 0.5 * (nodes[radial] + 1.0) * radius;
                    const double theta = std::numbers::pi * (nodes[angle] + 1.0);
                    const double z = std::max(1.0e-6, height + r * std::sin(theta));
                    const double profile = 0.48 / 0.4 * std::log(z / 0.00033);
                    value += weights[radial] * weights[angle] * profile
                        * (nodes[radial] + 1.0);
                }
            }
            return 0.25 * value;
        };
        const double ambient = ambient_average(0.12, 0.06);
        std::vector<int> order(static_cast<int>(turbines.size()));
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
            return std::tie(turbines[static_cast<std::size_t>(left)].x, left)
                < std::tie(turbines[static_cast<std::size_t>(right)].x, right);
        });
        std::vector<double> speeds(turbines.size(), ambient);
        for (std::size_t ranked = 0; ranked < order.size(); ++ranked) {
            const int downstream_index = order[ranked];
            const auto& downstream = turbines[static_cast<std::size_t>(downstream_index)];
            std::vector<PairReceipt> pairs;
            for (std::size_t prior = 0; prior < ranked; ++prior) {
                const int upstream_index = order[prior];
                const auto& upstream = turbines[static_cast<std::size_t>(upstream_index)];
                const double axial = downstream.x - upstream.x;
                if (axial <= 1.0e-12) continue;
                const double cross = downstream.y - upstream.y;
                const auto pair = pair_delta(
                    wake, upstream, downstream, axial, cross, 0.0,
                    speeds[static_cast<std::size_t>(upstream_index)], ambient,
                    cal_ct(speeds[static_cast<std::size_t>(upstream_index)]),
                    sampling, quadrature_points, true
                );
                if (pair.overlap_ratio > 0.0) pairs.push_back(pair);
            }
            speeds[static_cast<std::size_t>(downstream_index)] = merge_speed(
                ambient, pairs, merge, sampling
            );
        }
        const FlowTurbine probe{probe_x, 0.36, 0.0, 0.12, 0.12};
        std::vector<PairReceipt> pairs;
        for (std::size_t index = 0; index < turbines.size(); ++index) {
            const auto& upstream = turbines[index];
            const double axial = probe.x - upstream.x;
            if (axial <= 1.0e-12) continue;
            const auto pair = pair_delta(
                wake, upstream, probe, axial, probe.y - upstream.y, 0.0,
                speeds[index], ambient, cal_ct(speeds[index]), sampling,
                quadrature_points, true
            );
            if (pair.overlap_ratio > 0.0) pairs.push_back(pair);
        }
        return merge_speed(ambient, pairs, merge, sampling);
    }
};

Problem::Problem() : impl_(std::make_unique<Impl>()) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

Evaluation Problem::evaluate(
    const std::vector<Turbine>& layout,
    const WakeModel wake,
    const TerrainProfile terrain_profile,
    const DiskSampling disk_sampling,
    const int disk_quadrature_points
) const {
    return impl_->evaluate_receipt(
        layout, wake, terrain_profile, disk_sampling, disk_quadrature_points
    ).evaluation;
}

std::vector<Turbine> Problem::reference_layout() const {
    std::vector<Turbine> layout;
    layout.reserve(turbine_count);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            layout.push_back({
                500.0 * static_cast<double>(column),
                500.0 * static_cast<double>(row),
                reference_diameter_m, reference_height_m,
            });
        }
    }
    return layout;
}

std::vector<ValidationRecord> Problem::validate_wind_tunnel(
    const DiskSampling disk_sampling,
    const int disk_quadrature_points
) const {
    if (disk_quadrature_points < 1) {
        throw std::invalid_argument("T18 validation quadrature must be positive");
    }
    const std::array<WakeModel, 6> wakes{{
        WakeModel::jensen, WakeModel::frandsen, WakeModel::larsen,
        WakeModel::ishihara, WakeModel::bp, WakeModel::xa,
    }};
    const std::array<MergeScheme, 4> merges{{
        MergeScheme::linear, MergeScheme::quadratic,
        MergeScheme::energy, MergeScheme::dwm,
    }};
    std::vector<ValidationRecord> result;
    result.reserve(48);
    for (const auto& location : {
        std::tuple<std::string, double, double>{"table2_upstream", 1.65, 6.24},
        std::tuple<std::string, double, double>{"table3_downstream", 1.76, 5.18},
    }) {
        for (const auto merge : merges) {
            for (const auto wake : wakes) {
                const double predicted = impl_->validation_probe_velocity(
                    wake, merge, std::get<1>(location), disk_sampling,
                    disk_quadrature_points
                );
                const double observed = std::get<2>(location);
                result.push_back({
                    std::get<0>(location) + "_" + to_string(wake)
                        + "_" + to_string(merge),
                    wake, merge, predicted, observed,
                    100.0 * std::abs(predicted - observed) / observed,
                });
            }
        }
    }
    return result;
}

namespace {

OptimizerReceipt optimize_role(
    const Problem::Impl& problem,
    const std::vector<Turbine>& reference,
    const WakeModel wake,
    const DesignCase design_case,
    const RunConfig& config,
    fode::PersistentExecutor& executor,
    const std::uint64_t role_seed
) {
    OptimizerReceipt result;
    fode::CounterRng rng(role_seed);
    const std::size_t dimensions = static_cast<std::size_t>(
        design_case == DesignCase::case1_layout ? 50 : 100
    );
    std::vector<Candidate> population(static_cast<std::size_t>(config.population));
    population.front().variables = encode(reference, design_case);
    for (int individual = 1; individual < config.population; ++individual) {
        auto& variables = population[static_cast<std::size_t>(individual)].variables;
        variables.resize(dimensions);
        for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate) {
            const auto [low, high] = bounds(design_case, coordinate);
            variables[coordinate] = low + (high - low) * rng.uniform(
                0, 1, static_cast<std::uint64_t>(individual), coordinate
            );
        }
    }

    auto evaluate_candidates = [&](std::vector<Candidate>& candidates) {
        std::vector<double> timings(candidates.size(), 0.0);
        executor.parallel_for(0, static_cast<int>(candidates.size()), [&](const int index) {
            const auto start = Clock::now();
            auto& candidate = candidates[static_cast<std::size_t>(index)];
            candidate.receipt = problem.evaluate_receipt(
                decode(candidate.variables, design_case), wake,
                config.terrain_profile, config.disk_sampling,
                config.disk_quadrature_points
            );
            timings[static_cast<std::size_t>(index)] = elapsed_seconds(start);
        });
        result.objective_evaluations += candidates.size();
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            result.wind_scenarios += candidates[index].receipt.wind_scenarios;
            result.pair_checks += candidates[index].receipt.pair_checks;
            result.disk_samples += candidates[index].receipt.disk_samples;
            result.evaluator_seconds += timings[index];
        }
    };

    evaluate_candidates(population);
    std::stable_sort(population.begin(), population.end(), better);
    Candidate best = population.front();
    int active_kernel = rng.integer(0, 3, 0, 2, 0);
    result.role.starting_kernel = active_kernel;
    result.role.kernel_sequence.reserve(static_cast<std::size_t>(config.generations + 1));
    result.role.kernel_sequence.push_back(active_kernel);
    int stagnant = 0;

    for (int generation = 0; generation < config.generations; ++generation) {
        std::vector<Candidate> children(static_cast<std::size_t>(config.population));
        executor.parallel_for(0, config.population, [&](const int child_index) {
            auto& child = children[static_cast<std::size_t>(child_index)];
            child.variables.resize(dimensions);
            auto random_index = [&](const std::uint64_t phase, const std::uint64_t draw) {
                return rng.integer(
                    0, config.population, static_cast<std::uint64_t>(generation + 1),
                    phase, static_cast<std::uint64_t>(child_index), draw
                );
            };
            if (active_kernel == 1) {
                int r1 = random_index(10, 0);
                int r2 = random_index(10, 1);
                int r3 = random_index(10, 2);
                while (r1 == child_index) r1 = (r1 + 1) % config.population;
                while (r2 == child_index || r2 == r1) r2 = (r2 + 1) % config.population;
                while (r3 == child_index || r3 == r1 || r3 == r2) {
                    r3 = (r3 + 1) % config.population;
                }
                const int forced = rng.integer(
                    0, static_cast<int>(dimensions), generation + 1, 11,
                    static_cast<std::uint64_t>(child_index)
                );
                for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate) {
                    const double mutant = population[static_cast<std::size_t>(r1)].variables[coordinate]
                        + 0.5 * (
                            population[static_cast<std::size_t>(r2)].variables[coordinate]
                            - population[static_cast<std::size_t>(r3)].variables[coordinate]
                        );
                    const bool crossover = static_cast<int>(coordinate) == forced
                        || rng.uniform(generation + 1, 12,
                                       static_cast<std::uint64_t>(child_index), coordinate) < 0.7;
                    child.variables[coordinate] = crossover ? mutant
                        : population[static_cast<std::size_t>(child_index)].variables[coordinate];
                }
            } else {
                int parent_a = 0;
                int parent_b = 0;
                if (active_kernel == 0) {
                    parent_a = random_index(20, 0);
                    parent_b = random_index(20, 1);
                } else {
                    const int radius = std::max(1, std::min(20, config.population) / 2);
                    const int offset_a = rng.integer(
                        -radius, radius + 1, generation + 1, 21,
                        static_cast<std::uint64_t>(child_index), 0
                    );
                    const int offset_b = rng.integer(
                        -radius, radius + 1, generation + 1, 21,
                        static_cast<std::uint64_t>(child_index), 1
                    );
                    parent_a = (child_index + offset_a + config.population)
                        % config.population;
                    parent_b = (child_index + offset_b + config.population)
                        % config.population;
                }
                for (std::size_t coordinate = 0; coordinate < dimensions; ++coordinate) {
                    const double u = std::clamp(
                        rng.uniform(generation + 1, 22,
                                    static_cast<std::uint64_t>(child_index), coordinate),
                        1.0e-12, 1.0 - 1.0e-12
                    );
                    const double beta = u <= 0.5
                        ? std::pow(2.0 * u, 1.0 / 31.0)
                        : std::pow(1.0 / (2.0 * (1.0 - u)), 1.0 / 31.0);
                    const double sign = rng.uniform(
                        generation + 1, 23,
                        static_cast<std::uint64_t>(child_index), coordinate
                    ) < 0.5 ? 1.0 : -1.0;
                    double value = 0.5 * (
                        (1.0 + sign * beta)
                            * population[static_cast<std::size_t>(parent_a)].variables[coordinate]
                        + (1.0 - sign * beta)
                            * population[static_cast<std::size_t>(parent_b)].variables[coordinate]
                    );
                    if (rng.uniform(generation + 1, 24,
                                    static_cast<std::uint64_t>(child_index), coordinate)
                        < 1.0 / static_cast<double>(dimensions)) {
                        const auto [low, high] = bounds(design_case, coordinate);
                        const double mutation_u = rng.uniform(
                            generation + 1, 25,
                            static_cast<std::uint64_t>(child_index), coordinate
                        );
                        const double delta = mutation_u < 0.5
                            ? std::pow(2.0 * mutation_u, 1.0 / 21.0) - 1.0
                            : 1.0 - std::pow(2.0 * (1.0 - mutation_u), 1.0 / 21.0);
                        value += delta * (high - low);
                    }
                    child.variables[coordinate] = value;
                }
            }
            repair(child.variables, design_case);
        });
        evaluate_candidates(children);
        population.insert(
            population.end(),
            std::make_move_iterator(children.begin()),
            std::make_move_iterator(children.end())
        );
        std::stable_sort(population.begin(), population.end(), better);
        population.resize(static_cast<std::size_t>(config.population));
        if (better(population.front(), best)) {
            best = population.front();
            stagnant = 0;
        } else {
            ++stagnant;
        }
        if (stagnant >= config.stagnation_generations) {
            const int choice = rng.integer(
                0, 2, generation + 1, 30, static_cast<std::uint64_t>(active_kernel)
            );
            active_kernel = (active_kernel + 1 + choice) % 3;
            result.role.switch_generations.push_back(generation + 1);
            stagnant = 0;
        }
        result.role.kernel_sequence.push_back(active_kernel);
    }
    std::stable_sort(population.begin(), population.end(), better);
    result.role.wake = wake;
    result.role.design_case = design_case;
    result.role.reference = false;
    result.role.evaluation = population.front().receipt.evaluation;
    result.role.layout = decode(population.front().variables, design_case);
    result.role.role = "table4_" + to_string(wake) + "_" + to_string(design_case);
    return result;
}

}  // namespace

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.population < 4
        || config.generations < 1 || config.stagnation_generations < 1
        || config.disk_quadrature_points < 1
        || config.validation_disk_quadrature_points < 1) {
        throw std::invalid_argument("T18 invalid run configuration");
    }
    const auto total_start = Clock::now();
    RunResult result;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = config.population;
    result.generations = config.generations;
    result.stagnation_generations = config.stagnation_generations;
    result.disk_quadrature_points = config.disk_quadrature_points;
    result.validation_disk_quadrature_points =
        config.validation_disk_quadrature_points;
    result.terrain_profile = config.terrain_profile;
    result.disk_sampling = config.disk_sampling;
    result.validation_disk_sampling = config.validation_disk_sampling;
    result.terrain_precompute_seconds = problem.impl_->precompute_seconds;
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();

    const auto validation_start = Clock::now();
    const std::array<WakeModel, 6> wakes{{
        WakeModel::jensen, WakeModel::frandsen, WakeModel::larsen,
        WakeModel::ishihara, WakeModel::bp, WakeModel::xa,
    }};
    const std::array<MergeScheme, 4> merges{{
        MergeScheme::linear, MergeScheme::quadratic,
        MergeScheme::energy, MergeScheme::dwm,
    }};
    struct ValidationSpec {
        std::string prefix;
        double x = 0.0;
        double observed = 0.0;
        WakeModel wake = WakeModel::jensen;
        MergeScheme merge = MergeScheme::linear;
    };
    std::vector<ValidationSpec> validation_specs;
    validation_specs.reserve(48);
    for (const auto& location : {
        std::tuple<std::string, double, double>{"table2_upstream", 1.65, 6.24},
        std::tuple<std::string, double, double>{"table3_downstream", 1.76, 5.18},
    }) {
        for (const auto merge : merges) {
            for (const auto wake : wakes) {
                validation_specs.push_back({
                    std::get<0>(location), std::get<1>(location),
                    std::get<2>(location), wake, merge,
                });
            }
        }
    }
    result.validation.resize(validation_specs.size());
    executor.parallel_for(0, static_cast<int>(validation_specs.size()), [&](const int index) {
        const auto& spec = validation_specs[static_cast<std::size_t>(index)];
        const double predicted = problem.impl_->validation_probe_velocity(
            spec.wake, spec.merge, spec.x, config.validation_disk_sampling,
            config.validation_disk_quadrature_points
        );
        result.validation[static_cast<std::size_t>(index)] = {
            spec.prefix + "_" + to_string(spec.wake) + "_" + to_string(spec.merge),
            spec.wake, spec.merge, predicted, spec.observed,
            100.0 * std::abs(predicted - spec.observed) / spec.observed,
        };
    });
    result.validation_seconds = elapsed_seconds(validation_start);

    const auto algorithm_start = Clock::now();
    const auto reference = problem.reference_layout();
    for (const auto wake : {WakeModel::frandsen, WakeModel::bp}) {
        const auto start = Clock::now();
        const auto receipt = problem.impl_->evaluate_receipt(
            reference, wake, config.terrain_profile,
            config.disk_sampling, config.disk_quadrature_points
        );
        result.evaluator_seconds += elapsed_seconds(start);
        result.objective_evaluations += 1;
        result.wind_scenario_layout_evaluations += receipt.wind_scenarios;
        result.wake_pair_checks += receipt.pair_checks;
        result.disk_quadrature_samples += receipt.disk_samples;
        RoleResult role;
        role.role = "table4_" + to_string(wake) + "_reference";
        role.wake = wake;
        role.design_case = DesignCase::case1_layout;
        role.reference = true;
        role.evaluation = receipt.evaluation;
        role.layout = reference;
        role.starting_kernel = -1;
        result.roles.push_back(std::move(role));
        for (const auto design_case : {
            DesignCase::case1_layout, DesignCase::case2_layout_turbine
        }) {
            const std::uint64_t role_seed = mix_hash(
                config.seed,
                static_cast<std::uint64_t>(static_cast<int>(wake) * 10
                    + static_cast<int>(design_case) + 1)
            );
            auto optimized = optimize_role(
                *problem.impl_, reference, wake, design_case, config,
                executor, role_seed
            );
            result.objective_evaluations += optimized.objective_evaluations;
            result.wind_scenario_layout_evaluations += optimized.wind_scenarios;
            result.wake_pair_checks += optimized.pair_checks;
            result.disk_quadrature_samples += optimized.disk_samples;
            result.evaluator_seconds += optimized.evaluator_seconds;
            result.roles.push_back(std::move(optimized.role));
        }
    }
    result.algorithm_seconds = elapsed_seconds(algorithm_start);
    const auto executor_receipt = executor.work_receipt();
    result.observed_workers = executor_receipt.distinct_participants;
    result.parallel_regions = executor_receipt.parallel_regions;
    result.scientific_hash = hash_result(result);
    result.end_to_end_seconds = problem.impl_->precompute_seconds
        + elapsed_seconds(total_start);
    return result;
}

std::string to_string(const WakeModel value) {
    switch (value) {
        case WakeModel::jensen: return "jensen";
        case WakeModel::frandsen: return "frandsen";
        case WakeModel::larsen: return "larsen";
        case WakeModel::ishihara: return "ishihara";
        case WakeModel::bp: return "bp";
        case WakeModel::xa: return "xa";
    }
    throw std::invalid_argument("T18 unknown wake model");
}

std::string to_string(const MergeScheme value) {
    switch (value) {
        case MergeScheme::linear: return "linear";
        case MergeScheme::quadratic: return "quadratic";
        case MergeScheme::energy: return "energy";
        case MergeScheme::dwm: return "dwm";
    }
    throw std::invalid_argument("T18 unknown merge scheme");
}

std::string to_string(const TerrainProfile value) {
    return value == TerrainProfile::paper_local_rbf
        ? "paper_local_rbf" : "source_example_idw";
}

std::string to_string(const DiskSampling value) {
    return value == DiskSampling::paper_area_correct
        ? "paper_area_correct" : "source_uniform_radius";
}

std::string to_string(const DesignCase value) {
    return value == DesignCase::case1_layout
        ? "case1_layout" : "case2_layout_turbine";
}

}  // namespace core99::t18
