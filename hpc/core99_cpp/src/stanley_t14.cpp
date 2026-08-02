/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T14 pure-C++ boundary-grid evaluator and optimizer
Paper DOI: 10.5194/wes-4-663-2019
Public source: https://github.com/byuflowlab/stanley2019-variable-reduction
revision 62b590065f9541c4296338b3f1a0ee07cfcd28bc; archive
10.5281/zenodo.3523383
Provided/missing/conflicting facts and Reconstruction decisions:
include/core99/stanley_t14.hpp
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Independent oracle: scripts/validate_core99_t14.py
HPC design: search candidates are independent and evaluated by a persistent
all-core team; final wind states are independent and use the same team;
selection and FES commits remain deterministic
Claim boundary: academic declared contribution/problem reproduction, not
author SNOPT, Tapenade-gradient, Akima-package, or RNG replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/stanley_t14.hpp"

#include "core99/t14_stanley_data.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t14 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kTurbines = 100;
constexpr double kRotorDiameter = 130.0;
constexpr double kMinimumSpacing = 260.0;
constexpr double kHubHeight = 110.0;
constexpr double kPi = std::numbers::pi;
constexpr double kGeneratorEfficiency = 0.93;

struct WindState {
    double direction_deg = 0.0;
    double speed = 0.0;
    double frequency = 0.0;
};

struct GridTopology {
    std::vector<std::pair<int, int>> indices;
    double initial_dx = 0.0;
    double initial_dy = 0.0;
    double initial_shear = 0.0;
    double initial_rotation = 0.0;
};

struct Encoding {
    Representation representation = Representation::boundary_grid;
    std::vector<double> values;
    std::vector<double> scales;
    GridTopology topology;
    int outer_turbines = 0;
};

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double cross(const Point& left, const Point& right, const Point& point) {
    return (right.x - left.x) * (point.y - left.y)
        - (right.y - left.y) * (point.x - left.x);
}

double polygon_area(const std::vector<Point>& polygon) {
    double twice_area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point& current = polygon[index];
        const Point& next = polygon[(index + 1) % polygon.size()];
        twice_area += current.x * next.y - next.x * current.y;
    }
    return 0.5 * std::abs(twice_area);
}

std::vector<Point> convex_hull(std::vector<Point> points) {
    std::sort(points.begin(), points.end(), [](const Point& left, const Point& right) {
        return std::tie(left.x, left.y) < std::tie(right.x, right.y);
    });
    points.erase(
        std::unique(points.begin(), points.end(), [](const Point& left, const Point& right) {
            return left.x == right.x && left.y == right.y;
        }),
        points.end()
    );
    if (points.size() < 3) {
        throw std::runtime_error("T14 boundary needs at least three points");
    }
    std::vector<Point> hull;
    for (const Point& point : points) {
        while (
            hull.size() >= 2
            && cross(hull[hull.size() - 2], hull.back(), point) <= 0.0
        ) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const std::size_t lower = hull.size();
    for (auto iterator = points.rbegin() + 1; iterator != points.rend(); ++iterator) {
        while (
            hull.size() > lower
            && cross(hull[hull.size() - 2], hull.back(), *iterator) <= 0.0
        ) {
            hull.pop_back();
        }
        hull.push_back(*iterator);
    }
    hull.pop_back();
    return hull;
}

Point centroid(const std::vector<Point>& polygon) {
    Point result{};
    for (const Point& point : polygon) {
        result.x += point.x;
        result.y += point.y;
    }
    result.x /= static_cast<double>(polygon.size());
    result.y /= static_cast<double>(polygon.size());
    return result;
}

std::vector<Point> scaled_polygon(
    const std::vector<Point>& polygon,
    double scale
) {
    const Point center = centroid(polygon);
    std::vector<Point> result;
    result.reserve(polygon.size());
    for (const Point& point : polygon) {
        result.push_back({
            center.x + scale * (point.x - center.x),
            center.y + scale * (point.y - center.y),
        });
    }
    return result;
}

bool polygon_contains(const std::vector<Point>& polygon, const Point& point) {
    constexpr double tolerance = 1.0e-9;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        if (cross(
                polygon[index],
                polygon[(index + 1) % polygon.size()],
                point
            ) < -tolerance) {
            return false;
        }
    }
    return true;
}

Point project_inside(const std::vector<Point>& polygon, Point point) {
    if (polygon_contains(polygon, point)) {
        return point;
    }
    const Point center = centroid(polygon);
    double low = 0.0;
    double high = 1.0;
    for (int iteration = 0; iteration < 60; ++iteration) {
        const double fraction = 0.5 * (low + high);
        const Point candidate{
            center.x + fraction * (point.x - center.x),
            center.y + fraction * (point.y - center.y),
        };
        if (polygon_contains(polygon, candidate)) {
            low = fraction;
        } else {
            high = fraction;
        }
    }
    return {
        center.x + low * (point.x - center.x),
        center.y + low * (point.y - center.y),
    };
}

void repair_spacing(
    const std::vector<Point>& polygon,
    std::vector<Point>& layout
) {
    const std::vector<Point> inner = scaled_polygon(polygon, 0.995);
    for (int iteration = 0; iteration < 600; ++iteration) {
        std::vector<Point> displacement(layout.size());
        double worst = 0.0;
        for (std::size_t first = 0; first < layout.size(); ++first) {
            for (std::size_t second = first + 1; second < layout.size(); ++second) {
                double dx = layout[second].x - layout[first].x;
                double dy = layout[second].y - layout[first].y;
                double distance = std::hypot(dx, dy);
                if (distance >= kMinimumSpacing) {
                    continue;
                }
                if (distance < 1.0e-12) {
                    const double angle = 2.0 * kPi
                        * static_cast<double>(first + 31 * second)
                        / static_cast<double>(layout.size() * layout.size());
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    distance = 1.0;
                }
                const double deficit = kMinimumSpacing - distance;
                worst = std::max(worst, deficit);
                const double push = 0.51 * deficit / distance;
                displacement[first].x -= push * dx;
                displacement[first].y -= push * dy;
                displacement[second].x += push * dx;
                displacement[second].y += push * dy;
            }
        }
        for (std::size_t index = 0; index < layout.size(); ++index) {
            layout[index] = project_inside(
                inner,
                {
                    layout[index].x + displacement[index].x,
                    layout[index].y + displacement[index].y,
                }
            );
        }
        if (worst <= 1.0e-8) {
            return;
        }
    }
}

std::vector<Point> make_boundary(const Case& paper_case) {
    const double side = 9.0 * kRotorDiameter
        * paper_case.average_spacing_diameters;
    const double target_area = side * side;
    if (paper_case.boundary == Boundary::circle) {
        const double radius = std::sqrt(target_area / kPi);
        std::vector<Point> result;
        result.reserve(100);
        for (int index = 0; index < 100; ++index) {
            const double theta = 2.0 * kPi * static_cast<double>(index) / 100.0;
            result.push_back({radius * std::cos(theta), radius * std::sin(theta)});
        }
        return result;
    }
    if (paper_case.boundary == Boundary::square) {
        const double half = side / 2.0;
        const double angle = 30.0 * kPi / 180.0;
        std::vector<Point> result;
        for (const Point point : std::array<Point, 4>{{
                 {-half, -half}, {half, -half}, {half, half}, {-half, half}
             }}) {
            result.push_back({
                std::cos(angle) * point.x - std::sin(angle) * point.y,
                std::sin(angle) * point.x + std::cos(angle) * point.y,
            });
        }
        return result;
    }
    std::vector<Point> points;
    points.reserve(data::kAmaliaCoordinates.size() / 2);
    for (std::size_t index = 0; index < data::kAmaliaCoordinates.size(); index += 2) {
        points.push_back({
            data::kAmaliaCoordinates[index],
            data::kAmaliaCoordinates[index + 1],
        });
    }
    std::vector<Point> hull = convex_hull(std::move(points));
    const Point center = centroid(hull);
    for (Point& point : hull) {
        point.x -= center.x;
        point.y -= center.y;
    }
    const double scale = std::sqrt(target_area / polygon_area(hull));
    for (Point& point : hull) {
        point.x *= scale;
        point.y *= scale;
    }
    return hull;
}

template <std::size_t Size>
std::span<const double> span(const std::array<double, Size>& values) {
    return {values.data(), values.size()};
}

std::tuple<
    std::span<const double>,
    std::span<const double>,
    std::span<const double>
> resource(WindRose rose, bool fine) {
    if (rose == WindRose::north_island) {
        if (fine) {
            return {
                span(data::kNorthIslandDirections360),
                span(data::kNorthIslandFrequencies360),
                span(data::kNorthIslandMeanSpeeds360),
            };
        }
        return {
            span(data::kNorthIslandDirections24),
            span(data::kNorthIslandFrequencies24),
            span(data::kNorthIslandMeanSpeeds24),
        };
    }
    if (rose == WindRose::ukiah) {
        if (fine) {
            return {
                span(data::kUkiahDirections360),
                span(data::kUkiahFrequencies360),
                span(data::kUkiahMeanSpeeds360),
            };
        }
        return {
            span(data::kUkiahDirections24),
            span(data::kUkiahFrequencies24),
            span(data::kUkiahMeanSpeeds24),
        };
    }
    if (fine) {
        return {
            span(data::kVictorvilleDirections360),
            span(data::kVictorvilleFrequencies360),
            span(data::kVictorvilleMeanSpeeds360),
        };
    }
    return {
        span(data::kVictorvilleDirections24),
        span(data::kVictorvilleFrequencies24),
        span(data::kVictorvilleMeanSpeeds24),
    };
}

double weibull_cdf(double speed, double mean) {
    if (speed <= 0.0 || mean <= 1.0e-12) {
        return 0.0;
    }
    const double scale = mean / std::tgamma(1.5);
    return 1.0 - std::exp(-std::pow(speed / scale, 2.0));
}

std::vector<WindState> wind_states(WindRose rose, bool fine) {
    const auto [directions, frequencies, means] = resource(rose, fine);
    const int speed_bins = fine ? 50 : 5;
    const double width = 25.0 / static_cast<double>(speed_bins);
    std::vector<WindState> result;
    result.reserve(directions.size() * static_cast<std::size_t>(speed_bins));
    for (std::size_t direction = 0; direction < directions.size(); ++direction) {
        for (int bin = 0; bin < speed_bins; ++bin) {
            const double lower = width * static_cast<double>(bin);
            const double upper = lower + width;
            result.push_back({
                directions[direction],
                0.5 * (lower + upper) + (bin == 0 ? 0.001 : 0.0),
                frequencies[direction]
                    * (weibull_cdf(upper, means[direction])
                       - weibull_cdf(lower, means[direction])),
            });
        }
    }
    return result;
}

double ct_at(double speed) {
    static constexpr std::array<double, 51> speeds = {
        0.000001, 0.1, 0.60816327, 1.11632653, 1.6244898, 2.13265306,
        2.64081633, 3.14897959, 3.65714286, 4.16530612, 4.67346939,
        5.18163265, 5.68979592, 6.19795918, 6.70612245, 7.21428571,
        7.72244898, 8.23061224, 8.73877551, 9.24693878, 9.75510204,
        10.26326531, 10.77142857, 11.27959184, 11.7877551, 12.29591837,
        12.80408163, 13.3122449, 13.82040816, 14.32857143, 14.83673469,
        15.34489796, 15.85306122, 16.36122449, 16.86938776, 17.37755102,
        17.88571429, 18.39387755, 18.90204082, 19.41020408, 19.91836735,
        20.42653061, 20.93469388, 21.44285714, 21.95102041, 22.45918367,
        22.96734694, 23.4755102, 23.98367347, 24.49183673, 25.0,
    };
    static constexpr std::array<double, 51> values = {
        0.74988552, 0.74988552, 0.74988552, 0.74988552, 0.74988552,
        0.74988552, 0.74988552, 0.74945275, 0.74736838, 0.74578062,
        0.74452166, 0.7432327, 0.74240891, 0.74171844, 0.74113119,
        0.74062551, 0.7401854, 0.7397988, 0.73945643, 0.73915104,
        0.71535516, 0.50902345, 0.42264255, 0.36002829, 0.31616439,
        0.27728908, 0.2449473, 0.2179915, 0.19464155, 0.17388996,
        0.15676952, 0.14116089, 0.12769325, 0.11564223, 0.104593,
        0.09546578, 0.08765315, 0.08043937, 0.07409357, 0.06822311,
        0.06322334, 0.05887784, 0.05481244, 0.05114998, 0.0474271,
        0.04415572, 0.04104199, 0.0383636, 0.03582949, 0.03401271,
        0.03235028,
    };
    if (speed <= speeds.front()) {
        return values.front();
    }
    if (speed >= speeds.back()) {
        return values.back();
    }
    const auto upper = std::upper_bound(speeds.begin(), speeds.end(), speed);
    const std::size_t right = static_cast<std::size_t>(upper - speeds.begin());
    const std::size_t left = right - 1;
    const double fraction = (speed - speeds[left]) / (speeds[right] - speeds[left]);
    return values[left] + fraction * (values[right] - values[left]);
}

double turbine_power_mw(double speed) {
    if (speed < 3.0 || speed >= 25.0) {
        return 0.0;
    }
    if (speed < 10.0) {
        return 3.6 * std::pow(speed / 10.0, 3.0);
    }
    return 3.6;
}

std::vector<Point> rotor_points(int count) {
    if (count == 4) {
        return {{0.0, 0.69}, {0.0, -0.69}, {-0.69, 0.0}, {0.69, 0.0}};
    }
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(count));
    const double golden = kPi * (3.0 - std::sqrt(5.0));
    for (int index = 0; index < count; ++index) {
        const double radius = std::sqrt(
            (static_cast<double>(index) + 0.5) / static_cast<double>(count)
        );
        const double theta = golden * static_cast<double>(index);
        result.push_back({radius * std::cos(theta), radius * std::sin(theta)});
    }
    return result;
}

double state_power_mw(
    const std::vector<Point>& layout,
    const WindState& state,
    std::span<const Point> rotor
) {
    const double frame = (270.0 - state.direction_deg) * kPi / 180.0;
    std::array<double, kTurbines> along{};
    std::array<double, kTurbines> across{};
    std::array<int, kTurbines> order{};
    for (int index = 0; index < kTurbines; ++index) {
        along[index] = std::cos(frame) * layout[index].x
            + std::sin(frame) * layout[index].y;
        across[index] = -std::sin(frame) * layout[index].x
            + std::cos(frame) * layout[index].y;
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [&](int left, int right) {
        return along[left] < along[right];
    });
    std::array<double, kTurbines> velocities{};
    std::array<double, kTurbines> thrust{};
    const double free_stream = state.speed
        * std::pow(kHubHeight / 50.0, 0.1);
    for (int raw_downstream = 0; raw_downstream < kTurbines; ++raw_downstream) {
        const int downstream = order[raw_downstream];
        double rotor_velocity = 0.0;
        for (const Point sample : rotor) {
            const double local_y = 0.5 * kRotorDiameter * sample.x;
            const double local_z = 0.5 * kRotorDiameter * sample.y;
            double deficit_speed = 0.0;
            for (int raw_upstream = 0; raw_upstream < raw_downstream; ++raw_upstream) {
                const int upstream = order[raw_upstream];
                const double downstream_distance = along[downstream] - along[upstream];
                if (downstream_distance <= 0.1) {
                    continue;
                }
                const double ct = std::clamp(
                    thrust[upstream] > 0.0 ? thrust[upstream] : ct_at(free_stream),
                    1.0e-6,
                    0.95
                );
                constexpr double turbulence = 0.11;
                const double expansion = 0.3837 * turbulence + 0.003678;
                const double x0 = kRotorDiameter
                    * (1.0 + std::sqrt(1.0 - ct))
                    / (
                        std::sqrt(2.0)
                        * (2.32 * turbulence + 0.154 * (1.0 - std::sqrt(1.0 - ct)))
                    );
                const double a = 2.0 * expansion;
                const double b = 4.0 * expansion * expansion * (ct - 1.0);
                const double c = 2.0 * std::sqrt(8.0) * expansion * expansion;
                const double discontinuity = x0
                    + kRotorDiameter * (a - std::sqrt(std::max(0.0, a * a - b))) / c;
                const double model_x = std::max(downstream_distance, discontinuity);
                const double sigma = kRotorDiameter * (
                    expansion * (model_x - x0) / kRotorDiameter
                    + 1.0 / std::sqrt(8.0)
                );
                const double argument = std::clamp(
                    ct * kRotorDiameter * kRotorDiameter / (8.0 * sigma * sigma),
                    0.0,
                    1.0
                );
                const double magnitude = 1.0 - std::sqrt(1.0 - argument);
                const double dy = across[downstream] + local_y - across[upstream];
                const double deficit = magnitude
                    * std::exp(-0.5 * (dy * dy + local_z * local_z) / (sigma * sigma));
                deficit_speed += velocities[upstream] * deficit;
            }
            const double point_velocity = std::max(0.0, free_stream - deficit_speed)
                * std::pow((kHubHeight + local_z) / kHubHeight, 0.1);
            rotor_velocity += point_velocity;
        }
        velocities[downstream] = rotor_velocity / static_cast<double>(rotor.size());
        thrust[downstream] = ct_at(velocities[downstream]);
    }
    double result = 0.0;
    for (double velocity : velocities) {
        result += turbine_power_mw(velocity);
    }
    return result;
}

double evaluate_states_serial(
    const std::vector<Point>& layout,
    WindRose rose,
    bool fine
) {
    const std::vector<WindState> states = wind_states(rose, fine);
    const std::vector<Point> rotor = rotor_points(fine ? 100 : 4);
    double weighted_power = 0.0;
    for (const WindState& state : states) {
        weighted_power += state.frequency * state_power_mw(layout, state, rotor);
    }
    return 8760.0 * weighted_power * kGeneratorEfficiency / 1000.0;
}

double evaluate_states_parallel(
    const std::vector<Point>& layout,
    WindRose rose,
    bool fine,
    fode::PersistentExecutor& executor
) {
    const std::vector<WindState> states = wind_states(rose, fine);
    const std::vector<Point> rotor = rotor_points(fine ? 100 : 4);
    std::vector<double> contributions(states.size(), 0.0);
    executor.parallel_for(0, static_cast<int>(states.size()), [&](int raw) {
        const std::size_t index = static_cast<std::size_t>(raw);
        contributions[index] = states[index].frequency
            * state_power_mw(layout, states[index], rotor);
    });
    return 8760.0
        * std::accumulate(contributions.begin(), contributions.end(), 0.0)
        * kGeneratorEfficiency / 1000.0;
}

std::vector<Point> boundary_points(
    const std::vector<Point>& boundary,
    int count,
    double start
) {
    std::vector<double> lengths(boundary.size(), 0.0);
    double perimeter = 0.0;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        lengths[index] = std::hypot(
            boundary[(index + 1) % boundary.size()].x - boundary[index].x,
            boundary[(index + 1) % boundary.size()].y - boundary[index].y
        );
        perimeter += lengths[index];
    }
    start = std::fmod(start, perimeter);
    if (start < 0.0) {
        start += perimeter;
    }
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(count));
    for (int turbine = 0; turbine < count; ++turbine) {
        double distance = std::fmod(
            start + perimeter * static_cast<double>(turbine) / static_cast<double>(count),
            perimeter
        );
        for (std::size_t edge = 0; edge < boundary.size(); ++edge) {
            if (distance <= lengths[edge] || edge + 1 == boundary.size()) {
                const double fraction = std::min(1.0, distance / lengths[edge]);
                const Point& left = boundary[edge];
                const Point& right = boundary[(edge + 1) % boundary.size()];
                result.push_back({
                    left.x + fraction * (right.x - left.x),
                    left.y + fraction * (right.y - left.y),
                });
                break;
            }
            distance -= lengths[edge];
        }
    }
    return result;
}

std::vector<Point> grid_from_topology(
    const GridTopology& topology,
    std::span<const double> values
) {
    const double dx = values[0];
    const double dy = values[1];
    const double shear = values[2];
    const double rotation = values[3] * kPi / 180.0;
    std::vector<Point> result;
    result.reserve(topology.indices.size());
    for (const auto [row, column] : topology.indices) {
        result.push_back({
            dx * static_cast<double>(column) + shear * static_cast<double>(row),
            dy * static_cast<double>(row),
        });
    }
    const Point center = centroid(result);
    for (Point& point : result) {
        const double x = point.x - center.x;
        const double y = point.y - center.y;
        point = {
            std::cos(rotation) * x - std::sin(rotation) * y,
            std::sin(rotation) * x + std::cos(rotation) * y,
        };
    }
    return result;
}

GridTopology make_topology(
    const std::vector<Point>& boundary,
    int count,
    double rotation,
    double ratio
) {
    const std::vector<Point> inner = scaled_polygon(boundary, ratio == 4.0 ? 0.85 : 0.95);
    const double angle = rotation * kPi / 180.0;
    auto count_inside = [&](double spacing, bool retain) {
        std::vector<std::pair<int, int>> indices;
        for (int row = -12; row <= 12; ++row) {
            for (int column = -12; column <= 12; ++column) {
                const double x = spacing * static_cast<double>(column)
                    + spacing * ratio * std::tan(20.0 * kPi / 180.0)
                        * static_cast<double>(row);
                const double y = spacing * ratio * static_cast<double>(row);
                const Point point{
                    std::cos(angle) * x - std::sin(angle) * y,
                    std::sin(angle) * x + std::cos(angle) * y,
                };
                if (polygon_contains(inner, point)) {
                    indices.emplace_back(row, column);
                }
            }
        }
        if (!retain) {
            return indices;
        }
        std::sort(indices.begin(), indices.end());
        if (static_cast<int>(indices.size()) > count) {
            indices.resize(static_cast<std::size_t>(count));
        }
        return indices;
    };
    double low = 0.1 * kRotorDiameter;
    double high = 20.0 * kRotorDiameter;
    for (int iteration = 0; iteration < 70; ++iteration) {
        const double mid = 0.5 * (low + high);
        if (static_cast<int>(count_inside(mid, false).size()) >= count) {
            low = mid;
        } else {
            high = mid;
        }
    }
    GridTopology result;
    result.indices = count_inside(low * (1.0 - 1.0e-10), true);
    if (static_cast<int>(result.indices.size()) != count) {
        throw std::runtime_error("T14 could not freeze grid topology");
    }
    result.initial_dx = low;
    result.initial_dy = low * ratio;
    result.initial_shear = result.initial_dy * std::tan(20.0 * kPi / 180.0);
    result.initial_rotation = rotation;
    return result;
}

Encoding initial_encoding(
    const Problem& problem,
    Representation representation,
    std::uint64_t seed,
    double grid_ratio
) {
    const fode::CounterRng rng(seed);
    Encoding encoding;
    encoding.representation = representation;
    if (representation == Representation::direct) {
        encoding.values.resize(2 * kTurbines);
        encoding.scales.resize(2 * kTurbines);
        const double radius = std::sqrt(
            polygon_area(problem.boundary_vertices()) / kPi
        );
        const double golden = kPi * (3.0 - std::sqrt(5.0));
        for (int index = 0; index < kTurbines; ++index) {
            const double radial = std::sqrt(
                (static_cast<double>(index) + 0.5) / static_cast<double>(kTurbines)
            );
            const double angle = golden * static_cast<double>(index)
                + 2.0 * kPi * rng.uniform(0, 1, 0);
            const Point point = project_inside(
                scaled_polygon(problem.boundary_vertices(), 0.94),
                {radius * radial * std::cos(angle), radius * radial * std::sin(angle)}
            );
            encoding.values[2 * index] = point.x;
            encoding.values[2 * index + 1] = point.y;
            encoding.scales[2 * index] = 0.08 * radius;
            encoding.scales[2 * index + 1] = 0.08 * radius;
        }
        return encoding;
    }
    const double rotation = 360.0 * rng.uniform(0, 2, 0);
    if (representation == Representation::grid) {
        encoding.topology = make_topology(
            problem.boundary_vertices(),
            kTurbines,
            rotation,
            grid_ratio
        );
        encoding.values = {
            encoding.topology.initial_dx,
            encoding.topology.initial_dy,
            encoding.topology.initial_shear,
            encoding.topology.initial_rotation,
        };
        encoding.scales = {
            0.1 * encoding.values[0],
            0.1 * encoding.values[1],
            std::max(1.0, 0.1 * std::abs(encoding.values[2])),
            15.0,
        };
        return encoding;
    }
    const std::vector<Point>& boundary = problem.boundary_vertices();
    double perimeter = 0.0;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        perimeter += std::hypot(
            boundary[(index + 1) % boundary.size()].x - boundary[index].x,
            boundary[(index + 1) % boundary.size()].y - boundary[index].y
        );
    }
    encoding.outer_turbines = std::min(
        45,
        static_cast<int>(perimeter / (2.0 * std::sqrt(2.0) * kRotorDiameter))
    );
    encoding.topology = make_topology(
        boundary,
        kTurbines - encoding.outer_turbines,
        rotation,
        4.0
    );
    encoding.values = {
        encoding.topology.initial_dx,
        encoding.topology.initial_dy,
        encoding.topology.initial_shear,
        encoding.topology.initial_rotation,
        3.0 * kRotorDiameter * rng.uniform(0, 3, 0),
    };
    encoding.scales = {
        0.1 * encoding.values[0],
        0.1 * encoding.values[1],
        std::max(1.0, 0.1 * std::abs(encoding.values[2])),
        15.0,
        perimeter / static_cast<double>(encoding.outer_turbines),
    };
    return encoding;
}

std::vector<Point> decode(const Problem& problem, const Encoding& encoding) {
    if (encoding.representation == Representation::direct) {
        std::vector<Point> result;
        result.reserve(kTurbines);
        for (int index = 0; index < kTurbines; ++index) {
            result.push_back(project_inside(
                problem.boundary_vertices(),
                {
                    encoding.values[2 * index],
                    encoding.values[2 * index + 1],
                }
            ));
        }
        repair_spacing(problem.boundary_vertices(), result);
        return result;
    }
    std::vector<Point> grid = grid_from_topology(
        encoding.topology,
        std::span<const double>(encoding.values.data(), 4)
    );
    if (encoding.representation == Representation::grid) {
        return grid;
    }
    std::vector<Point> result = boundary_points(
        problem.boundary_vertices(),
        encoding.outer_turbines,
        encoding.values[4]
    );
    result.insert(result.end(), grid.begin(), grid.end());
    return result;
}

bool better(
    const Evaluation& left,
    const Evaluation& right
) {
    const bool left_feasible = left.constraint_violation_m <= 1.0e-9;
    const bool right_feasible = right.constraint_violation_m <= 1.0e-9;
    if (left_feasible != right_feasible) {
        return left_feasible;
    }
    if (!left_feasible) {
        return left.constraint_violation_m < right.constraint_violation_m;
    }
    return left.optimization_aep_gwh > right.optimization_aep_gwh;
}

std::uint64_t hash_layout(
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 1469598103934665603ULL;
    auto consume = [&](double value) {
        hash ^= std::bit_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    for (const Point& point : layout) {
        consume(point.x);
        consume(point.y);
    }
    consume(evaluation.optimization_aep_gwh);
    consume(evaluation.final_aep_gwh);
    consume(evaluation.constraint_violation_m);
    return hash;
}

}  // namespace

std::vector<Case> paper_cases() {
    return {
        {"t14_spacing4_amalia_north_island", 4.0, Boundary::amalia, WindRose::north_island},
        {"t14_spacing6_amalia_north_island", 6.0, Boundary::amalia, WindRose::north_island},
        {"t14_spacing8_amalia_north_island", 8.0, Boundary::amalia, WindRose::north_island},
        {"t14_spacing4_amalia_ukiah", 4.0, Boundary::amalia, WindRose::ukiah},
        {"t14_spacing4_amalia_victorville", 4.0, Boundary::amalia, WindRose::victorville},
        {"t14_spacing4_circle_north_island", 4.0, Boundary::circle, WindRose::north_island},
        {"t14_spacing4_square_north_island", 4.0, Boundary::square, WindRose::north_island},
    };
}

std::vector<std::string> algorithm_ids() {
    return {"t14_direct", "t14_grid", "t14_boundary_grid"};
}

Representation representation_from_id(const std::string& id) {
    if (id == "t14_direct") {
        return Representation::direct;
    }
    if (id == "t14_grid") {
        return Representation::grid;
    }
    if (id == "t14_boundary_grid") {
        return Representation::boundary_grid;
    }
    throw std::invalid_argument("unknown T14 algorithm: " + id);
}

Problem::Problem(Case paper_case)
    : paper_case_(std::move(paper_case)),
      boundary_vertices_(make_boundary(paper_case_)) {}

const Case& Problem::paper_case() const noexcept {
    return paper_case_;
}

const std::vector<Point>& Problem::boundary_vertices() const noexcept {
    return boundary_vertices_;
}

bool Problem::contains(const Point& point) const noexcept {
    return polygon_contains(boundary_vertices_, point);
}

double Problem::constraint_violation(
    const std::vector<Point>& layout
) const noexcept {
    if (layout.size() != kTurbines) {
        return std::numeric_limits<double>::infinity();
    }
    double violation = 0.0;
    const Point center = centroid(boundary_vertices_);
    for (const Point& point : layout) {
        if (!contains(point)) {
            const Point projected = project_inside(boundary_vertices_, point);
            violation += std::hypot(point.x - projected.x, point.y - projected.y);
        }
        (void) center;
    }
    for (int first = 0; first < kTurbines; ++first) {
        for (int second = first + 1; second < kTurbines; ++second) {
            violation += std::max(
                0.0,
                kMinimumSpacing - std::hypot(
                    layout[first].x - layout[second].x,
                    layout[first].y - layout[second].y
                )
            );
        }
    }
    return violation;
}

double Problem::evaluate_optimization(
    const std::vector<Point>& layout
) const {
    return evaluate_states_serial(layout, paper_case_.wind_rose, false);
}

double Problem::evaluate_final(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    return evaluate_states_parallel(layout, paper_case_.wind_rose, true, executor);
}

std::vector<Point> decode_reference_layout(
    const Problem& problem,
    Representation representation,
    std::uint64_t seed
) {
    return decode(problem, initial_encoding(problem, representation, seed, 1.0));
}

RunResult run(
    const Problem& problem,
    const std::string& algorithm_id,
    std::uint64_t seed,
    std::uint64_t physical_fes_limit,
    int workers
) {
    const Clock::time_point total_start = Clock::now();
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const Representation representation = representation_from_id(algorithm_id);
    const std::uint64_t budget = physical_fes_limit == 0
        ? (representation == Representation::direct ? 160 : 240)
        : physical_fes_limit;
    Encoding incumbent = initial_encoding(problem, representation, seed, 1.0);
    std::vector<Point> incumbent_layout = decode(problem, incumbent);
    const Clock::time_point evaluator_start = Clock::now();
    Evaluation incumbent_evaluation{
        problem.evaluate_optimization(incumbent_layout),
        0.0,
        problem.constraint_violation(incumbent_layout),
    };
    double evaluator_seconds = seconds_since(evaluator_start);
    std::uint64_t fes = 1;
    fode::CounterRng rng(seed);
    double global_scale = 1.0;
    const int batch_size = std::max(2, workers);
    std::uint64_t generation = 0;
    double algorithm_seconds = 0.0;
    while (fes < budget) {
        const Clock::time_point algorithm_start = Clock::now();
        const int count = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(batch_size),
            budget - fes
        ));
        std::vector<Encoding> trials(static_cast<std::size_t>(count), incumbent);
        for (int trial = 0; trial < count; ++trial) {
            for (std::size_t dimension = 0; dimension < incumbent.values.size(); ++dimension) {
                trials[trial].values[dimension] += global_scale
                    * incumbent.scales[dimension]
                    * rng.normal(generation, 10, static_cast<std::uint64_t>(trial), dimension);
            }
            if (representation != Representation::direct) {
                trials[trial].values[0] = std::max(0.25 * kRotorDiameter, trials[trial].values[0]);
                trials[trial].values[1] = std::max(0.25 * kRotorDiameter, trials[trial].values[1]);
                trials[trial].values[3] = std::fmod(trials[trial].values[3], 360.0);
            }
        }
        std::vector<std::vector<Point>> layouts(static_cast<std::size_t>(count));
        for (int trial = 0; trial < count; ++trial) {
            layouts[trial] = decode(problem, trials[trial]);
        }
        algorithm_seconds += seconds_since(algorithm_start);
        std::vector<Evaluation> evaluations(static_cast<std::size_t>(count));
        const Clock::time_point batch_evaluator_start = Clock::now();
        executor.parallel_for(0, count, [&](int trial) {
            evaluations[trial] = {
                problem.evaluate_optimization(layouts[trial]),
                0.0,
                problem.constraint_violation(layouts[trial]),
            };
        });
        evaluator_seconds += seconds_since(batch_evaluator_start);
        fes += static_cast<std::uint64_t>(count);
        const Clock::time_point commit_start = Clock::now();
        int best = -1;
        for (int trial = 0; trial < count; ++trial) {
            if (
                better(evaluations[trial], incumbent_evaluation)
                && (best < 0 || better(evaluations[trial], evaluations[best]))
            ) {
                best = trial;
            }
        }
        if (best >= 0) {
            incumbent = std::move(trials[best]);
            incumbent_layout = std::move(layouts[best]);
            incumbent_evaluation = evaluations[best];
            global_scale = std::min(1.5, global_scale * 1.04);
        } else {
            global_scale = std::max(0.01, global_scale * 0.92);
        }
        algorithm_seconds += seconds_since(commit_start);
        ++generation;
    }
    const Clock::time_point final_evaluator_start = Clock::now();
    incumbent_evaluation.final_aep_gwh = problem.evaluate_final(
        incumbent_layout,
        executor
    );
    evaluator_seconds += seconds_since(final_evaluator_start);
    const fode::ExecutorWorkReceipt receipt = executor.work_receipt();
    RunResult result;
    result.algorithm_id = algorithm_id;
    result.problem_id = problem.paper_case().id;
    result.best_layout = std::move(incumbent_layout);
    result.best_evaluation = incumbent_evaluation;
    result.seed = seed;
    result.physical_fes = fes;
    result.physical_fes_limit = budget;
    result.requested_workers = workers;
    result.observed_workers = receipt.distinct_participants;
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds = algorithm_seconds;
    result.end_to_end_seconds = seconds_since(total_start);
    result.scientific_hash = hash_layout(result.best_layout, result.best_evaluation);
    return result;
}

}  // namespace core99::t14
