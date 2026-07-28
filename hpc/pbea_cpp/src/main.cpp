#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Layout = std::vector<int>;
using Objectives = std::array<double, 3>;

struct WindScenario {
    std::string id;
    std::vector<double> theta;
    std::vector<double> velocity;
    std::vector<double> probability;
};

struct Problem {
    int rows = 20;
    int cols = 20;
    int turbines = 15;
    double cell_width_m = 231.0;
    double hub_height_m = 80.0;
    double rotor_diameter_m = 77.0;
    double roughness_m = 0.00025;
    double land_cost_factor = 0.1;
    WindScenario wind;
};

struct Timings {
    double evaluator_seconds = 0.0;
    double algorithm_seconds = 0.0;
};

struct Config {
    std::string mode = "run";
    std::string scenario = "ws1";
    std::string layout_text;
    int turbines = 15;
    int population = 100;
    int generations = 100;
    int workers = 1;
    int ipd = 3;
    double mu_c = 80.0;
    std::uint64_t seed = 1;
};

constexpr std::array<double, 6> kVelocity{
    4.0, 6.333333333333334, 8.666666666666668,
    11.0, 13.333333333333334, 15.666666666666666
};
constexpr std::array<double, 2> kThetaWs1{
    4.88692190558412, 6.10865238198015
};
constexpr std::array<double, 12> kProbWs1{
    0.06122, 0.10330, 0.09673, 0.08689, 0.07906, 0.07231,
    0.06258, 0.10148, 0.09601, 0.09014, 0.07819, 0.07209
};
constexpr std::array<double, 12> kThetaWs2{
    0.0, 0.17453292519943295, 0.6981317007977318,
    0.8726646259971648, 1.3962634015954636, 2.96705972839036,
    3.141592653589793, 4.014257279586958, 4.363323129985823,
    4.71238898038469, 4.886921905584122, 5.585053606381854
};
constexpr std::array<double, 72> kProbWs2{
    0.017000,0.009410,0.004710,0.002850,0.007610,0.017550,
    0.027670,0.016160,0.008660,0.005580,0.013270,0.030250,
    0.024750,0.014960,0.007490,0.005330,0.011890,0.028410,
    0.023000,0.013840,0.006730,0.004520,0.012000,0.025880,
    0.020680,0.012480,0.006070,0.004150,0.010530,0.023210,
    0.019100,0.011250,0.005570,0.003810,0.009530,0.020710,
    0.001560,0.007180,0.014850,0.011620,0.013550,0.016320,
    0.002760,0.011250,0.024420,0.019130,0.022060,0.026990,
    0.002320,0.009940,0.023320,0.018070,0.019500,0.024330,
    0.002130,0.009370,0.021010,0.016660,0.018380,0.022750,
    0.002130,0.008290,0.018030,0.014910,0.016290,0.021630,
    0.001900,0.007090,0.017000,0.012880,0.014940,0.018890
};

WindScenario make_wind(const std::string& id) {
    WindScenario result;
    result.id = id;
    result.velocity.assign(kVelocity.begin(), kVelocity.end());
    if (id == "ws1") {
        result.theta.assign(kThetaWs1.begin(), kThetaWs1.end());
        result.probability.assign(kProbWs1.begin(), kProbWs1.end());
    } else if (id == "ws2") {
        result.theta.assign(kThetaWs2.begin(), kThetaWs2.end());
        result.probability.assign(kProbWs2.begin(), kProbWs2.end());
    } else {
        throw std::invalid_argument("scenario must be ws1 or ws2");
    }
    return result;
}

double turbine_power(double speed) {
    if (speed < 2.0) {
        return 0.0;
    }
    if (speed < 12.8) {
        return 0.3 * speed * speed * speed;
    }
    if (speed < 18.0) {
        return 629.1;
    }
    return 0.0;
}

double overlap_area(double offset, double rotor_radius, double wake_radius) {
    const double distance = std::abs(offset);
    if (distance >= rotor_radius + wake_radius) {
        return 0.0;
    }
    if (distance <= std::abs(wake_radius - rotor_radius)) {
        const double contained = std::min(rotor_radius, wake_radius);
        return std::numbers::pi * contained * contained;
    }
    const double first = std::clamp(
        (wake_radius * wake_radius + distance * distance
         - rotor_radius * rotor_radius)
        / (2.0 * wake_radius * distance), -1.0, 1.0
    );
    const double second = std::clamp(
        (rotor_radius * rotor_radius + distance * distance
         - wake_radius * wake_radius)
        / (2.0 * rotor_radius * distance), -1.0, 1.0
    );
    const double alpha = std::acos(first);
    const double beta = std::acos(second);
    const double triangle = 0.5 * std::sqrt(std::max(
        0.0,
        (-distance + wake_radius + rotor_radius)
        * (distance + wake_radius - rotor_radius)
        * (distance - wake_radius + rotor_radius)
        * (distance + wake_radius + rotor_radius)
    ));
    return wake_radius * wake_radius * alpha
        + rotor_radius * rotor_radius * beta - triangle;
}

std::vector<std::pair<double, double>> grid_points(
    const Problem& problem, const Layout& layout
) {
    std::vector<std::pair<double, double>> points;
    points.reserve(layout.size());
    for (const int index : layout) {
        if (index < 1 || index > problem.rows * problem.cols) {
            throw std::invalid_argument("layout index outside 1..400");
        }
        const int zero = index - 1;
        const int row = zero / problem.cols;
        const int column = zero % problem.cols;
        points.emplace_back(
            static_cast<double>(column) * problem.cell_width_m
                + 0.5 * problem.cell_width_m,
            static_cast<double>(row) * problem.cell_width_m
                + 0.5 * problem.cell_width_m
        );
    }
    return points;
}

double expected_power(const Problem& problem, const Layout& layout) {
    const auto points = grid_points(problem, layout);
    const int count = static_cast<int>(points.size());
    const double radius = 0.5 * problem.rotor_diameter_m;
    const double entrainment =
        0.5 / std::log(problem.hub_height_m / problem.roughness_m);
    double total = 0.0;
    std::vector<double> rotated_x(static_cast<std::size_t>(count));
    std::vector<double> rotated_y(static_cast<std::size_t>(count));
    std::vector<int> order(static_cast<std::size_t>(count));
    std::vector<double> deficits(static_cast<std::size_t>(count));
    std::iota(order.begin(), order.end(), 0);
    for (std::size_t direction = 0;
         direction < problem.wind.theta.size(); ++direction) {
        const double cosine = std::cos(problem.wind.theta[direction]);
        const double sine = std::sin(problem.wind.theta[direction]);
        for (int index = 0; index < count; ++index) {
            const auto [x, y] = points[static_cast<std::size_t>(index)];
            rotated_x[static_cast<std::size_t>(index)] =
                cosine * x - sine * y;
            rotated_y[static_cast<std::size_t>(index)] =
                sine * x + cosine * y;
        }
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            return rotated_y[static_cast<std::size_t>(left)]
                 > rotated_y[static_cast<std::size_t>(right)];
        });
        std::fill(deficits.begin(), deficits.end(), 0.0);
        for (int downstream_rank = 1;
             downstream_rank < count; ++downstream_rank) {
            const int downstream =
                order[static_cast<std::size_t>(downstream_rank)];
            double squared = 0.0;
            for (int upstream_rank = 0;
                 upstream_rank < downstream_rank; ++upstream_rank) {
                const int upstream =
                    order[static_cast<std::size_t>(upstream_rank)];
                const double along =
                    rotated_y[static_cast<std::size_t>(upstream)]
                    - rotated_y[static_cast<std::size_t>(downstream)];
                if (along <= 0.0) {
                    continue;
                }
                const double lateral = std::abs(
                    rotated_x[static_cast<std::size_t>(downstream)]
                    - rotated_x[static_cast<std::size_t>(upstream)]
                );
                const double wake_radius = radius + entrainment * along;
                const double overlap =
                    overlap_area(lateral, radius, wake_radius);
                const double deficit =
                    (2.0 / 3.0) * radius * radius
                    / (wake_radius * wake_radius)
                    * overlap / (std::numbers::pi * radius * radius);
                squared += deficit * deficit;
            }
            deficits[static_cast<std::size_t>(downstream)] =
                std::sqrt(squared);
        }
        for (std::size_t speed = 0;
             speed < problem.wind.velocity.size(); ++speed) {
            double scenario_power = 0.0;
            for (const double deficit : deficits) {
                scenario_power += turbine_power(
                    (1.0 - deficit) * problem.wind.velocity[speed]
                );
            }
            total += scenario_power
                * problem.wind.probability[
                    direction * problem.wind.velocity.size() + speed
                ];
        }
    }
    return total;
}

double cross(
    const std::pair<int, int>& origin,
    const std::pair<int, int>& a,
    const std::pair<int, int>& b
) {
    return static_cast<double>(a.first - origin.first)
            * static_cast<double>(b.second - origin.second)
        - static_cast<double>(a.second - origin.second)
            * static_cast<double>(b.first - origin.first);
}

double land_area_grid_units(const Problem& problem, const Layout& layout) {
    std::vector<std::pair<int, int>> points;
    points.reserve(layout.size());
    for (const int index : layout) {
        const int zero = index - 1;
        points.emplace_back(zero / problem.cols + 1, zero % problem.cols + 1);
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) {
        throw std::invalid_argument("layout needs at least three unique cells");
    }
    std::vector<std::pair<int, int>> hull(2 * points.size());
    std::size_t size = 0;
    for (const auto& point : points) {
        while (size >= 2 && cross(hull[size - 2], hull[size - 1], point) <= 0) {
            --size;
        }
        hull[size++] = point;
    }
    const std::size_t lower = size;
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (size > lower && cross(hull[size - 2], hull[size - 1], *it) <= 0) {
            --size;
        }
        hull[size++] = *it;
    }
    if (size > 1) {
        --size;
    }
    double twice_area = 0.0;
    for (std::size_t index = 0; index < size; ++index) {
        const auto& current = hull[index];
        const auto& next = hull[(index + 1) % size];
        twice_area += static_cast<double>(current.first * next.second)
                    - static_cast<double>(next.first * current.second);
    }
    const double hull_area = 0.5 * std::abs(twice_area);
    const auto [min_x, max_x] = std::minmax_element(
        points.begin(), points.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        }
    );
    const auto [min_y, max_y] = std::minmax_element(
        points.begin(), points.end(),
        [](const auto& left, const auto& right) {
            return left.second < right.second;
        }
    );
    const double width = static_cast<double>(max_x->first - min_x->first + 1);
    const double height = static_cast<double>(max_y->second - min_y->second + 1);
    return 0.5 * (hull_area + width * height);
}

Objectives evaluate(const Problem& problem, const Layout& layout) {
    if (static_cast<int>(layout.size()) != problem.turbines) {
        throw std::invalid_argument("layout cardinality differs from turbines");
    }
    Layout unique = layout;
    std::sort(unique.begin(), unique.end());
    if (std::adjacent_find(unique.begin(), unique.end()) != unique.end()) {
        throw std::invalid_argument("layout contains duplicate cells");
    }
    const double power = expected_power(problem, layout);
    const double area = land_area_grid_units(problem, layout);
    const double construction =
        static_cast<double>(problem.turbines)
        * (2.0 / 3.0 + (1.0 / 3.0)
        * std::exp(-0.00174
            * static_cast<double>(problem.turbines * problem.turbines)));
    return {1.0 / power, area, construction + problem.land_cost_factor * area};
}

double weighted_tchebycheff(
    const Objectives& value,
    const Objectives& ideal,
    const Objectives& weight
) {
    double result = 0.0;
    for (int objective = 0; objective < 3; ++objective) {
        result = std::max(
            result,
            std::max(weight[static_cast<std::size_t>(objective)], 1e-6)
            * std::abs(value[static_cast<std::size_t>(objective)]
                - ideal[static_cast<std::size_t>(objective)])
        );
    }
    return result;
}

std::vector<Objectives> make_weights(
    int population,
    const fode::CounterRng& rng
) {
    std::vector<Objectives> result;
    constexpr int resolution = 98;
    for (int first = 0; first <= resolution; ++first) {
        for (int second = 0; second <= resolution - first; ++second) {
            const int third = resolution - first - second;
            result.push_back({
                static_cast<double>(first) / resolution,
                static_cast<double>(second) / resolution,
                static_cast<double>(third) / resolution
            });
        }
    }
    if (result.size() < static_cast<std::size_t>(population)) {
        throw std::invalid_argument(
            "population exceeds resolution-98 weight lattice"
        );
    }
    for (std::size_t index = result.size(); index > 1; --index) {
        const int selected = rng.integer(
            0, static_cast<int>(index), 0, 5, 0,
            static_cast<std::uint64_t>(index)
        );
        std::swap(
            result[index - 1],
            result[static_cast<std::size_t>(selected)]
        );
    }
    result.resize(static_cast<std::size_t>(population));
    return result;
}

std::pair<double, double> ipd_center(int ipd) {
    constexpr std::array<std::pair<double, double>, 6> centers{{
        {1.0, 1.0}, {1.0, 20.0}, {20.0, 1.0},
        {20.0, 20.0}, {10.5, 10.5}, {1.0, 10.5}
    }};
    if (ipd < 1 || ipd > 6) {
        throw std::invalid_argument("ipd must be 1..6");
    }
    return centers[static_cast<std::size_t>(ipd - 1)];
}

std::vector<double> initial_probability(int ipd) {
    const auto [center_row, center_col] = ipd_center(ipd);
    std::vector<double> probability(400);
    double sum = 0.0;
    for (int cell = 0; cell < 400; ++cell) {
        const double row = static_cast<double>(cell / 20 + 1);
        const double col = static_cast<double>(cell % 20 + 1);
        const double exponent =
            -0.5 * ((row - center_row) * (row - center_row)
                  + (col - center_col) * (col - center_col)) / 25.0;
        probability[static_cast<std::size_t>(cell)] = std::exp(exponent);
        sum += std::exp(exponent);
    }
    for (double& value : probability) {
        value /= sum;
    }
    return probability;
}

int sample_available(
    const std::vector<double>& probability,
    const std::vector<bool>& used,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t phase,
    std::uint64_t individual,
    std::uint64_t coordinate,
    std::uint64_t draw
) {
    double sum = 0.0;
    for (std::size_t cell = 0; cell < probability.size(); ++cell) {
        if (!used[cell]) {
            sum += std::max(0.0, probability[cell]);
        }
    }
    if (!(sum > 0.0)) {
        for (std::size_t cell = 0; cell < used.size(); ++cell) {
            if (!used[cell]) {
                return static_cast<int>(cell) + 1;
            }
        }
        throw std::runtime_error("no unused grid cell");
    }
    double target = rng.uniform(
        generation, phase, individual, coordinate, draw
    ) * sum;
    for (std::size_t cell = 0; cell < probability.size(); ++cell) {
        if (!used[cell]) {
            target -= std::max(0.0, probability[cell]);
            if (target <= 0.0) {
                return static_cast<int>(cell) + 1;
            }
        }
    }
    for (std::size_t cell = used.size(); cell-- > 0;) {
        if (!used[cell]) {
            return static_cast<int>(cell) + 1;
        }
    }
    throw std::runtime_error("sampling failed");
}

Layout initialize_layout(
    const Problem& problem,
    const std::vector<double>& probability,
    const fode::CounterRng& rng,
    int individual
) {
    Layout layout(static_cast<std::size_t>(problem.turbines));
    std::vector<bool> used(400, false);
    for (int coordinate = 0; coordinate < problem.turbines; ++coordinate) {
        const int cell = sample_available(
            probability, used, rng, 0, 10,
            static_cast<std::uint64_t>(individual),
            static_cast<std::uint64_t>(coordinate), 0
        );
        layout[static_cast<std::size_t>(coordinate)] = cell;
        used[static_cast<std::size_t>(cell - 1)] = true;
    }
    std::sort(layout.begin(), layout.end());
    return layout;
}

void repair_layout(
    Layout& layout,
    const std::vector<double>& probability,
    const fode::CounterRng& rng,
    int generation,
    int individual
) {
    std::vector<bool> used(400, false);
    for (int coordinate = 0;
         coordinate < static_cast<int>(layout.size()); ++coordinate) {
        int& cell = layout[static_cast<std::size_t>(coordinate)];
        cell = std::clamp(cell, 1, 400);
        if (used[static_cast<std::size_t>(cell - 1)]) {
            cell = sample_available(
                probability, used, rng,
                static_cast<std::uint64_t>(generation), 31,
                static_cast<std::uint64_t>(individual),
                static_cast<std::uint64_t>(coordinate), 0
            );
        }
        used[static_cast<std::size_t>(cell - 1)] = true;
    }
    std::sort(layout.begin(), layout.end());
}

Layout make_offspring(
    const Layout& base,
    const Layout& donor_a,
    const Layout& donor_b,
    const std::vector<double>& probability,
    const fode::CounterRng& rng,
    int generation,
    int individual
) {
    Layout child = base;
    const int forced = rng.integer(
        0, static_cast<int>(base.size()),
        static_cast<std::uint64_t>(generation), 20,
        static_cast<std::uint64_t>(individual)
    );
    for (int coordinate = 0;
         coordinate < static_cast<int>(base.size()); ++coordinate) {
        const bool crossover = coordinate == forced
            || rng.uniform(
                static_cast<std::uint64_t>(generation), 21,
                static_cast<std::uint64_t>(individual),
                static_cast<std::uint64_t>(coordinate)
            ) <= 0.9;
        if (crossover) {
            child[static_cast<std::size_t>(coordinate)] = static_cast<int>(
                std::llround(
                    static_cast<double>(base[static_cast<std::size_t>(coordinate)])
                    + 0.5 * static_cast<double>(
                        donor_a[static_cast<std::size_t>(coordinate)]
                        - donor_b[static_cast<std::size_t>(coordinate)]
                    )
                )
            );
        }
        if (rng.uniform(
            static_cast<std::uint64_t>(generation), 22,
            static_cast<std::uint64_t>(individual),
            static_cast<std::uint64_t>(coordinate)
        ) < 1.0 / static_cast<double>(base.size())) {
            child[static_cast<std::size_t>(coordinate)] +=
                rng.integer(
                    -20, 21,
                    static_cast<std::uint64_t>(generation), 23,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(coordinate)
                );
        }
    }
    repair_layout(child, probability, rng, generation, individual);
    return child;
}

void apply_probability_guidance(
    Layout& layout,
    const std::vector<double>& probability,
    const fode::CounterRng& rng,
    int generation,
    int individual
) {
    const int replace_count = static_cast<int>(layout.size()) / 2;
    std::stable_sort(layout.begin(), layout.end(), [&](int left, int right) {
        return probability[static_cast<std::size_t>(left - 1)]
             > probability[static_cast<std::size_t>(right - 1)];
    });
    std::vector<bool> used(400, false);
    const int keep_count = static_cast<int>(layout.size()) - replace_count;
    for (int index = 0; index < keep_count; ++index) {
        used[static_cast<std::size_t>(
            layout[static_cast<std::size_t>(index)] - 1
        )] = true;
    }
    std::vector<double> diffused = probability;
    const double delta1 = 0.01 * static_cast<double>(generation);
    const double delta2 = 0.02 * static_cast<double>(generation);
    const double delta3 = 0.03 * static_cast<double>(generation);
    for (int kept = 0; kept < keep_count; ++kept) {
        const int source = layout[static_cast<std::size_t>(kept)] - 1;
        const int source_row = source / 20;
        const int source_col = source % 20;
        for (int cell = 0; cell < 400; ++cell) {
            const int row = cell / 20;
            const int col = cell % 20;
            const double distance = std::hypot(
                static_cast<double>(row - source_row),
                static_cast<double>(col - source_col)
            );
            if (distance > 0.0 && distance <= 1.0) {
                diffused[static_cast<std::size_t>(cell)] += delta1;
            } else if (distance <= 3.0) {
                diffused[static_cast<std::size_t>(cell)] += delta2;
            } else if (distance <= 6.0) {
                diffused[static_cast<std::size_t>(cell)] += delta3;
            }
        }
    }
    for (int index = keep_count;
         index < static_cast<int>(layout.size()); ++index) {
        const int cell = sample_available(
            diffused, used, rng,
            static_cast<std::uint64_t>(generation), 40,
            static_cast<std::uint64_t>(individual),
            static_cast<std::uint64_t>(index), 0
        );
        layout[static_cast<std::size_t>(index)] = cell;
        used[static_cast<std::size_t>(cell - 1)] = true;
    }
    std::sort(layout.begin(), layout.end());
}

void compact_probability(
    std::vector<double>& probability,
    const Layout& layout,
    int generation
) {
    const double at_cell = 0.30 * static_cast<double>(generation);
    const double inner = 0.20 * static_cast<double>(generation);
    const double middle = 0.10 * static_cast<double>(generation);
    for (const int turbine : layout) {
        const int source = turbine - 1;
        const int source_row = source / 20;
        const int source_col = source % 20;
        for (int cell = 0; cell < 400; ++cell) {
            const int row = cell / 20;
            const int col = cell % 20;
            const double distance = std::hypot(
                static_cast<double>(row - source_row),
                static_cast<double>(col - source_col)
            );
            if (distance == 0.0) {
                probability[static_cast<std::size_t>(cell)] += at_cell;
            } else if (distance <= 1.0) {
                probability[static_cast<std::size_t>(cell)] += inner;
            } else if (distance <= 3.0) {
                probability[static_cast<std::size_t>(cell)] += middle;
            }
        }
    }
}

std::vector<std::vector<int>> neighborhoods(
    const std::vector<Objectives>& weights, int count
) {
    std::vector<std::vector<int>> result(weights.size());
    for (std::size_t index = 0; index < weights.size(); ++index) {
        std::vector<std::pair<double, int>> distance;
        distance.reserve(weights.size());
        for (std::size_t other = 0; other < weights.size(); ++other) {
            double squared = 0.0;
            for (int objective = 0; objective < 3; ++objective) {
                const double delta =
                    weights[index][static_cast<std::size_t>(objective)]
                    - weights[other][static_cast<std::size_t>(objective)];
                squared += delta * delta;
            }
            distance.emplace_back(squared, static_cast<int>(other));
        }
        std::sort(distance.begin(), distance.end());
        for (int neighbor = 0; neighbor < count; ++neighbor) {
            result[index].push_back(
                distance[static_cast<std::size_t>(neighbor)].second
            );
        }
    }
    return result;
}

void print_objectives_json(
    const Problem& problem,
    const Layout& layout,
    const Objectives& value,
    double seconds
) {
    std::cout << std::setprecision(17)
              << "{\"mode\":\"evaluate\",\"profile\":\"paper_repaired\","
              << "\"scenario\":\"" << problem.wind.id << "\","
              << "\"turbines\":" << layout.size() << ","
              << "\"complete_layout_evaluations\":1,"
              << "\"inverse_power\":" << value[0] << ","
              << "\"land_area_grid_units\":" << value[1] << ","
              << "\"total_cost\":" << value[2] << ","
              << "\"elapsed_seconds\":" << seconds << "}\n";
}

void run_optimizer(const Config& config) {
    Problem problem;
    problem.turbines = config.turbines;
    problem.wind = make_wind(config.scenario);
    const fode::CounterRng rng(config.seed);
    const auto weights = make_weights(config.population, rng);
    if (weights.size() != static_cast<std::size_t>(config.population)) {
        throw std::runtime_error("weight construction cardinality mismatch");
    }
    const auto neighbors = neighborhoods(
        weights, std::max(2, config.population / 10)
    );
    fode::PersistentExecutor executor(config.workers);
    std::vector<double> probability = initial_probability(config.ipd);
    std::vector<Layout> population(
        static_cast<std::size_t>(config.population)
    );
    std::vector<Objectives> values(
        static_cast<std::size_t>(config.population)
    );
    executor.parallel_for(0, config.population, [&](int index) {
        population[static_cast<std::size_t>(index)] =
            initialize_layout(problem, probability, rng, index);
    });
    Timings timing;
    auto start = Clock::now();
    executor.parallel_for(0, config.population, [&](int index) {
        values[static_cast<std::size_t>(index)] =
            evaluate(problem, population[static_cast<std::size_t>(index)]);
    });
    timing.evaluator_seconds +=
        std::chrono::duration<double>(Clock::now() - start).count();
    Objectives ideal{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    for (const auto& value : values) {
        for (int objective = 0; objective < 3; ++objective) {
            ideal[static_cast<std::size_t>(objective)] = std::min(
                ideal[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
        }
    }
    for (int generation = 1; generation <= config.generations; ++generation) {
        start = Clock::now();
        std::vector<Layout> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](int index) {
            std::vector<int> global_pool;
            const std::vector<int>* pool_pointer = nullptr;
            if (rng.uniform(
                    static_cast<std::uint64_t>(generation), 49,
                    static_cast<std::uint64_t>(index)
                ) < 0.9) {
                pool_pointer = &neighbors[static_cast<std::size_t>(index)];
            } else {
                global_pool.resize(static_cast<std::size_t>(config.population));
                std::iota(global_pool.begin(), global_pool.end(), 0);
                pool_pointer = &global_pool;
            }
            const auto& pool = *pool_pointer;
            int first = pool[static_cast<std::size_t>(rng.integer(
                0, static_cast<int>(pool.size()),
                static_cast<std::uint64_t>(generation), 50,
                static_cast<std::uint64_t>(index), 0
            ))];
            int second = pool[static_cast<std::size_t>(rng.integer(
                0, static_cast<int>(pool.size()),
                static_cast<std::uint64_t>(generation), 50,
                static_cast<std::uint64_t>(index), 1
            ))];
            if (second == first) {
                second = pool[
                    (static_cast<std::size_t>(second) + 1) % pool.size()
                ];
            }
            offspring[static_cast<std::size_t>(index)] = make_offspring(
                population[static_cast<std::size_t>(index)],
                population[static_cast<std::size_t>(first)],
                population[static_cast<std::size_t>(second)],
                probability, rng, generation, index
            );
            apply_probability_guidance(
                offspring[static_cast<std::size_t>(index)],
                probability, rng, generation, index
            );
        });
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
        std::vector<Objectives> offspring_values(
            static_cast<std::size_t>(config.population)
        );
        start = Clock::now();
        executor.parallel_for(0, config.population, [&](int index) {
            offspring_values[static_cast<std::size_t>(index)] =
                evaluate(problem, offspring[static_cast<std::size_t>(index)]);
        });
        timing.evaluator_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
        start = Clock::now();
        for (const auto& value : offspring_values) {
            for (int objective = 0; objective < 3; ++objective) {
                ideal[static_cast<std::size_t>(objective)] = std::min(
                    ideal[static_cast<std::size_t>(objective)],
                    value[static_cast<std::size_t>(objective)]
                );
            }
        }
        for (int index = 0; index < config.population; ++index) {
            double squared_distance = 0.0;
            for (int objective = 0; objective < 3; ++objective) {
                const double difference =
                    offspring_values[static_cast<std::size_t>(index)]
                                    [static_cast<std::size_t>(objective)]
                    - ideal[static_cast<std::size_t>(objective)];
                squared_distance += difference * difference;
            }
            if (std::sqrt(squared_distance) < config.mu_c) {
                compact_probability(
                    probability,
                    offspring[static_cast<std::size_t>(index)],
                    generation
                );
            }
        }
        for (int index = 0; index < config.population; ++index) {
            int replaced = 0;
            for (const int target :
                 neighbors[static_cast<std::size_t>(index)]) {
                if (weighted_tchebycheff(
                        offspring_values[static_cast<std::size_t>(index)],
                        ideal, weights[static_cast<std::size_t>(target)]
                    )
                    <= weighted_tchebycheff(
                        values[static_cast<std::size_t>(target)],
                        ideal, weights[static_cast<std::size_t>(target)]
                    )) {
                    population[static_cast<std::size_t>(target)] =
                        offspring[static_cast<std::size_t>(index)];
                    values[static_cast<std::size_t>(target)] =
                        offspring_values[static_cast<std::size_t>(index)];
                    if (++replaced == 2) {
                        break;
                    }
                }
            }
        }
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
    }
    const auto end = Clock::now();
    const double total = timing.evaluator_seconds + timing.algorithm_seconds;
    Objectives minima{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    for (const auto& value : values) {
        for (int objective = 0; objective < 3; ++objective) {
            minima[static_cast<std::size_t>(objective)] = std::min(
                minima[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
        }
    }
    (void)end;
    std::cout << std::setprecision(17)
              << "{\"mode\":\"optimize\",\"algorithm\":\"moead_p\","
              << "\"profile\":\"paper_repaired\",\"scenario\":\""
              << problem.wind.id << "\",\"turbines\":" << problem.turbines
              << ",\"population\":" << config.population
              << ",\"generations\":" << config.generations
              << ",\"workers\":" << config.workers
              << ",\"seed\":" << config.seed
              << ",\"ipd\":" << config.ipd
              << ",\"mu_c\":" << config.mu_c
              << ",\"complete_layout_evaluations\":"
              << static_cast<std::uint64_t>(config.population)
                 * static_cast<std::uint64_t>(config.generations + 1)
              << ",\"minimum_inverse_power\":" << minima[0]
              << ",\"minimum_land_area_grid_units\":" << minima[1]
              << ",\"minimum_total_cost\":" << minima[2]
              << ",\"evaluator_seconds\":" << timing.evaluator_seconds
              << ",\"algorithm_seconds\":" << timing.algorithm_seconds
              << ",\"end_to_end_seconds\":" << total << "}\n";
}

Layout parse_layout(const std::string& text) {
    Layout result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        result.push_back(std::stoi(token));
    }
    return result;
}

Config parse(int argc, char** argv) {
    Config config;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        const auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + option);
            }
            return argv[index];
        };
        if (option == "--evaluate-layout") {
            config.mode = "evaluate";
            config.layout_text = value();
        } else if (option == "--scenario") {
            config.scenario = value();
        } else if (option == "--turbines") {
            config.turbines = std::stoi(value());
        } else if (option == "--population") {
            config.population = std::stoi(value());
        } else if (option == "--generations") {
            config.generations = std::stoi(value());
        } else if (option == "--workers") {
            config.workers = std::stoi(value());
        } else if (option == "--seed") {
            config.seed = std::stoull(value());
        } else if (option == "--ipd") {
            config.ipd = std::stoi(value());
        } else if (option == "--mu-c") {
            config.mu_c = std::stod(value());
        } else if (option == "--help") {
            std::cout
                << "pbea_cpp_hpc [--evaluate-layout i,j,...] "
                   "[--scenario ws1|ws2] [--turbines 15..30] "
                   "[--population 100] [--generations 100] "
                   "[--workers N] [--seed N] [--ipd 1..6] [--mu-c 80]\n";
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option " + option);
        }
    }
    if (config.workers <= 0 || config.population < 3
        || config.generations < 0 || config.turbines < 3
        || config.turbines > 400) {
        throw std::invalid_argument("invalid numeric option");
    }
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Config config = parse(argc, argv);
        Problem problem;
        problem.wind = make_wind(config.scenario);
        if (config.mode == "evaluate") {
            const Layout layout = parse_layout(config.layout_text);
            problem.turbines = static_cast<int>(layout.size());
            const auto start = Clock::now();
            const Objectives value = evaluate(problem, layout);
            print_objectives_json(
                problem, layout, value,
                std::chrono::duration<double>(Clock::now() - start).count()
            );
        } else {
            run_optimizer(config);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
