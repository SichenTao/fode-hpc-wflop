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
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

struct CurvePoint {
    double speed = 0.0;
    double power = 0.0;
    double ct = 0.0;
};

struct Problem {
    std::string profile;
    std::string case_id;
    std::uint64_t candidate_seed = 0;
    int turbine_count = 0;
    double minimum_spacing_m = 0.0;
    Point substation;
    std::vector<Point> boundary;
    std::vector<Point> candidates;
    std::vector<double> theta;
    std::vector<double> velocity;
    std::vector<double> probability;
    std::vector<CurvePoint> curve;
    std::vector<double> power_second_derivative;
    double turbine_pinst_kw = 4200.0;
    double hub_height_m = 91.5;
    double rotor_diameter_m = 117.0;
    double cut_in_mps = 3.0;
    double cut_out_mps = 25.0;
    double sea_depth_m = 7.0;
    double offshore_length_m = 0.0;
    std::vector<int> cable_capacity;
    std::vector<double> cable_price;
    double export_cable_price = 601.5;
};

std::vector<double> solve_dense(
    std::vector<std::vector<double>> matrix,
    std::vector<double> right_hand_side
) {
    const int size = static_cast<int>(matrix.size());
    for (int pivot = 0; pivot < size; ++pivot) {
        int selected = pivot;
        for (int row = pivot + 1; row < size; ++row) {
            if (std::abs(matrix[static_cast<std::size_t>(row)]
                               [static_cast<std::size_t>(pivot)])
                > std::abs(matrix[static_cast<std::size_t>(selected)]
                                 [static_cast<std::size_t>(pivot)])) {
                selected = row;
            }
        }
        if (std::abs(matrix[static_cast<std::size_t>(selected)]
                           [static_cast<std::size_t>(pivot)]) < 1e-14) {
            throw std::runtime_error("singular cubic-spline system");
        }
        std::swap(
            matrix[static_cast<std::size_t>(pivot)],
            matrix[static_cast<std::size_t>(selected)]
        );
        std::swap(
            right_hand_side[static_cast<std::size_t>(pivot)],
            right_hand_side[static_cast<std::size_t>(selected)]
        );
        for (int row = pivot + 1; row < size; ++row) {
            const double factor =
                matrix[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(pivot)]
                / matrix[static_cast<std::size_t>(pivot)]
                        [static_cast<std::size_t>(pivot)];
            for (int column = pivot; column < size; ++column) {
                matrix[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(column)]
                    -= factor
                    * matrix[static_cast<std::size_t>(pivot)]
                            [static_cast<std::size_t>(column)];
            }
            right_hand_side[static_cast<std::size_t>(row)]
                -= factor
                * right_hand_side[static_cast<std::size_t>(pivot)];
        }
    }
    std::vector<double> solution(static_cast<std::size_t>(size), 0.0);
    for (int row = size - 1; row >= 0; --row) {
        double value = right_hand_side[static_cast<std::size_t>(row)];
        for (int column = row + 1; column < size; ++column) {
            value -= matrix[static_cast<std::size_t>(row)]
                           [static_cast<std::size_t>(column)]
                * solution[static_cast<std::size_t>(column)];
        }
        solution[static_cast<std::size_t>(row)] =
            value
            / matrix[static_cast<std::size_t>(row)]
                    [static_cast<std::size_t>(row)];
    }
    return solution;
}

std::vector<double> build_not_a_knot_second_derivatives(
    const std::vector<CurvePoint>& curve
) {
    const int size = static_cast<int>(curve.size());
    if (size < 4) {
        throw std::runtime_error(
            "not-a-knot spline requires at least four curve points"
        );
    }
    std::vector<std::vector<double>> matrix(
        static_cast<std::size_t>(size),
        std::vector<double>(static_cast<std::size_t>(size), 0.0)
    );
    std::vector<double> right_hand_side(
        static_cast<std::size_t>(size), 0.0
    );
    const double first_left = curve[1].speed - curve[0].speed;
    const double first_right = curve[2].speed - curve[1].speed;
    matrix[0][0] = -first_right;
    matrix[0][1] = first_left + first_right;
    matrix[0][2] = -first_left;
    for (int index = 1; index < size - 1; ++index) {
        const double left =
            curve[static_cast<std::size_t>(index)].speed
            - curve[static_cast<std::size_t>(index - 1)].speed;
        const double right =
            curve[static_cast<std::size_t>(index + 1)].speed
            - curve[static_cast<std::size_t>(index)].speed;
        matrix[static_cast<std::size_t>(index)]
              [static_cast<std::size_t>(index - 1)] = left;
        matrix[static_cast<std::size_t>(index)]
              [static_cast<std::size_t>(index)] =
            2.0 * (left + right);
        matrix[static_cast<std::size_t>(index)]
              [static_cast<std::size_t>(index + 1)] = right;
        right_hand_side[static_cast<std::size_t>(index)] =
            6.0 * (
                (
                    curve[static_cast<std::size_t>(index + 1)].power
                    - curve[static_cast<std::size_t>(index)].power
                ) / right
                - (
                    curve[static_cast<std::size_t>(index)].power
                    - curve[static_cast<std::size_t>(index - 1)].power
                ) / left
            );
    }
    const double last_left =
        curve[static_cast<std::size_t>(size - 2)].speed
        - curve[static_cast<std::size_t>(size - 3)].speed;
    const double last_right =
        curve[static_cast<std::size_t>(size - 1)].speed
        - curve[static_cast<std::size_t>(size - 2)].speed;
    matrix[static_cast<std::size_t>(size - 1)]
          [static_cast<std::size_t>(size - 3)] = -last_right;
    matrix[static_cast<std::size_t>(size - 1)]
          [static_cast<std::size_t>(size - 2)] = last_left + last_right;
    matrix[static_cast<std::size_t>(size - 1)]
          [static_cast<std::size_t>(size - 1)] = -last_left;
    return solve_dense(std::move(matrix), std::move(right_hand_side));
}

template <typename T>
void expect(std::istream& input, const T& expected) {
    T actual{};
    input >> actual;
    if (!input || actual != expected) {
        throw std::runtime_error("invalid GGA snapshot near expected token");
    }
}

std::vector<double> read_double_vector(
    std::istream& input,
    const std::string& label
) {
    expect(input, label);
    int size = 0;
    input >> size;
    if (size < 0) {
        throw std::runtime_error("negative vector size");
    }
    std::vector<double> values(static_cast<std::size_t>(size));
    for (double& value : values) {
        input >> value;
    }
    return values;
}

Problem load_problem(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open GGA problem snapshot");
    }
    Problem problem;
    expect(input, std::string("GGA_CASE_V1"));
    expect(input, std::string("profile"));
    input >> problem.profile;
    expect(input, std::string("case"));
    input >> problem.case_id;
    expect(input, std::string("candidate_seed"));
    input >> problem.candidate_seed;
    expect(input, std::string("turbine_count"));
    input >> problem.turbine_count;
    expect(input, std::string("minimum_spacing_m"));
    input >> problem.minimum_spacing_m;
    expect(input, std::string("substation_m"));
    input >> problem.substation.x >> problem.substation.y;
    expect(input, std::string("boundary_count"));
    int boundary_count = 0;
    input >> boundary_count;
    problem.boundary.resize(static_cast<std::size_t>(boundary_count));
    for (Point& point : problem.boundary) {
        expect(input, std::string("boundary"));
        input >> point.x >> point.y;
    }
    expect(input, std::string("candidate_count"));
    int candidate_count = 0;
    input >> candidate_count;
    problem.candidates.resize(static_cast<std::size_t>(candidate_count));
    for (Point& point : problem.candidates) {
        expect(input, std::string("candidate"));
        input >> point.x >> point.y;
    }
    problem.theta = read_double_vector(input, "theta_radians");
    problem.velocity = read_double_vector(input, "velocity_mps");
    problem.probability = read_double_vector(
        input, "joint_probability_dir_major"
    );
    expect(input, std::string("curve_count"));
    int curve_count = 0;
    input >> curve_count;
    problem.curve.resize(static_cast<std::size_t>(curve_count));
    for (CurvePoint& point : problem.curve) {
        expect(input, std::string("curve"));
        input >> point.speed >> point.power >> point.ct;
    }
    expect(input, std::string("turbine_pinst_kw"));
    input >> problem.turbine_pinst_kw;
    expect(input, std::string("hub_height_m"));
    input >> problem.hub_height_m;
    expect(input, std::string("rotor_diameter_m"));
    input >> problem.rotor_diameter_m;
    expect(input, std::string("cut_in_mps"));
    input >> problem.cut_in_mps;
    expect(input, std::string("cut_out_mps"));
    input >> problem.cut_out_mps;
    expect(input, std::string("sea_depth_m"));
    input >> problem.sea_depth_m;
    expect(input, std::string("offshore_length_m"));
    input >> problem.offshore_length_m;
    const auto capacity = read_double_vector(input, "inner_cable_capacity");
    for (const double value : capacity) {
        problem.cable_capacity.push_back(
            static_cast<int>(std::llround(value))
        );
    }
    problem.cable_price = read_double_vector(input, "inner_cable_price");
    expect(input, std::string("export_cable_price"));
    input >> problem.export_cable_price;
    bool curve_is_strictly_increasing = true;
    for (std::size_t index = 1; index < problem.curve.size(); ++index) {
        curve_is_strictly_increasing =
            curve_is_strictly_increasing
            && problem.curve[index - 1].speed < problem.curve[index].speed;
    }
    const double probability_sum = std::accumulate(
        problem.probability.begin(), problem.probability.end(), 0.0
    );
    if (!input || problem.turbine_count <= 0
        || static_cast<int>(problem.candidates.size())
            < problem.turbine_count
        || problem.theta.empty() || problem.velocity.empty()
        || problem.probability.size()
            != problem.theta.size() * problem.velocity.size()
        || problem.curve.size() < 4 || !curve_is_strictly_increasing
        || problem.cable_capacity.size() != problem.cable_price.size()
        || !std::isfinite(probability_sum)
        || std::abs(probability_sum - 1.0) > 1e-10
        || std::any_of(
            problem.probability.begin(),
            problem.probability.end(),
            [](double value) { return value < 0.0; }
        )) {
        throw std::runtime_error("incomplete GGA problem snapshot");
    }
    problem.power_second_derivative =
        build_not_a_knot_second_derivatives(problem.curve);
    return problem;
}

double interpolate(
    const std::vector<CurvePoint>& curve,
    double speed,
    bool power
) {
    if (speed <= curve.front().speed) {
        return power ? curve.front().power : curve.front().ct;
    }
    if (speed >= curve.back().speed) {
        return power ? curve.back().power : curve.back().ct;
    }
    const auto upper = std::upper_bound(
        curve.begin(), curve.end(), speed,
        [](double value, const CurvePoint& point) {
            return value < point.speed;
        }
    );
    const auto lower = upper - 1;
    const double fraction =
        (speed - lower->speed) / (upper->speed - lower->speed);
    const double left = power ? lower->power : lower->ct;
    const double right = power ? upper->power : upper->ct;
    return left + fraction * (right - left);
}

double spline_power(const Problem& problem, double speed) {
    const auto& curve = problem.curve;
    if (speed <= curve.front().speed) {
        return curve.front().power;
    }
    if (speed >= curve.back().speed) {
        return curve.back().power;
    }
    const auto upper = std::upper_bound(
        curve.begin(), curve.end(), speed,
        [](double value, const CurvePoint& point) {
            return value < point.speed;
        }
    );
    const std::size_t right =
        static_cast<std::size_t>(std::distance(curve.begin(), upper));
    const std::size_t left = right - 1;
    const double width = curve[right].speed - curve[left].speed;
    const double left_distance = curve[right].speed - speed;
    const double right_distance = speed - curve[left].speed;
    return (
        problem.power_second_derivative[left]
            * left_distance * left_distance * left_distance
        + problem.power_second_derivative[right]
            * right_distance * right_distance * right_distance
    ) / (6.0 * width)
        + (
            curve[left].power
            - problem.power_second_derivative[left] * width * width / 6.0
        ) * left_distance / width
        + (
            curve[right].power
            - problem.power_second_derivative[right] * width * width / 6.0
        ) * right_distance / width;
}

struct Edge {
    int from = 0;
    int to = 0;
    double length = 0.0;
};

double distance(const Point& lhs, const Point& rhs) {
    return std::hypot(lhs.x - rhs.x, lhs.y - rhs.y);
}

std::vector<Edge> group_tree(
    const std::vector<int>& group,
    const std::vector<Point>& coordinates,
    const Point& substation
) {
    const int size = static_cast<int>(group.size());
    if (size == 0) {
        return {};
    }
    std::vector<double> key(
        static_cast<std::size_t>(size),
        std::numeric_limits<double>::infinity()
    );
    std::vector<int> parent(static_cast<std::size_t>(size), -1);
    std::vector<char> used(static_cast<std::size_t>(size), 0);
    key[0] = 0.0;
    std::vector<Edge> edges;
    for (int iteration = 0; iteration < size; ++iteration) {
        int selected = -1;
        for (int index = 0; index < size; ++index) {
            if (used[static_cast<std::size_t>(index)] == 0
                && (selected < 0
                    || key[static_cast<std::size_t>(index)]
                        < key[static_cast<std::size_t>(selected)])) {
                selected = index;
            }
        }
        used[static_cast<std::size_t>(selected)] = 1;
        if (parent[static_cast<std::size_t>(selected)] >= 0) {
            const int source =
                parent[static_cast<std::size_t>(selected)];
            edges.push_back({
                group[static_cast<std::size_t>(source)],
                group[static_cast<std::size_t>(selected)],
                distance(
                    coordinates[static_cast<std::size_t>(source)],
                    coordinates[static_cast<std::size_t>(selected)]
                )
            });
        }
        for (int other = 0; other < size; ++other) {
            const double candidate = distance(
                coordinates[static_cast<std::size_t>(selected)],
                coordinates[static_cast<std::size_t>(other)]
            );
            if (used[static_cast<std::size_t>(other)] == 0
                && candidate < key[static_cast<std::size_t>(other)]) {
                key[static_cast<std::size_t>(other)] = candidate;
                parent[static_cast<std::size_t>(other)] = selected;
            }
        }
    }
    int closest = 0;
    for (int index = 1; index < size; ++index) {
        if (distance(coordinates[static_cast<std::size_t>(index)], substation)
            < distance(coordinates[static_cast<std::size_t>(closest)],
                       substation)) {
            closest = index;
        }
    }
    edges.push_back({
        -1,
        group[static_cast<std::size_t>(closest)],
        distance(coordinates[static_cast<std::size_t>(closest)], substation)
    });
    return edges;
}

double priced_tree(
    const std::vector<Edge>& edges,
    int turbines,
    const Problem& problem
) {
    const int root = turbines;
    std::vector<std::vector<std::pair<int, int>>> adjacency(
        static_cast<std::size_t>(turbines + 1)
    );
    for (int index = 0; index < static_cast<int>(edges.size()); ++index) {
        const int lhs = edges[static_cast<std::size_t>(index)].from < 0
            ? root
            : edges[static_cast<std::size_t>(index)].from;
        const int rhs = edges[static_cast<std::size_t>(index)].to;
        adjacency[static_cast<std::size_t>(lhs)].push_back({rhs, index});
        adjacency[static_cast<std::size_t>(rhs)].push_back({lhs, index});
    }
    std::vector<int> parent(static_cast<std::size_t>(turbines + 1), -2);
    std::vector<int> parent_edge(static_cast<std::size_t>(turbines + 1), -1);
    std::vector<int> order{root};
    parent[static_cast<std::size_t>(root)] = -1;
    for (std::size_t position = 0; position < order.size(); ++position) {
        const int node = order[position];
        for (const auto& [next, edge] :
             adjacency[static_cast<std::size_t>(node)]) {
            if (parent[static_cast<std::size_t>(next)] == -2) {
                parent[static_cast<std::size_t>(next)] = node;
                parent_edge[static_cast<std::size_t>(next)] = edge;
                order.push_back(next);
            }
        }
    }
    std::vector<int> subtree(static_cast<std::size_t>(turbines + 1), 1);
    subtree[static_cast<std::size_t>(root)] = 0;
    double cost = 0.0;
    for (auto iterator = order.rbegin(); iterator != order.rend(); ++iterator) {
        const int node = *iterator;
        if (node == root) {
            continue;
        }
        const int load = subtree[static_cast<std::size_t>(node)];
        std::size_t type = 0;
        while (type + 1 < problem.cable_capacity.size()
               && load > problem.cable_capacity[type]) {
            ++type;
        }
        if (load > problem.cable_capacity.back()) {
            return std::numeric_limits<double>::infinity();
        }
        cost += edges[static_cast<std::size_t>(
            parent_edge[static_cast<std::size_t>(node)]
        )].length * problem.cable_price[type];
        const int parent_node = parent[static_cast<std::size_t>(node)];
        subtree[static_cast<std::size_t>(parent_node)] += load;
    }
    return cost;
}

double route_bsr(
    const std::vector<Point>& coordinates,
    const Problem& problem
) {
    const int turbines = static_cast<int>(coordinates.size());
    const int capacity = problem.cable_capacity.back();
    std::vector<int> sorted(static_cast<std::size_t>(turbines));
    std::iota(sorted.begin(), sorted.end(), 0);
    std::stable_sort(sorted.begin(), sorted.end(), [&](int lhs, int rhs) {
        const Point& left = coordinates[static_cast<std::size_t>(lhs)];
        const Point& right = coordinates[static_cast<std::size_t>(rhs)];
        const double left_angle = std::atan2(
            left.y - problem.substation.y,
            left.x - problem.substation.x
        );
        const double right_angle = std::atan2(
            right.y - problem.substation.y,
            right.x - problem.substation.x
        );
        return left_angle < right_angle;
    });
    const int groups = (turbines + capacity - 1) / capacity;
    double best = std::numeric_limits<double>::infinity();
    for (int start = 0; start < capacity; ++start) {
        std::vector<int> order;
        order.reserve(static_cast<std::size_t>(turbines));
        for (int offset = 0; offset < turbines; ++offset) {
            order.push_back(sorted[static_cast<std::size_t>(
                (start + offset) % turbines
            )]);
        }
        std::vector<Edge> all_edges;
        for (int group_index = 0; group_index < groups; ++group_index) {
            const int begin = static_cast<int>(std::llround(
                static_cast<double>(group_index * turbines)
                / static_cast<double>(groups)
            ));
            const int end = static_cast<int>(std::llround(
                static_cast<double>((group_index + 1) * turbines)
                / static_cast<double>(groups)
            ));
            std::vector<int> members(
                order.begin() + begin,
                order.begin() + end
            );
            std::vector<Point> points;
            for (const int member : members) {
                points.push_back(
                    coordinates[static_cast<std::size_t>(member)]
                );
            }
            auto edges = group_tree(
                members,
                points,
                problem.substation
            );
            all_edges.insert(all_edges.end(), edges.begin(), edges.end());
        }
        best = std::min(
            best,
            priced_tree(all_edges, turbines, problem)
        );
    }
    return best;
}

struct Evaluation {
    double lcoe = std::numeric_limits<double>::infinity();
    double capacity_factor = 0.0;
    double aep_kwh = 0.0;
    double cable_cost = 0.0;
};

Evaluation evaluate(
    const std::vector<int>& layout,
    const Problem& problem,
    bool include_cost = true
) {
    std::vector<Point> coordinates;
    coordinates.reserve(layout.size());
    for (const int index : layout) {
        coordinates.push_back(
            problem.candidates[static_cast<std::size_t>(index)]
        );
    }
    const double cable_cost =
        include_cost ? route_bsr(coordinates, problem) : 0.0;
    double expected_power_kw = 0.0;
    const double rotor_radius = problem.rotor_diameter_m / 2.0;
    for (int direction = 0;
         direction < static_cast<int>(problem.theta.size());
         ++direction) {
        const double angle = problem.theta[
            static_cast<std::size_t>(direction)
        ];
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        std::vector<Point> rotated(coordinates.size());
        for (int index = 0; index < static_cast<int>(coordinates.size());
             ++index) {
            const Point& point =
                coordinates[static_cast<std::size_t>(index)];
            rotated[static_cast<std::size_t>(index)] = {
                cosine * point.x - sine * point.y,
                sine * point.x + cosine * point.y
            };
        }
        std::vector<int> order(coordinates.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs) {
            return rotated[static_cast<std::size_t>(lhs)].y
                > rotated[static_cast<std::size_t>(rhs)].y;
        });
        for (int speed_index = 0;
             speed_index < static_cast<int>(problem.velocity.size());
             ++speed_index) {
            const double free_speed = problem.velocity[
                static_cast<std::size_t>(speed_index)
            ];
            const double ct = std::clamp(
                interpolate(problem.curve, free_speed, false),
                0.0,
                0.999999
            );
            const double axial =
                0.5 * (1.0 - std::sqrt(1.0 - ct));
            std::vector<double> effective(
                coordinates.size(), free_speed
            );
            for (int downstream_position = 1;
                 downstream_position < static_cast<int>(order.size());
                 ++downstream_position) {
                const int downstream = order[static_cast<std::size_t>(
                    downstream_position
                )];
                double squared_deficit = 0.0;
                for (int upstream_position = 0;
                     upstream_position < downstream_position;
                     ++upstream_position) {
                    const int upstream = order[static_cast<std::size_t>(
                        upstream_position
                    )];
                    const double along =
                        rotated[static_cast<std::size_t>(upstream)].y
                        - rotated[static_cast<std::size_t>(downstream)].y;
                    const double cross = std::abs(
                        rotated[static_cast<std::size_t>(downstream)].x
                        - rotated[static_cast<std::size_t>(upstream)].x
                    );
                    const double wake_radius =
                        rotor_radius + 0.1 * along;
                    if (along > 0.0 && cross < wake_radius) {
                        const double deficit =
                            2.0 * axial
                            * std::pow(rotor_radius / wake_radius, 2.0);
                        squared_deficit += deficit * deficit;
                    }
                }
                effective[static_cast<std::size_t>(downstream)] =
                    free_speed
                    * std::max(0.0, 1.0 - std::sqrt(squared_deficit));
            }
            double farm_power = 0.0;
            for (const double speed : effective) {
                if (speed >= problem.cut_in_mps
                    && speed <= problem.cut_out_mps) {
                    farm_power += spline_power(problem, speed);
                }
            }
            const std::size_t probability_index =
                static_cast<std::size_t>(
                    direction * static_cast<int>(problem.velocity.size())
                    + speed_index
                );
            expected_power_kw +=
                farm_power * problem.probability[probability_index];
        }
    }
    const double aep = expected_power_kw * 365.0 * 24.0;
    const double capacity_factor =
        aep
        / (
            365.0 * 24.0 * problem.turbine_pinst_kw
            * static_cast<double>(problem.turbine_count)
        );
    if (!include_cost) {
        return {
            std::numeric_limits<double>::infinity(),
            capacity_factor,
            aep,
            0.0
        };
    }
    const double power_mw = problem.turbine_pinst_kw / 1000.0;
    const double installed_mw =
        power_mw * static_cast<double>(problem.turbine_count);
    const double c_w = 2.95e3 * std::log(power_mw) - 375.2;
    const double c_ist = 0.113 * c_w;
    const double c_f =
        320.0 * power_mw
        * (1.0 + 0.02 * (problem.sea_depth_m - 8.0))
        * (
            1.0
            + 0.8e-6
                * (
                    problem.hub_height_m
                        * std::pow(problem.rotor_diameter_m / 2.0, 2.0)
                    - 1.0e5
                )
        );
    const double turbine_cost =
        (c_w + c_ist + 1.5 * c_f)
        * static_cast<double>(problem.turbine_count);
    const double cable_total =
        (
            cable_cost
            + problem.offshore_length_m * problem.export_cable_price
        ) / 1000.0;
    const double substation_cost =
        539.0 * std::pow(installed_mw, 0.678)
        + 87.250 * installed_mw;
    constexpr std::array<double, 9> cost_per_mw{
        7, 92, 88, 52, 144, 130, 89, 109, 365
    };
    constexpr std::array<double, 9> historical_power{
        495, 120, 108, 60, -1, 40, 40, -1, -1
    };
    constexpr std::array<int, 9> years{
        2004, 2010, 2007, 2006, 2010, 2010, 2005, 2009, 2010
    };
    double weighted_cost = 0.0;
    double weight_sum = 0.0;
    for (std::size_t index = 0; index < cost_per_mw.size(); ++index) {
        if (historical_power[index] > 0.0) {
            const double weight =
                historical_power[index]
                / static_cast<double>(2024 - years[index]);
            weighted_cost += cost_per_mw[index] * weight;
            weight_sum += weight;
        }
    }
    const double development_cost =
        (weighted_cost / weight_sum) * installed_mw;
    const double decommissioning =
        0.0093 * c_w * static_cast<double>(problem.turbine_count);
    const double annual_opex = 78.2 * installed_mw;
    const double other =
        0.114 * (turbine_cost + substation_cost + cable_total)
        + 0.092 * annual_opex * 25.0;
    const double investment =
        turbine_cost + substation_cost + cable_total
        + development_cost + decommissioning + other;
    double discounted_cost = investment;
    double discounted_energy = 0.0;
    for (int year = 1; year <= 25; ++year) {
        const double discount = std::pow(1.05, year);
        discounted_cost += annual_opex / discount;
        discounted_energy += aep / discount;
    }
    return {
        discounted_cost / discounted_energy * 1000.0,
        capacity_factor,
        aep,
        cable_cost
    };
}

std::vector<int> random_layout(
    const Problem& problem,
    const fode::CounterRng& rng,
    std::uint64_t phase,
    int individual
) {
    std::vector<std::pair<double, int>> keyed;
    keyed.reserve(problem.candidates.size());
    for (int candidate = 0;
         candidate < static_cast<int>(problem.candidates.size());
         ++candidate) {
        keyed.emplace_back(
            rng.uniform(
                0,
                phase,
                static_cast<std::uint64_t>(individual),
                static_cast<std::uint64_t>(candidate)
            ),
            candidate
        );
    }
    std::stable_sort(keyed.begin(), keyed.end());
    std::vector<int> layout;
    for (int index = 0; index < problem.turbine_count; ++index) {
        layout.push_back(keyed[static_cast<std::size_t>(index)].second);
    }
    std::sort(layout.begin(), layout.end());
    return layout;
}

void repair(
    std::vector<int>& layout,
    int required,
    int candidates,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t phase,
    int individual
) {
    std::sort(layout.begin(), layout.end());
    layout.erase(std::unique(layout.begin(), layout.end()), layout.end());
    layout.erase(
        std::remove_if(
            layout.begin(), layout.end(),
            [&](int value) { return value < 0 || value >= candidates; }
        ),
        layout.end()
    );
    std::vector<char> used(static_cast<std::size_t>(candidates), 0);
    for (const int value : layout) {
        used[static_cast<std::size_t>(value)] = 1;
    }
    std::uint64_t draw = 0;
    while (static_cast<int>(layout.size()) < required) {
        const int value = rng.integer(
            0,
            candidates,
            generation,
            phase,
            static_cast<std::uint64_t>(individual),
            0,
            draw++
        );
        if (used[static_cast<std::size_t>(value)] == 0) {
            used[static_cast<std::size_t>(value)] = 1;
            layout.push_back(value);
        }
    }
    while (static_cast<int>(layout.size()) > required) {
        const int position = rng.integer(
            0,
            static_cast<int>(layout.size()),
            generation,
            phase + 1,
            static_cast<std::uint64_t>(individual),
            0,
            draw++
        );
        layout.erase(layout.begin() + position);
    }
    std::sort(layout.begin(), layout.end());
}

struct RunResult {
    Evaluation best;
    std::vector<int> layout;
    std::uint64_t fes = 0;
    std::uint64_t generations = 0;
    double total_seconds = 0.0;
    double evaluator_seconds = 0.0;
};

RunResult optimize(
    const Problem& problem,
    std::uint64_t budget,
    std::uint64_t seed,
    int workers
) {
    const auto started = Clock::now();
    constexpr int population_size = 30;
    if (budget < population_size) {
        throw std::runtime_error("GGA budget is below initialization");
    }
    fode::PersistentExecutor executor(workers);
    fode::CounterRng rng(seed ^ 0x474741ULL);
    std::vector<std::vector<int>> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](int individual) {
        population[static_cast<std::size_t>(individual)] =
            random_layout(problem, rng, 2000, individual);
    });
    std::vector<Evaluation> values(static_cast<std::size_t>(population_size));
    double evaluator_seconds = 0.0;
    auto evaluate_batch = [&](const std::vector<std::vector<int>>& layouts,
                              std::vector<Evaluation>& output,
                              int count) {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, count, [&](int row) {
            output[static_cast<std::size_t>(row)] = evaluate(
                layouts[static_cast<std::size_t>(row)], problem
            );
        });
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - evaluation_started
        ).count();
    };
    evaluate_batch(population, values, population_size);
    std::uint64_t fes = population_size;
    std::uint64_t generation = 0;
    while (fes < budget) {
        ++generation;
        const int count = static_cast<int>(std::min<std::uint64_t>(
            population_size, budget - fes
        ));
        std::vector<double> scores(static_cast<std::size_t>(population_size));
        const double worst = std::max_element(
            values.begin(), values.end(),
            [](const Evaluation& lhs, const Evaluation& rhs) {
                return lhs.lcoe < rhs.lcoe;
            }
        )->lcoe;
        double score_sum = 0.0;
        for (int row = 0; row < population_size; ++row) {
            scores[static_cast<std::size_t>(row)] =
                worst - values[static_cast<std::size_t>(row)].lcoe + 1e-8;
            score_sum += scores[static_cast<std::size_t>(row)];
        }
        auto select_parent = [&](int child, int which) {
            const double threshold = rng.uniform(
                generation,
                2001 + static_cast<std::uint64_t>(which),
                static_cast<std::uint64_t>(child)
            ) * score_sum;
            double cumulative = 0.0;
            for (int row = 0; row < population_size; ++row) {
                cumulative += scores[static_cast<std::size_t>(row)];
                if (threshold <= cumulative) {
                    return row;
                }
            }
            return population_size - 1;
        };
        std::vector<std::vector<int>> offspring(
            static_cast<std::size_t>(count)
        );
        executor.parallel_for(0, count, [&](int child_index) {
            int first = select_parent(child_index, 0);
            int second = select_parent(child_index, 1);
            if (first == second) {
                second = (second + 1) % population_size;
            }
            const double angle = rng.uniform(
                generation,
                2003,
                static_cast<std::uint64_t>(child_index)
            ) * std::numbers::pi;
            const double normal_x = std::cos(angle);
            const double normal_y = std::sin(angle);
            auto in_first_half = [&](int candidate) {
                const Point& point = problem.candidates[
                    static_cast<std::size_t>(candidate)
                ];
                return (
                    (point.x - problem.substation.x) * normal_x
                    + (point.y - problem.substation.y) * normal_y
                ) >= 0.0;
            };
            std::vector<int> child_a;
            std::vector<int> child_b;
            for (const int value :
                 population[static_cast<std::size_t>(first)]) {
                (in_first_half(value) ? child_a : child_b).push_back(value);
            }
            for (const int value :
                 population[static_cast<std::size_t>(second)]) {
                (in_first_half(value) ? child_b : child_a).push_back(value);
            }
            std::vector<int> child = rng.uniform(
                generation,
                2004,
                static_cast<std::uint64_t>(child_index)
            ) < 0.5 ? child_a : child_b;
            repair(
                child,
                problem.turbine_count,
                static_cast<int>(problem.candidates.size()),
                rng,
                generation,
                2005,
                child_index
            );
            if (rng.uniform(
                    generation,
                    2007,
                    static_cast<std::uint64_t>(child_index)
                ) < 0.5) {
                const int position = rng.integer(
                    0,
                    problem.turbine_count,
                    generation,
                    2008,
                    static_cast<std::uint64_t>(child_index)
                );
                child.erase(child.begin() + position);
                repair(
                    child,
                    problem.turbine_count,
                    static_cast<int>(problem.candidates.size()),
                    rng,
                    generation,
                    2009,
                    child_index
                );
            }
            offspring[static_cast<std::size_t>(child_index)] =
                std::move(child);
        });
        std::vector<Evaluation> offspring_values(
            static_cast<std::size_t>(count)
        );
        evaluate_batch(offspring, offspring_values, count);
        for (int row = 0; row < count; ++row) {
            if (offspring_values[static_cast<std::size_t>(row)].lcoe
                < values[static_cast<std::size_t>(row)].lcoe) {
                population[static_cast<std::size_t>(row)] =
                    std::move(offspring[static_cast<std::size_t>(row)]);
                values[static_cast<std::size_t>(row)] =
                    offspring_values[static_cast<std::size_t>(row)];
            }
        }
        fes += static_cast<std::uint64_t>(count);
    }
    const int best = static_cast<int>(std::distance(
        values.begin(),
        std::min_element(
            values.begin(), values.end(),
            [](const Evaluation& lhs, const Evaluation& rhs) {
                return lhs.lcoe < rhs.lcoe;
            }
        )
    ));
    RunResult result;
    result.best = values[static_cast<std::size_t>(best)];
    result.layout = population[static_cast<std::size_t>(best)];
    result.fes = fes;
    result.generations = generation;
    result.total_seconds = std::chrono::duration<double>(
        Clock::now() - started
    ).count();
    result.evaluator_seconds = evaluator_seconds;
    return result;
}

RunResult optimize_geoga(
    const Problem& problem,
    std::uint64_t budget,
    std::uint64_t seed,
    int workers
) {
    const auto started = Clock::now();
    constexpr int population_size = 50;
    constexpr int nearest_neighbors = 5;
    if (budget < population_size) {
        throw std::runtime_error(
            "GeoGA budget is below its 50-layout initialization"
        );
    }
    fode::PersistentExecutor executor(workers);
    fode::CounterRng rng(seed ^ 0x47454f4741ULL);
    std::vector<std::vector<int>> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](int individual) {
        population[static_cast<std::size_t>(individual)] =
            random_layout(problem, rng, 3000, individual);
    });
    std::vector<Evaluation> values(static_cast<std::size_t>(population_size));
    double evaluator_seconds = 0.0;
    auto evaluate_batch = [&](const std::vector<std::vector<int>>& layouts,
                              std::vector<Evaluation>& output,
                              int count) {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, count, [&](int row) {
            output[static_cast<std::size_t>(row)] = evaluate(
                layouts[static_cast<std::size_t>(row)], problem, false
            );
        });
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - evaluation_started
        ).count();
    };
    evaluate_batch(population, values, population_size);
    std::uint64_t fes = population_size;
    std::uint64_t generation = 0;
    while (fes < budget) {
        ++generation;
        const int count = static_cast<int>(std::min<std::uint64_t>(
            population_size, budget - fes
        ));
        double score_sum = 0.0;
        for (const Evaluation& value : values) {
            score_sum += std::max(value.aep_kwh, 0.0);
        }
        auto select_parent = [&](int child, int which) {
            if (!(score_sum > 0.0)) {
                return rng.integer(
                    0,
                    population_size,
                    generation,
                    3001 + static_cast<std::uint64_t>(which),
                    static_cast<std::uint64_t>(child)
                );
            }
            const double threshold = rng.uniform(
                generation,
                3001 + static_cast<std::uint64_t>(which),
                static_cast<std::uint64_t>(child)
            ) * score_sum;
            double cumulative = 0.0;
            for (int row = 0; row < population_size; ++row) {
                cumulative += std::max(
                    values[static_cast<std::size_t>(row)].aep_kwh,
                    0.0
                );
                if (threshold <= cumulative) {
                    return row;
                }
            }
            return population_size - 1;
        };
        std::vector<std::vector<int>> offspring(
            static_cast<std::size_t>(count)
        );
        executor.parallel_for(0, count, [&](int child_index) {
            int first = select_parent(child_index, 0);
            int second = select_parent(child_index, 1);
            if (first == second) {
                second = (second + 1) % population_size;
            }
            const int cut = rng.integer(
                1,
                problem.turbine_count,
                generation,
                3003,
                static_cast<std::uint64_t>(child_index)
            );
            std::vector<int> child;
            child.reserve(static_cast<std::size_t>(problem.turbine_count));
            const auto& first_parent =
                population[static_cast<std::size_t>(first)];
            const auto& second_parent =
                population[static_cast<std::size_t>(second)];
            child.insert(
                child.end(),
                first_parent.begin(),
                first_parent.begin() + cut
            );
            child.insert(
                child.end(),
                second_parent.begin() + cut,
                second_parent.end()
            );
            repair(
                child,
                problem.turbine_count,
                static_cast<int>(problem.candidates.size()),
                rng,
                generation,
                3004,
                child_index
            );

            const int position = rng.integer(
                0,
                problem.turbine_count,
                generation,
                3006,
                static_cast<std::uint64_t>(child_index)
            );
            const int current = child[static_cast<std::size_t>(position)];
            std::vector<char> used(problem.candidates.size(), 0);
            for (const int candidate : child) {
                used[static_cast<std::size_t>(candidate)] = 1;
            }
            std::vector<std::pair<double, int>> nearest;
            nearest.reserve(
                problem.candidates.size()
                - static_cast<std::size_t>(problem.turbine_count)
            );
            const Point& center =
                problem.candidates[static_cast<std::size_t>(current)];
            for (int candidate = 0;
                 candidate < static_cast<int>(problem.candidates.size());
                 ++candidate) {
                if (used[static_cast<std::size_t>(candidate)] != 0) {
                    continue;
                }
                const Point& point =
                    problem.candidates[static_cast<std::size_t>(candidate)];
                const double dx = point.x - center.x;
                const double dy = point.y - center.y;
                nearest.emplace_back(dx * dx + dy * dy, candidate);
            }
            std::stable_sort(
                nearest.begin(),
                nearest.end(),
                [](const auto& lhs, const auto& rhs) {
                    if (lhs.first != rhs.first) {
                        return lhs.first < rhs.first;
                    }
                    return lhs.second < rhs.second;
                }
            );
            if (!nearest.empty()) {
                const int choices = std::min(
                    nearest_neighbors,
                    static_cast<int>(nearest.size())
                );
                const int selected = rng.integer(
                    0,
                    choices,
                    generation,
                    3007,
                    static_cast<std::uint64_t>(child_index)
                );
                child[static_cast<std::size_t>(position)] =
                    nearest[static_cast<std::size_t>(selected)].second;
            }
            std::sort(child.begin(), child.end());
            offspring[static_cast<std::size_t>(child_index)] =
                std::move(child);
        });
        std::vector<Evaluation> offspring_values(
            static_cast<std::size_t>(count)
        );
        evaluate_batch(offspring, offspring_values, count);

        struct Candidate {
            Evaluation value;
            std::vector<int> layout;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(
            static_cast<std::size_t>(population_size + count)
        );
        for (int row = 0; row < population_size; ++row) {
            candidates.push_back({
                values[static_cast<std::size_t>(row)],
                population[static_cast<std::size_t>(row)]
            });
        }
        for (int row = 0; row < count; ++row) {
            candidates.push_back({
                offspring_values[static_cast<std::size_t>(row)],
                std::move(offspring[static_cast<std::size_t>(row)])
            });
        }
        std::stable_sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
                if (lhs.value.aep_kwh != rhs.value.aep_kwh) {
                    return lhs.value.aep_kwh > rhs.value.aep_kwh;
                }
                return lhs.layout < rhs.layout;
            }
        );
        for (int row = 0; row < population_size; ++row) {
            values[static_cast<std::size_t>(row)] =
                candidates[static_cast<std::size_t>(row)].value;
            population[static_cast<std::size_t>(row)] =
                std::move(candidates[static_cast<std::size_t>(row)].layout);
        }
        fes += static_cast<std::uint64_t>(count);
    }
    const int best = static_cast<int>(std::distance(
        values.begin(),
        std::max_element(
            values.begin(), values.end(),
            [](const Evaluation& lhs, const Evaluation& rhs) {
                return lhs.aep_kwh < rhs.aep_kwh;
            }
        )
    ));
    RunResult result;
    result.best = values[static_cast<std::size_t>(best)];
    result.layout = population[static_cast<std::size_t>(best)];
    result.fes = fes;
    result.generations = generation;
    result.total_seconds = std::chrono::duration<double>(
        Clock::now() - started
    ).count();
    result.evaluator_seconds = evaluator_seconds;
    return result;
}

struct Arguments {
    std::filesystem::path problem;
    std::filesystem::path output;
    std::string layout_spec;
    std::string algorithm = "gga";
    std::uint64_t seed = 20260316;
    std::uint64_t physical_fes = 3000;
    int workers = 20;
    bool help = false;
};

Arguments parse_arguments(int argc, char** argv) {
    Arguments result;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        auto value = [&]() -> std::string {
            if (++index >= argc) {
                throw std::invalid_argument("missing value for " + flag);
            }
            return argv[index];
        };
        if (flag == "--problem") {
            result.problem = value();
        } else if (flag == "--output") {
            result.output = value();
        } else if (flag == "--seed") {
            result.seed = std::stoull(value());
        } else if (flag == "--physical-fes") {
            result.physical_fes = std::stoull(value());
        } else if (flag == "--workers") {
            result.workers = std::stoi(value());
        } else if (flag == "--evaluate-layout") {
            result.layout_spec = value();
        } else if (flag == "--algorithm") {
            result.algorithm = value();
        } else if (flag == "--help") {
            result.help = true;
        } else {
            throw std::invalid_argument("unknown argument: " + flag);
        }
    }
    if (result.algorithm != "gga" && result.algorithm != "geoga") {
        throw std::invalid_argument(
            "--algorithm must be gga or geoga"
        );
    }
    return result;
}

std::vector<int> parse_layout(const std::string& specification) {
    std::vector<int> layout;
    std::istringstream input(specification);
    std::string token;
    while (std::getline(input, token, ',')) {
        if (token.empty()) {
            throw std::invalid_argument("empty layout index");
        }
        layout.push_back(std::stoi(token));
    }
    return layout;
}

std::string to_json(
    const Problem& problem,
    const Arguments& arguments,
    const RunResult& result
) {
    std::ostringstream output;
    const bool evaluation_only = !arguments.layout_spec.empty();
    const bool geoga = arguments.algorithm == "geoga";
    output << std::setprecision(17);
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"algorithm_id\": \"" << arguments.algorithm << "\",\n"
           << "  \"method_id\": \""
           << (
               evaluation_only
                   ? (
                       geoga
                           ? "GEOGA_DECLARED_RECONSTRUCTION_EVALUATOR"
                           : "GGA_CPP_REPAIRED_EVALUATOR"
                   )
                   : (
                       geoga
                           ? "GEOGA_DECLARED_RECONSTRUCTION_V1"
                           : "GGA_CPP_HPC_FULL"
                   )
           ) << "\",\n"
           << "  \"run_mode\": \""
           << (evaluation_only ? "evaluate_layout" : "optimization")
           << "\",\n"
           << "  \"problem_id\": \""
           << (
               geoga
                   ? "admitted_gga_problem_asset_proxy"
                   : "gga2026_layout_cable"
           ) << "\",\n"
           << "  \"problem_semantics_id\": \"" << problem.profile << "\",\n"
           << "  \"case_id\": \"" << problem.case_id << "\",\n"
           << "  \"seed\": " << arguments.seed << ",\n"
           << "  \"physical_fes\": " << result.fes << ",\n"
           << "  \"generations\": " << result.generations << ",\n"
           << "  \"population_size\": "
           << (evaluation_only ? 0 : (geoga ? 50 : 30)) << ",\n"
           << "  \"requested_workers\": " << arguments.workers << ",\n"
           << "  \"best_lcoe\": ";
    if (std::isfinite(result.best.lcoe)) {
        output << result.best.lcoe;
    } else {
        output << "null";
    }
    output << ",\n"
           << "  \"best_capacity_factor\": "
           << result.best.capacity_factor << ",\n"
           << "  \"best_aep_kwh\": " << result.best.aep_kwh << ",\n"
           << "  \"best_cable_cost\": " << result.best.cable_cost << ",\n"
           << "  \"best_layout_0based\": [";
    for (std::size_t index = 0; index < result.layout.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << result.layout[index];
    }
    output << "],\n"
           << "  \"timing_seconds\": {\n"
           << "    \"end_to_end\": " << result.total_seconds << ",\n"
           << "    \"evaluator\": " << result.evaluator_seconds << ",\n"
           << "    \"algorithm\": "
           << std::max(
               0.0, result.total_seconds - result.evaluator_seconds
           ) << "\n"
           << "  }\n"
           << "}\n";
    return output.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.help) {
            std::cout
                << "gga_cpp_hpc --problem CASE.wfp [--physical-fes N] "
                << "[--workers N] [--seed N] [--output FILE] "
                << "[--algorithm gga|geoga] "
                << "[--evaluate-layout i0,i1,...]\n";
            return 0;
        }
        if (arguments.problem.empty() || arguments.workers <= 0
            || arguments.physical_fes == 0) {
            throw std::invalid_argument(
                "--problem and positive work settings are required"
            );
        }
        const Problem problem = load_problem(arguments.problem);
        RunResult result;
        if (arguments.layout_spec.empty()) {
            result = arguments.algorithm == "geoga"
                ? optimize_geoga(
                    problem,
                    arguments.physical_fes,
                    arguments.seed,
                    arguments.workers
                )
                : optimize(
                    problem,
                    arguments.physical_fes,
                    arguments.seed,
                    arguments.workers
                );
        } else {
            result.layout = parse_layout(arguments.layout_spec);
            std::vector<int> sorted = result.layout;
            std::sort(sorted.begin(), sorted.end());
            if (static_cast<int>(sorted.size()) != problem.turbine_count
                || std::adjacent_find(sorted.begin(), sorted.end())
                    != sorted.end()
                || sorted.front() < 0
                || sorted.back()
                    >= static_cast<int>(problem.candidates.size())) {
                throw std::invalid_argument(
                    "evaluation layout must contain the required number "
                    "of unique in-range zero-based candidate indices"
                );
            }
            const auto started = Clock::now();
            result.best = evaluate(
                result.layout,
                problem,
                arguments.algorithm != "geoga"
            );
            result.evaluator_seconds = std::chrono::duration<double>(
                Clock::now() - started
            ).count();
            result.total_seconds = result.evaluator_seconds;
            result.fes = 1;
            result.generations = 0;
        }
        const std::string json = to_json(problem, arguments, result);
        if (arguments.output.empty()) {
            std::cout << json;
        } else {
            std::ofstream output(arguments.output);
            output << json;
        }
        std::cerr << arguments.algorithm << " " << problem.case_id
                  << " FES=" << result.fes
                  << " best_objective="
                  << (
                      arguments.algorithm == "geoga"
                          ? result.best.aep_kwh
                          : result.best.lcoe
                  )
                  << " seconds=" << result.total_seconds << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
