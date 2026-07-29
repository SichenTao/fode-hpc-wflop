/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PBEA/MOEA-D-P three-objective C++ package
Paper title and DOI: Probabilistic Bootstrap-Based Evolutionary Algorithm for
Three-Objective Wind Farm Turbine Position Optimization;
10.1016/j.swevo.2025.101972
Paper/source basis: paper equations and local MATLAB behavior/problem oracle
Public asset: local author archive, no redistribution license, not vendored
Missing/conflicts: partial pcode prevents literal source distribution
Reconstruction: independent pure C++ evaluator and six-algorithm protocol
Method/problem semantic IDs: moead_p_repaired_v1 and registered comparators;
zhang2025_three_objective
Controlling contract and claim boundary:
shared/contracts/pbea_execution_contract.json; independent reconstruction only
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <numbers>
#include <set>
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
    std::string algorithm = "moead_p";
    std::string scenario = "ws1";
    std::string execution_mode = "cpu";
    std::string layout_text;
    std::string output_front;
    int turbines = 15;
    int population = 100;
    int generations = 100;
    int workers = 1;
    int ipd = 3;
    double mu_c = 80.0;
    std::uint64_t seed = 1;
};

struct RankedPopulation {
    std::vector<int> rank;
    std::vector<double> crowding;
};

struct ArReferenceState {
    std::vector<Objectives> archive;
    std::vector<Objectives> reference_points;
    Objectives range_min{};
    Objectives range_max{};
    bool initialized = false;
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

std::vector<double> uniform_probability() {
    return std::vector<double>(400, 1.0 / 400.0);
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

bool dominates(const Objectives& left, const Objectives& right) {
    bool strictly_better = false;
    for (int objective = 0; objective < 3; ++objective) {
        if (left[static_cast<std::size_t>(objective)]
            > right[static_cast<std::size_t>(objective)]) {
            return false;
        }
        strictly_better = strictly_better
            || left[static_cast<std::size_t>(objective)]
               < right[static_cast<std::size_t>(objective)];
    }
    return strictly_better;
}

RankedPopulation rank_and_crowding(const std::vector<Objectives>& values) {
    const int size = static_cast<int>(values.size());
    RankedPopulation result{
        std::vector<int>(static_cast<std::size_t>(size), -1),
        std::vector<double>(static_cast<std::size_t>(size), 0.0)
    };
    std::vector<int> dominated_count(static_cast<std::size_t>(size), 0);
    std::vector<std::vector<int>> dominates_set(
        static_cast<std::size_t>(size)
    );
    std::vector<std::vector<int>> fronts(1);
    for (int left = 0; left < size; ++left) {
        for (int right = left + 1; right < size; ++right) {
            if (dominates(
                    values[static_cast<std::size_t>(left)],
                    values[static_cast<std::size_t>(right)])) {
                dominates_set[static_cast<std::size_t>(left)].push_back(right);
                ++dominated_count[static_cast<std::size_t>(right)];
            } else if (dominates(
                    values[static_cast<std::size_t>(right)],
                    values[static_cast<std::size_t>(left)])) {
                dominates_set[static_cast<std::size_t>(right)].push_back(left);
                ++dominated_count[static_cast<std::size_t>(left)];
            }
        }
    }
    for (int index = 0; index < size; ++index) {
        if (dominated_count[static_cast<std::size_t>(index)] == 0) {
            result.rank[static_cast<std::size_t>(index)] = 0;
            fronts[0].push_back(index);
        }
    }
    for (std::size_t front = 0; front < fronts.size(); ++front) {
        std::vector<int> next;
        for (const int index : fronts[front]) {
            for (const int dominated :
                 dominates_set[static_cast<std::size_t>(index)]) {
                int& count =
                    dominated_count[static_cast<std::size_t>(dominated)];
                if (--count == 0) {
                    result.rank[static_cast<std::size_t>(dominated)] =
                        static_cast<int>(front + 1);
                    next.push_back(dominated);
                }
            }
        }
        if (!next.empty()) {
            fronts.push_back(std::move(next));
        }
    }
    for (const auto& front : fronts) {
        if (front.empty()) {
            continue;
        }
        for (int objective = 0; objective < 3; ++objective) {
            std::vector<int> order = front;
            std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
                return values[static_cast<std::size_t>(left)]
                             [static_cast<std::size_t>(objective)]
                     < values[static_cast<std::size_t>(right)]
                             [static_cast<std::size_t>(objective)];
            });
            result.crowding[static_cast<std::size_t>(order.front())] =
                std::numeric_limits<double>::infinity();
            result.crowding[static_cast<std::size_t>(order.back())] =
                std::numeric_limits<double>::infinity();
            const double minimum =
                values[static_cast<std::size_t>(order.front())]
                      [static_cast<std::size_t>(objective)];
            const double maximum =
                values[static_cast<std::size_t>(order.back())]
                      [static_cast<std::size_t>(objective)];
            if (maximum <= minimum || order.size() < 3) {
                continue;
            }
            for (std::size_t position = 1;
                 position + 1 < order.size(); ++position) {
                result.crowding[
                    static_cast<std::size_t>(order[position])
                ] += (
                    values[static_cast<std::size_t>(order[position + 1])]
                          [static_cast<std::size_t>(objective)]
                    - values[static_cast<std::size_t>(order[position - 1])]
                            [static_cast<std::size_t>(objective)]
                ) / (maximum - minimum);
            }
        }
    }
    return result;
}

int tournament(
    int left,
    int right,
    const RankedPopulation& ranked
) {
    const int left_rank = ranked.rank[static_cast<std::size_t>(left)];
    const int right_rank = ranked.rank[static_cast<std::size_t>(right)];
    if (left_rank != right_rank) {
        return left_rank < right_rank ? left : right;
    }
    const double left_crowding =
        ranked.crowding[static_cast<std::size_t>(left)];
    const double right_crowding =
        ranked.crowding[static_cast<std::size_t>(right)];
    if (left_crowding != right_crowding) {
        return left_crowding > right_crowding ? left : right;
    }
    return std::min(left, right);
}

Layout permutation_offspring(
    const Layout& first,
    const Layout& second,
    const std::vector<double>& probability,
    const fode::CounterRng& rng,
    int generation,
    int individual
) {
    const int dimension = static_cast<int>(first.size());
    Layout child = first;
    const int cut = rng.integer(
        1, dimension + 1,
        static_cast<std::uint64_t>(generation), 70,
        static_cast<std::uint64_t>(individual)
    );
    if (rng.uniform(
            static_cast<std::uint64_t>(generation), 71,
            static_cast<std::uint64_t>(individual)
        ) < 1.0) {
        std::vector<bool> used(401, false);
        for (int coordinate = 0; coordinate < cut; ++coordinate) {
            used[static_cast<std::size_t>(
                child[static_cast<std::size_t>(coordinate)]
            )] = true;
        }
        int destination = cut;
        for (const int cell : second) {
            if (!used[static_cast<std::size_t>(cell)]
                && destination < dimension) {
                child[static_cast<std::size_t>(destination++)] = cell;
                used[static_cast<std::size_t>(cell)] = true;
            }
        }
        for (const int cell : first) {
            if (!used[static_cast<std::size_t>(cell)]
                && destination < dimension) {
                child[static_cast<std::size_t>(destination++)] = cell;
                used[static_cast<std::size_t>(cell)] = true;
            }
        }
    }
    const int from = rng.integer(
        0, dimension,
        static_cast<std::uint64_t>(generation), 72,
        static_cast<std::uint64_t>(individual)
    );
    const int to = rng.integer(
        0, dimension,
        static_cast<std::uint64_t>(generation), 73,
        static_cast<std::uint64_t>(individual)
    );
    const int value = child[static_cast<std::size_t>(from)];
    child.erase(child.begin() + from);
    child.insert(child.begin() + to, value);
    repair_layout(child, probability, rng, generation, individual);
    return child;
}

void print_run_summary(
    const Config& config,
    const Problem& problem,
    const std::vector<Layout>& layouts,
    const std::vector<Objectives>& values,
    const Timings& timing,
    std::uint64_t complete_evaluations
) {
    Objectives minima{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    std::set<Layout> nondominated_layouts;
    const RankedPopulation ranked = rank_and_crowding(values);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (ranked.rank[index] == 0) {
            Layout canonical = layouts[index];
            std::sort(canonical.begin(), canonical.end());
            nondominated_layouts.insert(std::move(canonical));
        }
        for (int objective = 0; objective < 3; ++objective) {
            minima[static_cast<std::size_t>(objective)] = std::min(
                minima[static_cast<std::size_t>(objective)],
                values[index][static_cast<std::size_t>(objective)]
            );
        }
    }
    if (!config.output_front.empty()) {
        const std::filesystem::path output(config.output_front);
        if (!output.parent_path().empty()) {
            std::filesystem::create_directories(output.parent_path());
        }
        const std::filesystem::path temporary =
            output.string() + ".tmp";
        std::ofstream stream(temporary);
        if (!stream) {
            throw std::runtime_error("cannot open front output");
        }
        stream << std::setprecision(17)
               << "{\n  \"schema_version\": 1,\n"
               << "  \"algorithm\": \"" << config.algorithm << "\",\n"
               << "  \"problem_semantics\": \"three_objective_paper_repaired_v1\",\n"
               << "  \"scenario\": \"" << problem.wind.id << "\",\n"
               << "  \"turbines\": " << problem.turbines << ",\n"
               << "  \"seed\": " << config.seed << ",\n"
               << "  \"complete_layout_evaluations\": "
               << complete_evaluations << ",\n"
               << "  \"solutions\": [\n";
        bool first_solution = true;
        std::set<Layout> written_layouts;
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (ranked.rank[index] != 0) {
                continue;
            }
            Layout canonical = layouts[index];
            std::sort(canonical.begin(), canonical.end());
            if (!written_layouts.insert(std::move(canonical)).second) {
                continue;
            }
            if (!first_solution) {
                stream << ",\n";
            }
            first_solution = false;
            stream << "    {\"objectives\": ["
                   << values[index][0] << ", " << values[index][1]
                   << ", " << values[index][2] << "], \"layout\": [";
            for (std::size_t cell = 0; cell < layouts[index].size(); ++cell) {
                if (cell != 0) {
                    stream << ", ";
                }
                stream << layouts[index][cell];
            }
            stream << "]}";
        }
        stream << "\n  ]\n}\n";
        stream.close();
        if (!stream) {
            throw std::runtime_error("failed writing front output");
        }
        std::filesystem::rename(temporary, output);
    }
    const double total = timing.evaluator_seconds + timing.algorithm_seconds;
    std::cout << std::setprecision(17)
              << "{\"mode\":\"optimize\",\"algorithm\":\""
              << config.algorithm << "\",\"profile\":\"paper_repaired\","
              << "\"scenario\":\"" << problem.wind.id
              << "\",\"turbines\":" << problem.turbines
              << ",\"population\":" << config.population
              << ",\"generations\":" << config.generations
              << ",\"workers\":" << config.workers
              << ",\"seed\":" << config.seed
              << ",\"complete_layout_evaluations\":" << complete_evaluations
              << ",\"nondominated_count\":" << nondominated_layouts.size()
              << ",\"minimum_inverse_power\":" << minima[0]
              << ",\"minimum_land_area_grid_units\":" << minima[1]
              << ",\"minimum_total_cost\":" << minima[2]
              << ",\"evaluator_seconds\":" << timing.evaluator_seconds
              << ",\"algorithm_seconds\":" << timing.algorithm_seconds
              << ",\"end_to_end_seconds\":" << total << "}\n";
}

void run_nsga2(const Config& config) {
    Problem problem;
    problem.turbines = config.turbines;
    problem.wind = make_wind(config.scenario);
    const fode::CounterRng rng(config.seed);
    fode::PersistentExecutor executor(config.workers);
    const std::vector<double> probability = uniform_probability();
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
    for (int generation = 1; generation <= config.generations; ++generation) {
        start = Clock::now();
        const RankedPopulation ranked = rank_and_crowding(values);
        std::vector<Layout> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](int index) {
            const int left_first = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 74,
                static_cast<std::uint64_t>(index), 0
            );
            const int right_first = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 74,
                static_cast<std::uint64_t>(index), 1
            );
            const int left_second = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 74,
                static_cast<std::uint64_t>(index), 2
            );
            const int right_second = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 74,
                static_cast<std::uint64_t>(index), 3
            );
            const int parent_first =
                tournament(left_first, right_first, ranked);
            const int parent_second =
                tournament(left_second, right_second, ranked);
            offspring[static_cast<std::size_t>(index)] =
                permutation_offspring(
                    population[static_cast<std::size_t>(parent_first)],
                    population[static_cast<std::size_t>(parent_second)],
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
        std::vector<Layout> combined = population;
        combined.insert(
            combined.end(), offspring.begin(), offspring.end()
        );
        std::vector<Objectives> combined_values = values;
        combined_values.insert(
            combined_values.end(),
            offspring_values.begin(), offspring_values.end()
        );
        const RankedPopulation combined_ranked =
            rank_and_crowding(combined_values);
        std::vector<int> order(combined.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            const int left_rank =
                combined_ranked.rank[static_cast<std::size_t>(left)];
            const int right_rank =
                combined_ranked.rank[static_cast<std::size_t>(right)];
            if (left_rank != right_rank) {
                return left_rank < right_rank;
            }
            const double left_distance =
                combined_ranked.crowding[static_cast<std::size_t>(left)];
            const double right_distance =
                combined_ranked.crowding[static_cast<std::size_t>(right)];
            if (left_distance != right_distance) {
                return left_distance > right_distance;
            }
            return left < right;
        });
        for (int index = 0; index < config.population; ++index) {
            const int selected = order[static_cast<std::size_t>(index)];
            population[static_cast<std::size_t>(index)] =
                std::move(combined[static_cast<std::size_t>(selected)]);
            values[static_cast<std::size_t>(index)] =
                combined_values[static_cast<std::size_t>(selected)];
        }
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
    }
    print_run_summary(
        config, problem, population, values, timing,
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1)
    );
}

std::vector<int> grid_keys(
    const std::vector<Objectives>& values,
    int divisions
) {
    Objectives minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    Objectives maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (const auto& value : values) {
        for (int objective = 0; objective < 3; ++objective) {
            minimum[static_cast<std::size_t>(objective)] = std::min(
                minimum[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
            maximum[static_cast<std::size_t>(objective)] = std::max(
                maximum[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
        }
    }
    std::vector<int> keys(values.size(), 0);
    for (std::size_t index = 0; index < values.size(); ++index) {
        int key = 0;
        for (int objective = 0; objective < 3; ++objective) {
            const double range =
                maximum[static_cast<std::size_t>(objective)]
                - minimum[static_cast<std::size_t>(objective)];
            int cell = 0;
            if (range > 0.0) {
                cell = static_cast<int>(std::floor(
                    (values[index][static_cast<std::size_t>(objective)]
                     - minimum[static_cast<std::size_t>(objective)])
                    / range * static_cast<double>(divisions)
                ));
                cell = std::clamp(cell, 0, divisions - 1);
            }
            key = key * divisions + cell;
        }
        keys[index] = key;
    }
    return keys;
}

void update_mopso_archive(
    std::vector<Layout>& archive,
    std::vector<Objectives>& archive_values,
    const std::vector<Layout>& candidates,
    const std::vector<Objectives>& candidate_values,
    int maximum_size,
    const fode::CounterRng& rng,
    int generation
) {
    archive.insert(archive.end(), candidates.begin(), candidates.end());
    archive_values.insert(
        archive_values.end(), candidate_values.begin(), candidate_values.end()
    );
    const RankedPopulation ranked = rank_and_crowding(archive_values);
    std::vector<Layout> nondominated_layouts;
    std::vector<Objectives> nondominated_values;
    for (std::size_t index = 0; index < archive.size(); ++index) {
        if (ranked.rank[index] == 0) {
            nondominated_layouts.push_back(std::move(archive[index]));
            nondominated_values.push_back(archive_values[index]);
        }
    }
    archive = std::move(nondominated_layouts);
    archive_values = std::move(nondominated_values);
    std::uint64_t deletion = 0;
    while (archive.size() > static_cast<std::size_t>(maximum_size)) {
        const std::vector<int> keys = grid_keys(archive_values, 10);
        std::vector<int> unique_keys = keys;
        std::sort(unique_keys.begin(), unique_keys.end());
        unique_keys.erase(
            std::unique(unique_keys.begin(), unique_keys.end()),
            unique_keys.end()
        );
        int maximum_occupancy = 0;
        std::vector<int> crowded_keys;
        for (const int key : unique_keys) {
            const int occupancy = static_cast<int>(
                std::count(keys.begin(), keys.end(), key)
            );
            if (occupancy > maximum_occupancy) {
                maximum_occupancy = occupancy;
                crowded_keys.assign(1, key);
            } else if (occupancy == maximum_occupancy) {
                crowded_keys.push_back(key);
            }
        }
        const int selected_key = crowded_keys[static_cast<std::size_t>(
            rng.integer(
                0, static_cast<int>(crowded_keys.size()),
                static_cast<std::uint64_t>(generation), 82,
                deletion
            )
        )];
        std::vector<int> members;
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (keys[index] == selected_key) {
                members.push_back(static_cast<int>(index));
            }
        }
        const int selected = members[static_cast<std::size_t>(
            rng.integer(
                0, static_cast<int>(members.size()),
                static_cast<std::uint64_t>(generation), 83,
                deletion
            )
        )];
        archive.erase(archive.begin() + selected);
        archive_values.erase(archive_values.begin() + selected);
        ++deletion;
    }
}

std::vector<int> select_mopso_leaders(
    const std::vector<Objectives>& archive_values,
    int count,
    const fode::CounterRng& rng,
    int generation
) {
    if (archive_values.empty()) {
        throw std::runtime_error("MOPSO archive is empty");
    }
    const std::vector<int> keys = grid_keys(archive_values, 10);
    std::vector<int> unique_keys = keys;
    std::sort(unique_keys.begin(), unique_keys.end());
    unique_keys.erase(
        std::unique(unique_keys.begin(), unique_keys.end()), unique_keys.end()
    );
    std::vector<double> weights;
    weights.reserve(unique_keys.size());
    double total_weight = 0.0;
    for (const int key : unique_keys) {
        const int occupancy = static_cast<int>(
            std::count(keys.begin(), keys.end(), key)
        );
        const double weight = 1.0 / static_cast<double>(occupancy);
        weights.push_back(weight);
        total_weight += weight;
    }
    std::vector<int> result(static_cast<std::size_t>(count));
    for (int individual = 0; individual < count; ++individual) {
        double target = rng.uniform(
            static_cast<std::uint64_t>(generation), 84,
            static_cast<std::uint64_t>(individual)
        ) * total_weight;
        std::size_t selected_grid = 0;
        for (; selected_grid + 1 < weights.size(); ++selected_grid) {
            target -= weights[selected_grid];
            if (target <= 0.0) {
                break;
            }
        }
        std::vector<int> members;
        for (std::size_t index = 0; index < keys.size(); ++index) {
            if (keys[index] == unique_keys[selected_grid]) {
                members.push_back(static_cast<int>(index));
            }
        }
        result[static_cast<std::size_t>(individual)] =
            members[static_cast<std::size_t>(rng.integer(
                0, static_cast<int>(members.size()),
                static_cast<std::uint64_t>(generation), 85,
                static_cast<std::uint64_t>(individual)
            ))];
    }
    return result;
}

void run_mopso(const Config& config) {
    Problem problem;
    problem.turbines = config.turbines;
    problem.wind = make_wind(config.scenario);
    const fode::CounterRng rng(config.seed);
    fode::PersistentExecutor executor(config.workers);
    const std::vector<double> probability = uniform_probability();
    std::vector<Layout> population(
        static_cast<std::size_t>(config.population)
    );
    std::vector<Objectives> values(
        static_cast<std::size_t>(config.population)
    );
    std::vector<std::vector<double>> velocity(
        static_cast<std::size_t>(config.population),
        std::vector<double>(static_cast<std::size_t>(problem.turbines), 0.0)
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
    std::vector<Layout> personal_best = population;
    std::vector<Objectives> personal_best_values = values;
    std::vector<Layout> archive;
    std::vector<Objectives> archive_values;
    update_mopso_archive(
        archive, archive_values, population, values,
        config.population, rng, 0
    );
    for (int generation = 1; generation <= config.generations; ++generation) {
        start = Clock::now();
        const std::vector<int> leaders = select_mopso_leaders(
            archive_values, config.population, rng, generation
        );
        std::vector<Layout> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](int index) {
            Layout child(static_cast<std::size_t>(problem.turbines));
            for (int coordinate = 0;
                 coordinate < problem.turbines; ++coordinate) {
                double& speed = velocity[static_cast<std::size_t>(index)]
                                        [static_cast<std::size_t>(coordinate)];
                const double current = static_cast<double>(
                    population[static_cast<std::size_t>(index)]
                              [static_cast<std::size_t>(coordinate)]
                );
                speed = 0.4 * speed
                    + rng.uniform(
                        static_cast<std::uint64_t>(generation), 86,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(coordinate), 0
                    ) * (
                        static_cast<double>(
                            personal_best[static_cast<std::size_t>(index)]
                                         [static_cast<std::size_t>(coordinate)]
                        ) - current
                    )
                    + rng.uniform(
                        static_cast<std::uint64_t>(generation), 86,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(coordinate), 1
                    ) * (
                        static_cast<double>(
                            archive[static_cast<std::size_t>(
                                leaders[static_cast<std::size_t>(index)]
                            )][static_cast<std::size_t>(coordinate)]
                        ) - current
                    );
                child[static_cast<std::size_t>(coordinate)] =
                    static_cast<int>(std::floor(current + speed));
            }
            repair_layout(child, probability, rng, generation, index);
            offspring[static_cast<std::size_t>(index)] = std::move(child);
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
        population = std::move(offspring);
        values = std::move(offspring_values);
        update_mopso_archive(
            archive, archive_values, population, values,
            config.population, rng, generation
        );
        for (int index = 0; index < config.population; ++index) {
            const std::size_t position = static_cast<std::size_t>(index);
            if (dominates(values[position], personal_best_values[position])
                || (!dominates(
                        personal_best_values[position], values[position])
                    && rng.uniform(
                        static_cast<std::uint64_t>(generation), 87,
                        static_cast<std::uint64_t>(index)
                    ) < 0.5)) {
                personal_best[position] = population[position];
                personal_best_values[position] = values[position];
            }
        }
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
    }
    print_run_summary(
        config, problem, personal_best, personal_best_values, timing,
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1)
    );
}

void update_crowding_archive(
    std::vector<Layout>& archive,
    std::vector<Objectives>& archive_values,
    const std::vector<Layout>& candidates,
    const std::vector<Objectives>& candidate_values,
    int maximum_size
) {
    archive.insert(archive.end(), candidates.begin(), candidates.end());
    archive_values.insert(
        archive_values.end(), candidate_values.begin(), candidate_values.end()
    );
    const RankedPopulation ranked = rank_and_crowding(archive_values);
    std::vector<int> order(archive.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        const int left_rank = ranked.rank[static_cast<std::size_t>(left)];
        const int right_rank = ranked.rank[static_cast<std::size_t>(right)];
        if (left_rank != right_rank) {
            return left_rank < right_rank;
        }
        const double left_crowding =
            ranked.crowding[static_cast<std::size_t>(left)];
        const double right_crowding =
            ranked.crowding[static_cast<std::size_t>(right)];
        if (left_crowding != right_crowding) {
            return left_crowding > right_crowding;
        }
        return left < right;
    });
    const int retained = std::min(
        maximum_size, static_cast<int>(order.size())
    );
    std::vector<Layout> selected_layouts;
    std::vector<Objectives> selected_values;
    selected_layouts.reserve(static_cast<std::size_t>(retained));
    selected_values.reserve(static_cast<std::size_t>(retained));
    for (int index = 0; index < retained; ++index) {
        const int selected = order[static_cast<std::size_t>(index)];
        selected_layouts.push_back(
            std::move(archive[static_cast<std::size_t>(selected)])
        );
        selected_values.push_back(
            archive_values[static_cast<std::size_t>(selected)]
        );
    }
    archive = std::move(selected_layouts);
    archive_values = std::move(selected_values);
}

std::vector<double> morime_quality(
    const std::vector<Objectives>& values
) {
    Objectives minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    };
    Objectives maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (const auto& value : values) {
        for (int objective = 0; objective < 3; ++objective) {
            minimum[static_cast<std::size_t>(objective)] = std::min(
                minimum[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
            maximum[static_cast<std::size_t>(objective)] = std::max(
                maximum[static_cast<std::size_t>(objective)],
                value[static_cast<std::size_t>(objective)]
            );
        }
    }
    std::vector<double> result(values.size(), 0.0);
    for (std::size_t index = 0; index < values.size(); ++index) {
        double normalized_sum = 0.0;
        for (int objective = 0; objective < 3; ++objective) {
            const double range =
                maximum[static_cast<std::size_t>(objective)]
                - minimum[static_cast<std::size_t>(objective)];
            if (range > 0.0) {
                normalized_sum += (
                    values[index][static_cast<std::size_t>(objective)]
                    - minimum[static_cast<std::size_t>(objective)]
                ) / range;
            }
        }
        result[index] = std::clamp(
            1.0 - normalized_sum / 3.0, 0.0, 1.0
        );
    }
    return result;
}

void run_morime(const Config& config) {
    Problem problem;
    problem.turbines = config.turbines;
    problem.wind = make_wind(config.scenario);
    const fode::CounterRng rng(config.seed);
    fode::PersistentExecutor executor(config.workers);
    const std::vector<double> probability = uniform_probability();
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
    std::vector<Layout> archive;
    std::vector<Objectives> archive_values;
    update_crowding_archive(
        archive, archive_values, population, values, config.population
    );
    for (int generation = 1; generation <= config.generations; ++generation) {
        start = Clock::now();
        const std::vector<double> quality = morime_quality(values);
        const std::vector<int> leaders = select_mopso_leaders(
            archive_values, config.population, rng, generation
        );
        const double random_factor =
            (2.0 * rng.uniform(
                static_cast<std::uint64_t>(generation), 90, 0
            ) - 1.0)
            * std::cos(
                std::numbers::pi * static_cast<double>(generation)
                / (static_cast<double>(config.generations) / 10.0)
            )
            * (
                1.0
                - std::round(
                    static_cast<double>(generation) * 5.0
                    / static_cast<double>(config.generations)
                ) / 5.0
            );
        const double exploration = std::sqrt(
            static_cast<double>(generation)
            / static_cast<double>(config.generations)
        );
        std::vector<Layout> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](int index) {
            Layout child = population[static_cast<std::size_t>(index)];
            const Layout& leader = archive[static_cast<std::size_t>(
                leaders[static_cast<std::size_t>(index)]
            )];
            for (int coordinate = 0;
                 coordinate < problem.turbines; ++coordinate) {
                if (rng.uniform(
                        static_cast<std::uint64_t>(generation), 91,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(coordinate)
                    ) < exploration) {
                    child[static_cast<std::size_t>(coordinate)] =
                        static_cast<int>(std::floor(
                            static_cast<double>(
                                leader[static_cast<std::size_t>(coordinate)]
                            )
                            + random_factor * (
                                399.0 * rng.uniform(
                                    static_cast<std::uint64_t>(generation), 92,
                                    static_cast<std::uint64_t>(index),
                                    static_cast<std::uint64_t>(coordinate)
                                ) + 1.0
                            )
                        ));
                }
                if (rng.uniform(
                        static_cast<std::uint64_t>(generation), 93,
                        static_cast<std::uint64_t>(index),
                        static_cast<std::uint64_t>(coordinate)
                    ) < quality[static_cast<std::size_t>(index)]) {
                    child[static_cast<std::size_t>(coordinate)] =
                        leader[static_cast<std::size_t>(coordinate)];
                }
            }
            repair_layout(child, probability, rng, generation, index);
            offspring[static_cast<std::size_t>(index)] = std::move(child);
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
        population = std::move(offspring);
        values = std::move(offspring_values);
        update_crowding_archive(
            archive, archive_values, population, values, config.population
        );
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
    }
    print_run_summary(
        config, problem, population, values, timing,
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1)
    );
}

double objective_norm(const Objectives& value) {
    double squared = 0.0;
    for (const double component : value) {
        squared += component * component;
    }
    return std::sqrt(squared);
}

double cosine_similarity(
    const Objectives& left,
    const Objectives& right
) {
    double dot = 0.0;
    for (int objective = 0; objective < 3; ++objective) {
        dot += left[static_cast<std::size_t>(objective)]
             * right[static_cast<std::size_t>(objective)];
    }
    const double denominator = objective_norm(left) * objective_norm(right);
    return denominator > 0.0 ? dot / denominator : 0.0;
}

std::vector<std::vector<double>> ar_distance(
    const std::vector<Objectives>& input_values,
    const std::vector<Objectives>& input_reference
) {
    std::vector<Objectives> values = input_values;
    std::vector<Objectives> reference = input_reference;
    for (Objectives& value : values) {
        for (double& component : value) {
            component = std::max(component, 1e-6);
        }
    }
    for (Objectives& point : reference) {
        for (double& component : point) {
            component = std::max(component, 1e-6);
        }
    }
    const int population_size = static_cast<int>(values.size());
    const int reference_size = static_cast<int>(reference.size());
    for (int point = 0; point < reference_size; ++point) {
        double best_perpendicular = std::numeric_limits<double>::infinity();
        int nearest = 0;
        double nearest_projection = 0.0;
        const double reference_norm =
            objective_norm(reference[static_cast<std::size_t>(point)]);
        for (int individual = 0;
             individual < population_size; ++individual) {
            const double value_norm =
                objective_norm(values[static_cast<std::size_t>(individual)]);
            const double cosine = std::clamp(
                cosine_similarity(
                    values[static_cast<std::size_t>(individual)],
                    reference[static_cast<std::size_t>(point)]
                ), -1.0, 1.0
            );
            const double projection = value_norm * cosine;
            const double perpendicular =
                value_norm * std::sqrt(std::max(0.0, 1.0 - cosine * cosine));
            if (perpendicular < best_perpendicular) {
                best_perpendicular = perpendicular;
                nearest = individual;
                nearest_projection = projection;
            }
        }
        (void)nearest;
        const double scale = reference_norm > 0.0
            ? nearest_projection / reference_norm : 1.0;
        for (double& component :
             reference[static_cast<std::size_t>(point)]) {
            component *= scale;
        }
    }
    std::vector<std::vector<double>> distance(
        values.size(), std::vector<double>(reference.size(), 0.0)
    );
    for (std::size_t individual = 0;
         individual < values.size(); ++individual) {
        for (std::size_t point = 0;
             point < reference.size(); ++point) {
            double squared = 0.0;
            for (int objective = 0; objective < 3; ++objective) {
                const double difference =
                    values[individual][static_cast<std::size_t>(objective)]
                    - reference[point][static_cast<std::size_t>(objective)];
                squared += difference * difference;
            }
            distance[individual][point] = std::sqrt(squared);
        }
    }
    return distance;
}

std::vector<double> ar_indicator_fitness(
    const std::vector<std::vector<double>>& distance
) {
    const int population_size = static_cast<int>(distance.size());
    const int reference_size = distance.empty()
        ? 0 : static_cast<int>(distance.front().size());
    std::vector<double> convergence(
        static_cast<std::size_t>(population_size),
        std::numeric_limits<double>::infinity()
    );
    std::vector<std::vector<int>> rank(
        static_cast<std::size_t>(reference_size)
    );
    double metric = 0.0;
    std::vector<bool> noncontributing(
        static_cast<std::size_t>(population_size), true
    );
    for (int point = 0; point < reference_size; ++point) {
        auto& order = rank[static_cast<std::size_t>(point)];
        order.resize(static_cast<std::size_t>(population_size));
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
            return distance[static_cast<std::size_t>(left)]
                           [static_cast<std::size_t>(point)]
                 < distance[static_cast<std::size_t>(right)]
                           [static_cast<std::size_t>(point)];
        });
        if (!order.empty()) {
            noncontributing[static_cast<std::size_t>(order[0])] = false;
            metric += distance[static_cast<std::size_t>(order[0])]
                              [static_cast<std::size_t>(point)];
        }
    }
    for (int individual = 0;
         individual < population_size; ++individual) {
        for (int point = 0; point < reference_size; ++point) {
            convergence[static_cast<std::size_t>(individual)] = std::min(
                convergence[static_cast<std::size_t>(individual)],
                distance[static_cast<std::size_t>(individual)]
                        [static_cast<std::size_t>(point)]
            );
        }
        if (noncontributing[static_cast<std::size_t>(individual)]) {
            metric += convergence[static_cast<std::size_t>(individual)];
        }
    }
    std::vector<double> fitness(
        static_cast<std::size_t>(population_size),
        std::numeric_limits<double>::infinity()
    );
    for (int individual = 0;
         individual < population_size; ++individual) {
        if (noncontributing[static_cast<std::size_t>(individual)]) {
            fitness[static_cast<std::size_t>(individual)] =
                metric - convergence[static_cast<std::size_t>(individual)];
            continue;
        }
        double value = metric;
        std::vector<bool> second_noncontributing(
            static_cast<std::size_t>(population_size), false
        );
        for (int point = 0; point < reference_size; ++point) {
            const auto& order = rank[static_cast<std::size_t>(point)];
            if (!order.empty() && order[0] == individual) {
                value -= distance[static_cast<std::size_t>(individual)]
                                 [static_cast<std::size_t>(point)];
                if (order.size() > 1) {
                    const int second = order[1];
                    value += distance[static_cast<std::size_t>(second)]
                                     [static_cast<std::size_t>(point)];
                    second_noncontributing[
                        static_cast<std::size_t>(second)
                    ] = true;
                }
            }
        }
        for (int candidate = 0;
             candidate < population_size; ++candidate) {
            if (second_noncontributing[static_cast<std::size_t>(candidate)]
                && noncontributing[static_cast<std::size_t>(candidate)]) {
                value -= convergence[static_cast<std::size_t>(candidate)];
            }
        }
        fitness[static_cast<std::size_t>(individual)] = value;
    }
    return fitness;
}

std::vector<Objectives> unique_nondominated(
    const std::vector<Objectives>& values
) {
    const RankedPopulation ranked = rank_and_crowding(values);
    std::vector<Objectives> result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (ranked.rank[index] == 0) {
            result.push_back(values[index]);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void ar_add_cosine_diverse(
    const std::vector<Objectives>& candidates,
    std::vector<bool>& chosen,
    int target
) {
    while (std::count(chosen.begin(), chosen.end(), true) < target) {
        int selected = -1;
        double selected_similarity = std::numeric_limits<double>::infinity();
        for (std::size_t candidate = 0;
             candidate < candidates.size(); ++candidate) {
            if (chosen[candidate]) {
                continue;
            }
            double maximum_similarity = -1.0;
            for (std::size_t reference = 0;
                 reference < candidates.size(); ++reference) {
                if (chosen[reference]) {
                    maximum_similarity = std::max(
                        maximum_similarity,
                        cosine_similarity(
                            candidates[candidate], candidates[reference]
                        )
                    );
                }
            }
            if (maximum_similarity < selected_similarity) {
                selected_similarity = maximum_similarity;
                selected = static_cast<int>(candidate);
            }
        }
        if (selected < 0) {
            break;
        }
        chosen[static_cast<std::size_t>(selected)] = true;
    }
}

void update_ar_reference(
    ArReferenceState& state,
    const std::vector<Objectives>& additions,
    const std::vector<Objectives>& base_weights
) {
    std::vector<Objectives> combined = state.archive;
    combined.insert(combined.end(), additions.begin(), additions.end());
    state.archive = unique_nondominated(combined);
    if (state.archive.empty()) {
        state.reference_points = base_weights;
        return;
    }
    if (!state.initialized) {
        state.range_min = state.archive.front();
        state.range_max = state.archive.front();
        for (const auto& value : state.archive) {
            for (int objective = 0; objective < 3; ++objective) {
                state.range_min[static_cast<std::size_t>(objective)] = std::min(
                    state.range_min[static_cast<std::size_t>(objective)],
                    value[static_cast<std::size_t>(objective)]
                );
                state.range_max[static_cast<std::size_t>(objective)] = std::max(
                    state.range_max[static_cast<std::size_t>(objective)],
                    value[static_cast<std::size_t>(objective)]
                );
            }
        }
        state.initialized = true;
    } else {
        for (const auto& value : state.archive) {
            for (int objective = 0; objective < 3; ++objective) {
                state.range_min[static_cast<std::size_t>(objective)] = std::min(
                    state.range_min[static_cast<std::size_t>(objective)],
                    value[static_cast<std::size_t>(objective)]
                );
            }
        }
    }
    if (state.archive.size() <= 1) {
        state.reference_points = base_weights;
        return;
    }
    std::vector<Objectives> translated = state.archive;
    for (Objectives& value : translated) {
        for (int objective = 0; objective < 3; ++objective) {
            value[static_cast<std::size_t>(objective)] -=
                state.range_min[static_cast<std::size_t>(objective)];
        }
    }
    std::vector<Objectives> scaled_weights = base_weights;
    for (Objectives& weight : scaled_weights) {
        for (int objective = 0; objective < 3; ++objective) {
            weight[static_cast<std::size_t>(objective)] *= std::max(
                1e-6,
                state.range_max[static_cast<std::size_t>(objective)]
                - state.range_min[static_cast<std::size_t>(objective)]
            );
        }
    }
    const auto distance = ar_distance(translated, scaled_weights);
    std::vector<int> nearest_solution(scaled_weights.size(), 0);
    for (std::size_t point = 0; point < scaled_weights.size(); ++point) {
        for (std::size_t individual = 1;
             individual < translated.size(); ++individual) {
            if (distance[individual][point]
                < distance[static_cast<std::size_t>(
                    nearest_solution[point])][point]) {
                nearest_solution[point] = static_cast<int>(individual);
            }
        }
    }
    std::vector<bool> contributing(translated.size(), false);
    for (const int index : nearest_solution) {
        contributing[static_cast<std::size_t>(index)] = true;
    }
    std::vector<bool> valid_weight(scaled_weights.size(), false);
    for (std::size_t individual = 0;
         individual < translated.size(); ++individual) {
        if (!contributing[individual]) {
            continue;
        }
        std::size_t nearest = 0;
        for (std::size_t point = 1; point < scaled_weights.size(); ++point) {
            if (distance[individual][point]
                < distance[individual][nearest]) {
                nearest = point;
            }
        }
        valid_weight[nearest] = true;
    }
    ar_add_cosine_diverse(
        translated, contributing,
        std::min(
            3 * static_cast<int>(scaled_weights.size()),
            static_cast<int>(translated.size())
        )
    );
    std::vector<Objectives> selected_archive;
    std::vector<Objectives> selected_translated;
    for (std::size_t index = 0; index < contributing.size(); ++index) {
        if (contributing[index]) {
            selected_archive.push_back(state.archive[index]);
            selected_translated.push_back(translated[index]);
        }
    }
    state.archive = std::move(selected_archive);
    std::vector<Objectives> reference_candidates;
    std::vector<bool> reference_chosen;
    for (std::size_t index = 0; index < scaled_weights.size(); ++index) {
        if (valid_weight[index]) {
            reference_candidates.push_back(scaled_weights[index]);
            reference_chosen.push_back(true);
        }
    }
    for (const auto& value : selected_translated) {
        reference_candidates.push_back(value);
        reference_chosen.push_back(false);
    }
    if (reference_candidates.empty()) {
        state.reference_points = base_weights;
        return;
    }
    ar_add_cosine_diverse(
        reference_candidates, reference_chosen,
        std::min(
            static_cast<int>(scaled_weights.size()),
            static_cast<int>(reference_candidates.size())
        )
    );
    state.reference_points.clear();
    for (std::size_t index = 0;
         index < reference_candidates.size(); ++index) {
        if (reference_chosen[index]) {
            state.reference_points.push_back(reference_candidates[index]);
        }
    }
}

std::vector<int> ar_environmental_indices(
    const std::vector<Objectives>& values,
    const ArReferenceState& state,
    int retained
) {
    const RankedPopulation ranked = rank_and_crowding(values);
    int last_rank = 0;
    while (std::count_if(
        ranked.rank.begin(), ranked.rank.end(),
        [&](int rank) { return rank <= last_rank; }
    ) < retained) {
        ++last_rank;
    }
    std::vector<int> selected;
    std::vector<int> last;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (ranked.rank[index] < last_rank) {
            selected.push_back(static_cast<int>(index));
        } else if (ranked.rank[index] == last_rank) {
            last.push_back(static_cast<int>(index));
        }
    }
    const int needed = retained - static_cast<int>(selected.size());
    std::vector<Objectives> last_values;
    for (const int index : last) {
        Objectives translated = values[static_cast<std::size_t>(index)];
        for (int objective = 0; objective < 3; ++objective) {
            translated[static_cast<std::size_t>(objective)] -=
                state.range_min[static_cast<std::size_t>(objective)];
        }
        last_values.push_back(translated);
    }
    const auto distance = ar_distance(
        last_values, state.reference_points
    );
    std::vector<int> remaining(last.size());
    std::iota(remaining.begin(), remaining.end(), 0);
    while (static_cast<int>(remaining.size()) > needed) {
        std::vector<std::vector<double>> active_distance;
        active_distance.reserve(remaining.size());
        for (const int index : remaining) {
            active_distance.push_back(
                distance[static_cast<std::size_t>(index)]
            );
        }
        const std::vector<double> fitness =
            ar_indicator_fitness(active_distance);
        const auto worst = std::min_element(fitness.begin(), fitness.end());
        remaining.erase(
            remaining.begin() + std::distance(fitness.begin(), worst)
        );
    }
    for (const int index : remaining) {
        selected.push_back(last[static_cast<std::size_t>(index)]);
    }
    return selected;
}

void run_armoea(const Config& config) {
    Problem problem;
    problem.turbines = config.turbines;
    problem.wind = make_wind(config.scenario);
    const fode::CounterRng rng(config.seed);
    fode::PersistentExecutor executor(config.workers);
    const std::vector<double> probability = uniform_probability();
    const std::vector<Objectives> weights =
        make_weights(config.population, rng);
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
    ArReferenceState reference_state;
    update_ar_reference(reference_state, values, weights);
    for (int generation = 1; generation <= config.generations; ++generation) {
        start = Clock::now();
        std::vector<Objectives> translated = values;
        for (Objectives& value : translated) {
            for (int objective = 0; objective < 3; ++objective) {
                value[static_cast<std::size_t>(objective)] -=
                    reference_state.range_min[
                        static_cast<std::size_t>(objective)
                    ];
            }
        }
        const std::vector<double> fitness = ar_indicator_fitness(
            ar_distance(translated, reference_state.reference_points)
        );
        std::vector<Layout> offspring(
            static_cast<std::size_t>(config.population)
        );
        executor.parallel_for(0, config.population, [&](int index) {
            const int first_a = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 100,
                static_cast<std::uint64_t>(index), 0
            );
            const int first_b = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 100,
                static_cast<std::uint64_t>(index), 1
            );
            const int second_a = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 100,
                static_cast<std::uint64_t>(index), 2
            );
            const int second_b = rng.integer(
                0, config.population,
                static_cast<std::uint64_t>(generation), 100,
                static_cast<std::uint64_t>(index), 3
            );
            const int parent_first = fitness[
                static_cast<std::size_t>(first_a)
            ] >= fitness[static_cast<std::size_t>(first_b)]
                ? first_a : first_b;
            const int parent_second = fitness[
                static_cast<std::size_t>(second_a)
            ] >= fitness[static_cast<std::size_t>(second_b)]
                ? second_a : second_b;
            offspring[static_cast<std::size_t>(index)] =
                permutation_offspring(
                    population[static_cast<std::size_t>(parent_first)],
                    population[static_cast<std::size_t>(parent_second)],
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
        update_ar_reference(reference_state, offspring_values, weights);
        std::vector<Layout> combined = population;
        combined.insert(
            combined.end(), offspring.begin(), offspring.end()
        );
        std::vector<Objectives> combined_values = values;
        combined_values.insert(
            combined_values.end(),
            offspring_values.begin(), offspring_values.end()
        );
        const std::vector<int> selected = ar_environmental_indices(
            combined_values, reference_state, config.population
        );
        for (int index = 0; index < config.population; ++index) {
            const int source = selected[static_cast<std::size_t>(index)];
            population[static_cast<std::size_t>(index)] =
                std::move(combined[static_cast<std::size_t>(source)]);
            values[static_cast<std::size_t>(index)] =
                combined_values[static_cast<std::size_t>(source)];
        }
        for (int objective = 0; objective < 3; ++objective) {
            reference_state.range_max[
                static_cast<std::size_t>(objective)
            ] = values.front()[static_cast<std::size_t>(objective)];
            for (const auto& value : values) {
                reference_state.range_max[
                    static_cast<std::size_t>(objective)
                ] = std::max(
                    reference_state.range_max[
                        static_cast<std::size_t>(objective)
                    ],
                    value[static_cast<std::size_t>(objective)]
                );
            }
            if (reference_state.range_max[
                    static_cast<std::size_t>(objective)
                ] - reference_state.range_min[
                    static_cast<std::size_t>(objective)
                ] < 1e-6) {
                reference_state.range_max[
                    static_cast<std::size_t>(objective)
                ] = reference_state.range_min[
                    static_cast<std::size_t>(objective)
                ] + 1.0;
            }
        }
        timing.algorithm_seconds +=
            std::chrono::duration<double>(Clock::now() - start).count();
    }
    print_run_summary(
        config, problem, population, values, timing,
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1)
    );
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
    const bool probability_guided = config.algorithm == "moead_p";
    std::vector<double> probability = probability_guided
        ? initial_probability(config.ipd)
        : uniform_probability();
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
            if (probability_guided) {
                apply_probability_guidance(
                    offspring[static_cast<std::size_t>(index)],
                    probability, rng, generation, index
                );
            }
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
        if (probability_guided) {
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
    (void)end;
    (void)total;
    print_run_summary(
        config, problem, population, values, timing,
        static_cast<std::uint64_t>(config.population)
        * static_cast<std::uint64_t>(config.generations + 1)
    );
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
        } else if (option == "--algorithm") {
            config.algorithm = value();
        } else if (option == "--output-front") {
            config.output_front = value();
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
        } else if (option == "--execution-mode") {
            config.execution_mode = value();
        } else if (option == "--ipd") {
            config.ipd = std::stoi(value());
        } else if (option == "--mu-c") {
            config.mu_c = std::stod(value());
        } else if (option == "--help") {
            std::cout
                << "pbea_cpp_hpc [--evaluate-layout i,j,...] "
                   "[--algorithm moead_p|moead|nsgaii|mopso|morime|armoea] "
                   "[--scenario ws1|ws2] [--turbines 15..30] "
                   "[--population 100] [--generations 100] "
                   "[--workers N] [--seed N] [--ipd 1..6] [--mu-c 80] "
                   "[--execution-mode cpu|auto|hybrid|gpu] "
                   "[--output-front result.json]\n";
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
    if (
        config.execution_mode != "cpu"
        && config.execution_mode != "auto"
        && config.execution_mode != "hybrid"
        && config.execution_mode != "gpu"
    ) {
        throw std::invalid_argument(
            "--execution-mode must be cpu, auto, hybrid, or gpu"
        );
    }
    if (config.execution_mode != "cpu") {
        throw std::invalid_argument(
            "execution mode " + config.execution_mode
            + " is recognized but unavailable; no hidden CPU fallback "
              "was performed"
        );
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
            if (config.algorithm == "moead_p"
                || config.algorithm == "moead") {
                run_optimizer(config);
            } else if (config.algorithm == "nsgaii") {
                run_nsga2(config);
            } else if (config.algorithm == "mopso") {
                run_mopso(config);
            } else if (config.algorithm == "morime") {
                run_morime(config);
            } else if (config.algorithm == "armoea") {
                run_armoea(config);
            } else {
                throw std::invalid_argument(
                    "algorithm must be moead_p, moead, nsgaii, mopso, morime, or armoea"
                );
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
