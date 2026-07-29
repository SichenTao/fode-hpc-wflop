/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T-MOEA Nysted same-author-asset reconstruction in the
shared pure-CPU GGA executable
Paper title: A Topology-Driven Multi-Objective Evolutionary Algorithm for
Offshore Wind Farm Layout Optimization
DOI: 10.1109/CPEEE69412.2026.11521465
Paper provides: Jensen wake Eqs. (2)-(3), the biobjective formulation,
NSGA-II selection, single-point crossover, topology mutation including the
Eq. (16) complement set, random swap, population 30, and 3000 fitness
evaluations.
Public author code URL: no T-MOEA source was found; same-author problem assets
come from https://github.com/zbh0528/WFLO-GGA
Public author code revision or archive hash:
6ce41326e6c1d3685a01e038baf6d1d07aa46126
Public code/assets provide: the related Nysted boundary, wind, turbine,
substation, cable arrays, and balanced-substation radial router.
Known missing information: original T-MOEA candidate set and seed, cable
decision transition or router, k, variation probabilities, duplicate repair,
parent-equality rule, tie rules, and reference front.
Reconstruction performed here: v1 preserves the admitted historical transition;
v2 corrects Eq. (16) to exclude every pre-mutation site and freezes all omitted
controls, exact physical FES, schedule-independent counter randomness, and CPU
execution receipts.
Method evidence tier: M3_DECLARED_COMPLETION.
Problem evidence tier: P2_CITATION_SAME_AUTHOR.
Method semantic ID: tmoea_nysted_gga_asset_reconstruction_v1 (historical) or
tmoea_nysted_gga_asset_reconstruction_paper_eq16_v2 (corrected R4).
Problem semantic ID: gga2026_layout_cable_repaired_v1 on the historical
nysted_gga_asset_reconstruction problem, or
tmoea_nysted_paper_wake_gga_router_problem_v1 on the corrected
nysted_tmoea_paper_wake_gga_router_reconstruction problem.
Controlling contracts:
shared/contracts/tmoea_nysted_reconstruction_execution_contract.json and
shared/contracts/tmoea_nysted_paper_eq16_r4_execution_contract.json
Claim boundary: both profiles are Nysted reconstructions using same-author GGA
assets; neither is the unavailable original T-MOEA experiment or reference
front.
Last evidence audit date: 2026-07-29
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
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
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

enum class EvaluationMode {
    Lcoe,
    AepOnly,
    AepAndCable,
    TmoeaAepAndCable
};

Evaluation evaluate(
    const std::vector<int>& layout,
    const Problem& problem,
    EvaluationMode mode = EvaluationMode::Lcoe
) {
    std::vector<Point> coordinates;
    coordinates.reserve(layout.size());
    for (const int index : layout) {
        coordinates.push_back(
            problem.candidates[static_cast<std::size_t>(index)]
        );
    }
    const double cable_cost = mode == EvaluationMode::AepOnly
        ? 0.0
        : route_bsr(coordinates, problem);
    const bool tmoea_wake =
        mode == EvaluationMode::TmoeaAepAndCable;
    const double tmoea_wake_expansion =
        0.5 / std::log(problem.hub_height_m / 0.0002);
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
                        rotor_radius
                        + (
                            tmoea_wake
                                ? tmoea_wake_expansion
                                : 0.1
                        ) * along;
                    if (along > 0.0 && cross < wake_radius) {
                        const double deficit = (
                            tmoea_wake
                                ? 2.0 / 3.0
                                : 2.0 * axial
                        ) * std::pow(rotor_radius / wake_radius, 2.0);
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
    if (mode != EvaluationMode::Lcoe) {
        return {
            std::numeric_limits<double>::infinity(),
            capacity_factor,
            aep,
            cable_cost
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

std::vector<int> random_ordered_layout(
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
    layout.reserve(static_cast<std::size_t>(problem.turbine_count));
    for (int index = 0; index < problem.turbine_count; ++index) {
        layout.push_back(keyed[static_cast<std::size_t>(index)].second);
    }
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

void repair_ordered(
    std::vector<int>& layout,
    int required,
    int candidates,
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t phase,
    int individual
) {
    std::vector<char> used(static_cast<std::size_t>(candidates), 0);
    std::vector<int> repaired;
    repaired.reserve(static_cast<std::size_t>(required));
    for (const int value : layout) {
        if (value >= 0 && value < candidates
            && used[static_cast<std::size_t>(value)] == 0
            && static_cast<int>(repaired.size()) < required) {
            used[static_cast<std::size_t>(value)] = 1;
            repaired.push_back(value);
        }
    }
    std::uint64_t draw = 0;
    while (static_cast<int>(repaired.size()) < required) {
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
            repaired.push_back(value);
        }
    }
    layout = std::move(repaired);
}

struct RunResult {
    struct FrontPoint {
        Evaluation value;
        std::vector<int> layout;
    };

    Evaluation best;
    std::vector<int> layout;
    std::vector<FrontPoint> front;
    std::uint64_t fes = 0;
    std::uint64_t generations = 0;
    int observed_workers = 0;
    double total_seconds = 0.0;
    double evaluator_seconds = 0.0;
    struct StageReceipt {
        double wall_seconds = 0.0;
        std::uint64_t parallel_regions = 0;
        std::uint64_t task_items = 0;
        std::uint64_t participant_activations = 0;
        int distinct_participants = 0;
        int peak_region_participants = 0;
    };
    struct TmoeaWorkReceipt {
        std::uint64_t complete_layout_evaluations = 0;
        std::uint64_t initial_layouts = 0;
        std::uint64_t offspring_layouts = 0;
        std::uint64_t ranked_individuals = 0;
        std::uint64_t tournament_candidates = 0;
        std::uint64_t parent_equality_resolutions = 0;
        std::uint64_t crossover_offspring = 0;
        std::uint64_t duplicate_repairs = 0;
        std::uint64_t topology_mutation_trials = 0;
        std::uint64_t topology_relocations = 0;
        std::uint64_t random_swap_trials = 0;
        std::uint64_t random_swaps = 0;
        std::uint64_t environmental_candidates = 0;
    };
    StageReceipt initialization_stage;
    StageReceipt variation_stage;
    StageReceipt evaluator_stage;
    StageReceipt selection_stage;
    TmoeaWorkReceipt tmoea_work;
    std::string final_population_hash;
    std::string nondominated_front_hash;
};

void add_stage(
    RunResult::StageReceipt& total,
    const RunResult::StageReceipt& addition
) {
    total.wall_seconds += addition.wall_seconds;
    total.parallel_regions += addition.parallel_regions;
    total.task_items += addition.task_items;
    total.participant_activations += addition.participant_activations;
    total.distinct_participants = std::max(
        total.distinct_participants,
        addition.distinct_participants
    );
    total.peak_region_participants = std::max(
        total.peak_region_participants,
        addition.peak_region_participants
    );
}

RunResult::StageReceipt stage_receipt(
    double seconds,
    const fode::PersistentExecutor& executor
) {
    const fode::ExecutorWorkReceipt source = executor.work_receipt();
    return {
        seconds,
        source.parallel_regions,
        source.task_items,
        source.participant_activations,
        source.distinct_participants,
        source.peak_region_participants
    };
}

std::string fnv1a64_text(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : text) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

std::string population_layout_hash(
    const std::vector<std::vector<int>>& population
) {
    std::ostringstream canonical;
    for (const auto& layout : population) {
        canonical << '[';
        for (const int candidate : layout) {
            canonical << candidate << ',';
        }
        canonical << ']';
    }
    return fnv1a64_text(canonical.str());
}

using BiObjectives = std::array<double, 2>;

BiObjectives tmoea_objectives(const Evaluation& value) {
    return {-value.aep_kwh, value.cable_cost};
}

bool bi_dominates(
    const BiObjectives& left,
    const BiObjectives& right
) {
    bool strictly_better = false;
    for (int objective = 0; objective < 2; ++objective) {
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

struct BiRankedPopulation {
    std::vector<int> rank;
    std::vector<double> crowding;
};

BiRankedPopulation bi_rank_and_crowding(
    const std::vector<Evaluation>& values
) {
    const int size = static_cast<int>(values.size());
    BiRankedPopulation result{
        std::vector<int>(static_cast<std::size_t>(size), -1),
        std::vector<double>(static_cast<std::size_t>(size), 0.0)
    };
    std::vector<BiObjectives> objectives;
    objectives.reserve(values.size());
    for (const Evaluation& value : values) {
        objectives.push_back(tmoea_objectives(value));
    }
    std::vector<int> dominated_count(static_cast<std::size_t>(size), 0);
    std::vector<std::vector<int>> dominates_set(
        static_cast<std::size_t>(size)
    );
    std::vector<std::vector<int>> fronts(1);
    for (int left = 0; left < size; ++left) {
        for (int right = left + 1; right < size; ++right) {
            if (bi_dominates(
                    objectives[static_cast<std::size_t>(left)],
                    objectives[static_cast<std::size_t>(right)])) {
                dominates_set[static_cast<std::size_t>(left)].push_back(
                    right
                );
                ++dominated_count[static_cast<std::size_t>(right)];
            } else if (bi_dominates(
                    objectives[static_cast<std::size_t>(right)],
                    objectives[static_cast<std::size_t>(left)])) {
                dominates_set[static_cast<std::size_t>(right)].push_back(
                    left
                );
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
        for (int objective = 0; objective < 2; ++objective) {
            std::vector<int> order = front;
            std::stable_sort(
                order.begin(),
                order.end(),
                [&](int left, int right) {
                    const double left_value =
                        objectives[static_cast<std::size_t>(left)]
                                  [static_cast<std::size_t>(objective)];
                    const double right_value =
                        objectives[static_cast<std::size_t>(right)]
                                  [static_cast<std::size_t>(objective)];
                    if (left_value != right_value) {
                        return left_value < right_value;
                    }
                    return left < right;
                }
            );
            result.crowding[static_cast<std::size_t>(order.front())] =
                std::numeric_limits<double>::infinity();
            result.crowding[static_cast<std::size_t>(order.back())] =
                std::numeric_limits<double>::infinity();
            const double minimum =
                objectives[static_cast<std::size_t>(order.front())]
                          [static_cast<std::size_t>(objective)];
            const double maximum =
                objectives[static_cast<std::size_t>(order.back())]
                          [static_cast<std::size_t>(objective)];
            if (maximum <= minimum || order.size() < 3) {
                continue;
            }
            for (std::size_t position = 1;
                 position + 1 < order.size();
                 ++position) {
                result.crowding[
                    static_cast<std::size_t>(order[position])
                ] += (
                    objectives[
                        static_cast<std::size_t>(order[position + 1])
                    ][static_cast<std::size_t>(objective)]
                    - objectives[
                        static_cast<std::size_t>(order[position - 1])
                    ][static_cast<std::size_t>(objective)]
                ) / (maximum - minimum);
            }
        }
    }
    return result;
}

int bi_tournament(
    int left,
    int right,
    const BiRankedPopulation& ranked
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

int tmoea_replacement_candidate(
    const std::vector<int>& pre_mutation_layout,
    int isolated,
    const Point& centroid,
    const Problem& problem,
    bool paper_equation_16
) {
    std::vector<char> used(problem.candidates.size(), 0);
    for (const int candidate : pre_mutation_layout) {
        used[static_cast<std::size_t>(candidate)] = 1;
    }
    if (!paper_equation_16) {
        used[static_cast<std::size_t>(
            pre_mutation_layout[static_cast<std::size_t>(isolated)]
        )] = 0;
    }
    std::vector<std::pair<double, int>> available;
    available.reserve(
        problem.candidates.size() - pre_mutation_layout.size()
        + (paper_equation_16 ? 0U : 1U)
    );
    for (int candidate = 0;
         candidate < static_cast<int>(problem.candidates.size());
         ++candidate) {
        if (used[static_cast<std::size_t>(candidate)] != 0) {
            continue;
        }
        const Point& point =
            problem.candidates[static_cast<std::size_t>(candidate)];
        available.emplace_back(
            std::hypot(point.x - centroid.x, point.y - centroid.y),
            candidate
        );
    }
    if (available.empty()) {
        throw std::runtime_error(
            "T-MOEA topology mutation has no Eq. (16) complement candidate"
        );
    }
    std::stable_sort(available.begin(), available.end());
    return available.front().second;
}

bool tmoea_topology_mutation(
    std::vector<int>& layout,
    const Problem& problem,
    bool paper_equation_16
) {
    constexpr int nearest_neighbors = 5;
    const int size = static_cast<int>(layout.size());
    std::vector<std::vector<int>> nearest(
        static_cast<std::size_t>(size)
    );
    for (int left = 0; left < size; ++left) {
        std::vector<std::pair<double, int>> distance;
        distance.reserve(static_cast<std::size_t>(size - 1));
        const Point& left_point = problem.candidates[
            static_cast<std::size_t>(
                layout[static_cast<std::size_t>(left)]
            )
        ];
        for (int right = 0; right < size; ++right) {
            if (left == right) {
                continue;
            }
            const Point& right_point = problem.candidates[
                static_cast<std::size_t>(
                    layout[static_cast<std::size_t>(right)]
                )
            ];
            const double dx = left_point.x - right_point.x;
            const double dy = left_point.y - right_point.y;
            distance.emplace_back(dx * dx + dy * dy, right);
        }
        std::stable_sort(distance.begin(), distance.end());
        const int count = std::min(
            nearest_neighbors,
            static_cast<int>(distance.size())
        );
        for (int neighbor = 0; neighbor < count; ++neighbor) {
            nearest[static_cast<std::size_t>(left)].push_back(
                distance[static_cast<std::size_t>(neighbor)].second
            );
        }
    }
    std::vector<int> degree(static_cast<std::size_t>(size), 0);
    for (int left = 0; left < size; ++left) {
        for (const int right : nearest[static_cast<std::size_t>(left)]) {
            ++degree[static_cast<std::size_t>(left)];
            if (std::find(
                    nearest[static_cast<std::size_t>(right)].begin(),
                    nearest[static_cast<std::size_t>(right)].end(),
                    left
                ) == nearest[static_cast<std::size_t>(right)].end()) {
                ++degree[static_cast<std::size_t>(right)];
            }
        }
    }
    Point centroid;
    for (const int candidate : layout) {
        const Point& point =
            problem.candidates[static_cast<std::size_t>(candidate)];
        centroid.x += point.x;
        centroid.y += point.y;
    }
    centroid.x /= static_cast<double>(size);
    centroid.y /= static_cast<double>(size);
    std::vector<double> centroid_distance(
        static_cast<std::size_t>(size), 0.0
    );
    double maximum_distance = 0.0;
    double minimum_distance = std::numeric_limits<double>::infinity();
    for (int index = 0; index < size; ++index) {
        const Point& point = problem.candidates[
            static_cast<std::size_t>(
                layout[static_cast<std::size_t>(index)]
            )
        ];
        centroid_distance[static_cast<std::size_t>(index)] = std::hypot(
            point.x - centroid.x,
            point.y - centroid.y
        );
        maximum_distance = std::max(
            maximum_distance,
            centroid_distance[static_cast<std::size_t>(index)]
        );
        minimum_distance = std::min(
            minimum_distance,
            centroid_distance[static_cast<std::size_t>(index)]
        );
    }
    const int maximum_degree =
        *std::max_element(degree.begin(), degree.end());
    int isolated = 0;
    double best_isolation = -1.0;
    for (int index = 0; index < size; ++index) {
        const double normalized_distance =
            maximum_distance > minimum_distance
                ? (
                    centroid_distance[static_cast<std::size_t>(index)]
                    - minimum_distance
                ) / (maximum_distance - minimum_distance)
                : 0.0;
        const double isolation =
            static_cast<double>(
                maximum_degree - degree[static_cast<std::size_t>(index)]
            ) + normalized_distance;
        if (isolation > best_isolation
            || (
                isolation == best_isolation
                && layout[static_cast<std::size_t>(index)]
                    < layout[static_cast<std::size_t>(isolated)]
            )) {
            isolated = index;
            best_isolation = isolation;
        }
    }
    const int previous = layout[static_cast<std::size_t>(isolated)];
    layout[static_cast<std::size_t>(isolated)] =
        tmoea_replacement_candidate(
            layout,
            isolated,
            centroid,
            problem,
            paper_equation_16
        );
    return layout[static_cast<std::size_t>(isolated)] != previous;
}

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
    result.observed_workers = executor.thread_count();
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
                layouts[static_cast<std::size_t>(row)],
                problem,
                EvaluationMode::AepOnly
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
    result.observed_workers = executor.thread_count();
    result.total_seconds = std::chrono::duration<double>(
        Clock::now() - started
    ).count();
    result.evaluator_seconds = evaluator_seconds;
    return result;
}

RunResult optimize_tmoea(
    const Problem& problem,
    std::uint64_t budget,
    std::uint64_t seed,
    int workers,
    bool paper_equation_16
) {
    const auto started = Clock::now();
    constexpr int population_size = 30;
    constexpr double swap_probability = 0.1;
    if (budget < population_size) {
        throw std::runtime_error(
            "T-MOEA budget is below its 30-layout initialization"
        );
    }
    if (static_cast<int>(problem.candidates.size())
        <= problem.turbine_count) {
        throw std::runtime_error(
            "T-MOEA requires at least one unoccupied candidate"
        );
    }
    fode::PersistentExecutor executor(workers);
    fode::CounterRng rng(seed ^ 0x544d4f4541ULL);
    std::vector<std::vector<int>> population(
        static_cast<std::size_t>(population_size)
    );
    executor.reset_work_receipt();
    const auto initialization_started = Clock::now();
    executor.parallel_for(0, population_size, [&](int individual) {
        population[static_cast<std::size_t>(individual)] =
            random_ordered_layout(problem, rng, 4000, individual);
    });
    const RunResult::StageReceipt initialization_stage = stage_receipt(
        std::chrono::duration<double>(
            Clock::now() - initialization_started
        ).count(),
        executor
    );
    std::vector<Evaluation> values(static_cast<std::size_t>(population_size));
    double evaluator_seconds = 0.0;
    RunResult::StageReceipt evaluator_stage;
    auto evaluate_batch = [&](const std::vector<std::vector<int>>& layouts,
                              std::vector<Evaluation>& output,
                              int count) {
        executor.reset_work_receipt();
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, count, [&](int row) {
            output[static_cast<std::size_t>(row)] = evaluate(
                layouts[static_cast<std::size_t>(row)],
                problem,
                EvaluationMode::TmoeaAepAndCable
            );
        });
        const double seconds = std::chrono::duration<double>(
            Clock::now() - evaluation_started
        ).count();
        evaluator_seconds += seconds;
        add_stage(evaluator_stage, stage_receipt(seconds, executor));
    };
    evaluate_batch(population, values, population_size);
    std::uint64_t fes = population_size;
    std::uint64_t generation = 0;
    RunResult::StageReceipt variation_stage;
    RunResult::StageReceipt selection_stage;
    RunResult::TmoeaWorkReceipt work;
    work.complete_layout_evaluations = population_size;
    work.initial_layouts = population_size;
    while (fes < budget) {
        ++generation;
        const int count = static_cast<int>(std::min<std::uint64_t>(
            population_size, budget - fes
        ));
        const auto parent_ranking_started = Clock::now();
        const BiRankedPopulation ranked = bi_rank_and_crowding(values);
        selection_stage.wall_seconds += std::chrono::duration<double>(
            Clock::now() - parent_ranking_started
        ).count();
        selection_stage.task_items += population_size;
        selection_stage.participant_activations += 1;
        selection_stage.distinct_participants = 1;
        selection_stage.peak_region_participants = 1;
        work.ranked_individuals += population_size;
        auto select_parent = [&](int child, int which) {
            const int first = rng.integer(
                0,
                population_size,
                generation,
                4001 + static_cast<std::uint64_t>(2 * which),
                static_cast<std::uint64_t>(child)
            );
            const int second = rng.integer(
                0,
                population_size,
                generation,
                4002 + static_cast<std::uint64_t>(2 * which),
                static_cast<std::uint64_t>(child)
            );
            return bi_tournament(first, second, ranked);
        };
        std::vector<std::vector<int>> offspring(
            static_cast<std::size_t>(count)
        );
        std::vector<char> parent_equality(
            static_cast<std::size_t>(count), 0
        );
        std::vector<char> topology_relocated(
            static_cast<std::size_t>(count), 0
        );
        std::vector<char> random_swapped(
            static_cast<std::size_t>(count), 0
        );
        executor.reset_work_receipt();
        const auto variation_started = Clock::now();
        executor.parallel_for(0, count, [&](int child_index) {
            int first = select_parent(child_index, 0);
            int second = select_parent(child_index, 1);
            if (first == second) {
                parent_equality[static_cast<std::size_t>(child_index)] = 1;
                second = (second + 1) % population_size;
            }
            const int cut = rng.integer(
                1,
                problem.turbine_count,
                generation,
                4005,
                static_cast<std::uint64_t>(child_index)
            );
            const auto& first_parent =
                population[static_cast<std::size_t>(first)];
            const auto& second_parent =
                population[static_cast<std::size_t>(second)];
            std::vector<int> child;
            child.reserve(static_cast<std::size_t>(problem.turbine_count));
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
            repair_ordered(
                child,
                problem.turbine_count,
                static_cast<int>(problem.candidates.size()),
                rng,
                generation,
                4006,
                child_index
            );
            topology_relocated[static_cast<std::size_t>(child_index)] =
                tmoea_topology_mutation(
                    child,
                    problem,
                    paper_equation_16
                ) ? 1 : 0;
            if (rng.uniform(
                    generation,
                    4009,
                    static_cast<std::uint64_t>(child_index)
                ) < swap_probability) {
                const int first_position = rng.integer(
                    0,
                    problem.turbine_count,
                    generation,
                    4010,
                    static_cast<std::uint64_t>(child_index)
                );
                int second_position = rng.integer(
                    0,
                    problem.turbine_count,
                    generation,
                    4011,
                    static_cast<std::uint64_t>(child_index)
                );
                if (first_position == second_position) {
                    second_position =
                        (second_position + 1) % problem.turbine_count;
                }
                std::swap(
                    child[static_cast<std::size_t>(first_position)],
                    child[static_cast<std::size_t>(second_position)]
                );
                random_swapped[static_cast<std::size_t>(child_index)] = 1;
            }
            offspring[static_cast<std::size_t>(child_index)] =
                std::move(child);
        });
        add_stage(
            variation_stage,
            stage_receipt(
                std::chrono::duration<double>(
                    Clock::now() - variation_started
                ).count(),
                executor
            )
        );
        work.offspring_layouts += static_cast<std::uint64_t>(count);
        work.tournament_candidates +=
            static_cast<std::uint64_t>(count) * 4U;
        work.parent_equality_resolutions += static_cast<std::uint64_t>(
            std::count(parent_equality.begin(), parent_equality.end(), 1)
        );
        work.crossover_offspring += static_cast<std::uint64_t>(count);
        work.duplicate_repairs += static_cast<std::uint64_t>(count);
        work.topology_mutation_trials += static_cast<std::uint64_t>(count);
        work.topology_relocations += static_cast<std::uint64_t>(
            std::count(
                topology_relocated.begin(),
                topology_relocated.end(),
                1
            )
        );
        work.random_swap_trials += static_cast<std::uint64_t>(count);
        work.random_swaps += static_cast<std::uint64_t>(
            std::count(random_swapped.begin(), random_swapped.end(), 1)
        );
        std::vector<Evaluation> offspring_values(
            static_cast<std::size_t>(count)
        );
        evaluate_batch(offspring, offspring_values, count);
        work.complete_layout_evaluations +=
            static_cast<std::uint64_t>(count);

        const auto environmental_selection_started = Clock::now();
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
        std::vector<Evaluation> combined_values;
        combined_values.reserve(candidates.size());
        for (const Candidate& candidate : candidates) {
            combined_values.push_back(candidate.value);
        }
        const BiRankedPopulation combined_ranked =
            bi_rank_and_crowding(combined_values);
        work.ranked_individuals +=
            static_cast<std::uint64_t>(population_size + count);
        work.environmental_candidates +=
            static_cast<std::uint64_t>(population_size + count);
        std::vector<int> order(candidates.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](int left, int right) {
                const int left_rank =
                    combined_ranked.rank[static_cast<std::size_t>(left)];
                const int right_rank =
                    combined_ranked.rank[static_cast<std::size_t>(right)];
                if (left_rank != right_rank) {
                    return left_rank < right_rank;
                }
                const double left_crowding =
                    combined_ranked.crowding[
                        static_cast<std::size_t>(left)
                    ];
                const double right_crowding =
                    combined_ranked.crowding[
                        static_cast<std::size_t>(right)
                    ];
                if (left_crowding != right_crowding) {
                    return left_crowding > right_crowding;
                }
                return candidates[static_cast<std::size_t>(left)].layout
                    < candidates[static_cast<std::size_t>(right)].layout;
            }
        );
        for (int row = 0; row < population_size; ++row) {
            const int selected = order[static_cast<std::size_t>(row)];
            values[static_cast<std::size_t>(row)] =
                candidates[static_cast<std::size_t>(selected)].value;
            population[static_cast<std::size_t>(row)] =
                std::move(
                    candidates[static_cast<std::size_t>(selected)].layout
                );
        }
        selection_stage.wall_seconds += std::chrono::duration<double>(
            Clock::now() - environmental_selection_started
        ).count();
        selection_stage.task_items +=
            static_cast<std::uint64_t>(population_size + count);
        selection_stage.participant_activations += 1;
        fes += static_cast<std::uint64_t>(count);
    }
    const auto final_ranking_started = Clock::now();
    const BiRankedPopulation final_ranked =
        bi_rank_and_crowding(values);
    std::vector<int> front_indices;
    for (int row = 0; row < population_size; ++row) {
        if (final_ranked.rank[static_cast<std::size_t>(row)] == 0) {
            front_indices.push_back(row);
        }
    }
    std::stable_sort(
        front_indices.begin(),
        front_indices.end(),
        [&](int left, int right) {
            const BiObjectives left_objectives = tmoea_objectives(
                values[static_cast<std::size_t>(left)]
            );
            const BiObjectives right_objectives = tmoea_objectives(
                values[static_cast<std::size_t>(right)]
            );
            if (left_objectives != right_objectives) {
                return left_objectives < right_objectives;
            }
            return population[static_cast<std::size_t>(left)]
                < population[static_cast<std::size_t>(right)];
        }
    );
    selection_stage.wall_seconds += std::chrono::duration<double>(
        Clock::now() - final_ranking_started
    ).count();
    selection_stage.task_items += population_size;
    selection_stage.participant_activations += 1;
    work.ranked_individuals += population_size;
    RunResult result;
    for (const int index : front_indices) {
        result.front.push_back({
            values[static_cast<std::size_t>(index)],
            population[static_cast<std::size_t>(index)]
        });
    }
    const int representative = front_indices.front();
    result.best = values[static_cast<std::size_t>(representative)];
    result.layout = population[static_cast<std::size_t>(representative)];
    result.fes = fes;
    result.generations = generation;
    result.observed_workers = executor.thread_count();
    result.initialization_stage = initialization_stage;
    result.variation_stage = variation_stage;
    result.evaluator_stage = evaluator_stage;
    result.selection_stage = selection_stage;
    result.tmoea_work = work;
    result.final_population_hash = population_layout_hash(population);
    std::ostringstream canonical_front;
    canonical_front << std::setprecision(17);
    for (const auto& member : result.front) {
        canonical_front << -member.value.aep_kwh << ','
                        << member.value.cable_cost << ':';
        for (const int candidate : member.layout) {
            canonical_front << candidate << ',';
        }
        canonical_front << ';';
    }
    result.nondominated_front_hash = fnv1a64_text(canonical_front.str());
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
    std::string tmoea_profile = "historical-v1";
    std::string execution_mode;
    std::uint64_t seed = 20260316;
    std::uint64_t physical_fes = 3000;
    int workers = 20;
    int resolved_workers = 20;
    bool workers_explicit = false;
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
            result.workers_explicit = true;
        } else if (flag == "--execution-mode") {
            result.execution_mode = value();
        } else if (flag == "--tmoea-profile") {
            result.tmoea_profile = value();
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
    if (result.algorithm != "gga"
        && result.algorithm != "geoga"
        && result.algorithm != "tmoea") {
        throw std::invalid_argument(
            "--algorithm must be gga, geoga, or tmoea"
        );
    }
    if (result.tmoea_profile != "historical-v1"
        && result.tmoea_profile != "paper-eq16-v2") {
        throw std::invalid_argument(
            "--tmoea-profile must be historical-v1 or paper-eq16-v2"
        );
    }
    if (result.algorithm != "tmoea"
        && result.tmoea_profile != "historical-v1") {
        throw std::invalid_argument(
            "--tmoea-profile is valid only with --algorithm tmoea"
        );
    }
    if (result.algorithm == "tmoea"
        && result.tmoea_profile == "paper-eq16-v2"
        && !result.workers_explicit) {
        result.workers = 0;
    }
    if (result.workers < 0) {
        throw std::invalid_argument("--workers cannot be negative");
    }
    if (result.workers == 0) {
        const unsigned int visible = std::thread::hardware_concurrency();
        result.resolved_workers =
            visible == 0U ? 1 : static_cast<int>(visible);
    } else {
        result.resolved_workers = result.workers;
    }
    if (result.execution_mode.empty()) {
        result.execution_mode =
            result.tmoea_profile == "paper-eq16-v2" ? "auto" : "cpu";
    }
    if (result.execution_mode == "hybrid"
        || result.execution_mode == "gpu") {
        throw std::invalid_argument(
            "execution mode " + result.execution_mode
            + " is unavailable; T-MOEA supports cpu and auto"
        );
    }
    if (result.execution_mode != "cpu"
        && result.execution_mode != "auto") {
        throw std::invalid_argument(
            "--execution-mode must be cpu, auto, hybrid, or gpu"
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

std::string stage_to_json(const RunResult::StageReceipt& stage) {
    std::ostringstream output;
    output << std::setprecision(17)
           << "{\"wall_seconds\":" << stage.wall_seconds
           << ",\"parallel_regions\":" << stage.parallel_regions
           << ",\"task_items\":" << stage.task_items
           << ",\"participant_activations\":"
           << stage.participant_activations
           << ",\"distinct_participants\":"
           << stage.distinct_participants
           << ",\"peak_region_participants\":"
           << stage.peak_region_participants << '}';
    return output.str();
}

std::string to_json(
    const Problem& problem,
    const Arguments& arguments,
    const RunResult& result
) {
    std::ostringstream output;
    const bool evaluation_only = !arguments.layout_spec.empty();
    const bool geoga = arguments.algorithm == "geoga";
    const bool tmoea = arguments.algorithm == "tmoea";
    const bool tmoea_v2 =
        tmoea && arguments.tmoea_profile == "paper-eq16-v2";
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
                           : (
                               tmoea
                                   ? (
                                       tmoea_v2
                                           ? "TMOEA_NYSTED_PAPER_WAKE_"
                                             "GGA_ROUTER_EVALUATOR_V1"
                                           : "TMOEA_NYSTED_"
                                             "RECONSTRUCTION_EVALUATOR"
                                   )
                                   : "GGA_CPP_REPAIRED_EVALUATOR"
                           )
                   )
                   : (
                       geoga
                           ? "GEOGA_DECLARED_RECONSTRUCTION_V1"
                           : (
                               tmoea
                                   ? (
                                       tmoea_v2
                                           ? "TMOEA_NYSTED_GGA_ASSET_"
                                             "RECONSTRUCTION_PAPER_EQ16_V2"
                                           : "TMOEA_NYSTED_GGA_ASSET_"
                                             "RECONSTRUCTION_V1"
                                   )
                                   : "GGA_CPP_HPC_FULL"
                           )
                   )
           ) << "\",\n"
           << "  \"run_mode\": \""
           << (evaluation_only ? "evaluate_layout" : "optimization")
           << "\",\n"
           << "  \"problem_id\": \""
           << (
               geoga
                   ? "admitted_gga_problem_asset_proxy"
                   : (
                       tmoea
                           ? (
                               tmoea_v2
                                   ? "nysted_tmoea_paper_wake_"
                                     "gga_router_reconstruction"
                                   : "nysted_gga_asset_reconstruction"
                           )
                           : "gga2026_layout_cable"
                   )
           ) << "\",\n"
           << "  \"problem_semantics_id\": \""
           << (
               tmoea_v2
                   ? "tmoea_nysted_paper_wake_gga_router_problem_v1"
                   : problem.profile
           ) << "\",\n"
           << "  \"case_id\": \"" << problem.case_id << "\",\n"
           << "  \"seed\": " << arguments.seed << ",\n"
           << "  \"physical_fes\": " << result.fes << ",\n"
           << "  \"generations\": " << result.generations << ",\n"
           << "  \"population_size\": "
           << (evaluation_only ? 0 : (geoga ? 50 : 30)) << ",\n"
           << "  \"requested_workers\": " << arguments.workers << ",\n"
           << "  \"observed_workers\": "
           << result.observed_workers << ",\n"
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
           << "  \"nondominated_count\": "
           << result.front.size() << ",\n"
           << "  \"front\": [";
    for (std::size_t point = 0; point < result.front.size(); ++point) {
        if (point != 0) {
            output << ",";
        }
        const auto& member = result.front[point];
        output << "\n    {\n"
               << "      \"objectives\": ["
               << -member.value.aep_kwh << ", "
               << member.value.cable_cost << "],\n"
               << "      \"aep_kwh\": " << member.value.aep_kwh << ",\n"
               << "      \"cable_cost\": "
               << member.value.cable_cost << ",\n"
               << "      \"layout_0based\": [";
        for (std::size_t index = 0;
             index < member.layout.size();
             ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << member.layout[index];
        }
        output << "]\n"
               << "    }";
    }
    if (!result.front.empty()) {
        output << "\n  ";
    }
    output << "],\n";
    if (tmoea_v2) {
        output
            << "  \"method_semantic_id\": "
               "\"tmoea_nysted_gga_asset_reconstruction_"
               "paper_eq16_v2\",\n"
            << "  \"execution_profile_id\": "
               "\"tmoea_nysted_paper_eq16_cpu_r4_v2\",\n"
            << "  \"problem_asset_sha256\": "
               "\"920cb61b0dc1415af4b6908252799d703c884f2eeaec9ba7b9590c42d40791c4\",\n"
            << "  \"requested_execution_mode\": \""
            << arguments.execution_mode << "\",\n"
            << "  \"resolved_execution_mode\": \"cpu\",\n"
            << "  \"resolved_workers\": "
            << arguments.resolved_workers << ",\n"
            << "  \"stage_receipts\": {\n"
            << "    \"initialization\": "
            << stage_to_json(result.initialization_stage) << ",\n"
            << "    \"variation_repair\": "
            << stage_to_json(result.variation_stage) << ",\n"
            << "    \"evaluator\": "
            << stage_to_json(result.evaluator_stage) << ",\n"
            << "    \"selection_serialization\": "
            << stage_to_json(result.selection_stage) << "\n"
            << "  },\n"
            << "  \"work_receipt\": {\n"
            << "    \"complete_layout_evaluations\": "
            << result.tmoea_work.complete_layout_evaluations << ",\n"
            << "    \"initial_layouts\": "
            << result.tmoea_work.initial_layouts << ",\n"
            << "    \"offspring_layouts\": "
            << result.tmoea_work.offspring_layouts << ",\n"
            << "    \"ranked_individuals\": "
            << result.tmoea_work.ranked_individuals << ",\n"
            << "    \"tournament_candidates\": "
            << result.tmoea_work.tournament_candidates << ",\n"
            << "    \"parent_equality_resolutions\": "
            << result.tmoea_work.parent_equality_resolutions << ",\n"
            << "    \"crossover_offspring\": "
            << result.tmoea_work.crossover_offspring << ",\n"
            << "    \"duplicate_repairs\": "
            << result.tmoea_work.duplicate_repairs << ",\n"
            << "    \"topology_mutation_trials\": "
            << result.tmoea_work.topology_mutation_trials << ",\n"
            << "    \"topology_relocations\": "
            << result.tmoea_work.topology_relocations << ",\n"
            << "    \"random_swap_trials\": "
            << result.tmoea_work.random_swap_trials << ",\n"
            << "    \"random_swaps\": "
            << result.tmoea_work.random_swaps << ",\n"
            << "    \"environmental_candidates\": "
            << result.tmoea_work.environmental_candidates << "\n"
            << "  },\n"
            << "  \"final_population_hash\": \""
            << result.final_population_hash << "\",\n"
            << "  \"nondominated_front_hash\": \""
            << result.nondominated_front_hash << "\",\n"
            << "  \"claim_boundary\": "
               "\"M3 declared completion on P2 same-author Nysted assets; "
               "not the unavailable original T-MOEA experiment or reference "
               "front\",\n";
    }
    output << "  \"timing_seconds\": {\n"
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

void write_atomic(
    const std::filesystem::path& destination,
    const std::string& contents
) {
    if (!destination.parent_path().empty()) {
        std::filesystem::create_directories(destination.parent_path());
    }
    std::filesystem::path temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            throw std::runtime_error(
                "cannot open temporary output: " + temporary.string()
            );
        }
        output << contents;
        output.flush();
        if (!output) {
            throw std::runtime_error(
                "cannot complete temporary output: "
                + temporary.string()
            );
        }
    }
    std::filesystem::rename(temporary, destination);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Arguments arguments = parse_arguments(argc, argv);
        if (arguments.help) {
            std::cout
                << "gga_cpp_hpc --problem CASE.wfp [--physical-fes N] "
                << "[--workers N] [--seed N] [--output FILE] "
                << "[--algorithm gga|geoga|tmoea] "
                << "[--tmoea-profile historical-v1|paper-eq16-v2] "
                << "[--execution-mode cpu|auto|hybrid|gpu] "
                << "[--evaluate-layout i0,i1,...]\n";
            return 0;
        }
        if (arguments.problem.empty() || arguments.resolved_workers <= 0
            || arguments.physical_fes == 0) {
            throw std::invalid_argument(
                "--problem and positive work settings are required"
            );
        }
        const Problem problem = load_problem(arguments.problem);
        RunResult result;
        if (arguments.layout_spec.empty()) {
            if (arguments.algorithm == "geoga") {
                result = optimize_geoga(
                    problem,
                    arguments.physical_fes,
                    arguments.seed,
                    arguments.resolved_workers
                );
            } else if (arguments.algorithm == "tmoea") {
                result = optimize_tmoea(
                    problem,
                    arguments.physical_fes,
                    arguments.seed,
                    arguments.resolved_workers,
                    arguments.tmoea_profile == "paper-eq16-v2"
                );
            } else {
                result = optimize(
                    problem,
                    arguments.physical_fes,
                    arguments.seed,
                    arguments.resolved_workers
                );
            }
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
                arguments.algorithm == "geoga"
                    ? EvaluationMode::AepOnly
                    : (
                        arguments.algorithm == "tmoea"
                            ? EvaluationMode::TmoeaAepAndCable
                            : EvaluationMode::Lcoe
                    )
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
            write_atomic(arguments.output, json);
        }
        std::cerr << arguments.algorithm << " " << problem.case_id
                  << " FES=" << result.fes
                  << " best_objective="
                  << (
                      arguments.algorithm == "geoga"
                          ? result.best.aep_kwh
                          : (
                              arguments.algorithm == "tmoea"
                                  ? -result.best.aep_kwh
                                  : result.best.lcoe
                          )
                  )
                  << " seconds=" << result.total_seconds << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
