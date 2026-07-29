/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: GeoGA Anholt-structured declared problem evaluator
Paper title: A Geometric Mutation-Based Genetic Algorithm for Irregular Large-Scale Offshore Wind Farm Layout Optimization
DOI: 10.1109/CBD69312.2025.00059
Paper-preserved fields: irregular domain, 111 turbines, approximately 180 candidates, 5D spacing, twelve joint wind bins, 4.2 MW/91.5 m/117 m/3--25 m/s turbine surface scalars, Jensen wake with root-sum-square overlap, and AEP
Declared P3 completions: synthetic polygon, target-capped Bridson sampling with seed 20250726 and L=30, frozen wind tuples, rated speed 12 m/s, cubic power curve, Ct=0.8, k=0.05, flow-to coordinate convention, and fixed source order
Problem evidence tier: P3_DECLARED_PROXY
Problem semantic ID: geoga_anholt_structured_declared_proxy_v1
Controlling contract: shared/contracts/geoga_anholt_structured_declared_proxy_contract.json
Claim boundary: actual Anholt coordinates, author candidate set, original wind/curve arrays, actual-layout AEP, and paper rankings remain unavailable and are not approximated as original data
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "geoga/problem.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace geoga {
namespace {

constexpr std::string_view kCandidateRule =
    "bridson_active_list_uniform_base_uniform_angle_uniform_radius_d_to_2d_"
    "fixed_acceptance_order_stop_at_target_180_seed_20250726_L30";
constexpr std::string_view kBoundaryRule =
    "declared_synthetic_irregular_polygon_local_meters";
constexpr std::string_view kPowerRule =
    "zero_below_cut_in_cubic_in_speed_cubed_to_rated_plateau_through_"
    "cut_out_zero_above";
constexpr std::string_view kWakeRule =
    "jensen_top_hat_constant_ct_root_sum_square_fractional_deficits_"
    "fixed_source_order";
constexpr std::string_view kObjectiveRule =
    "maximize_8760_times_probability_weighted_total_power_kwh";
constexpr std::string_view kCoordinateRule =
    "wind_angle_degrees_is_cartesian_flow_to_counterclockwise_from_positive_x";
constexpr std::string_view kActualLayoutBoundary =
    "blocked_unavailable_no_actual_layout_or_actual_aep_comparison";

std::string read_text(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open GeoGA case manifest: " + path);
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

std::size_t matching_delimiter(
    std::string_view text,
    std::size_t opening,
    char open,
    char close
) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = opening; index < text.size(); ++index) {
        const char character = text[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
        } else if (character == '"') {
            in_string = true;
        } else if (character == open) {
            ++depth;
        } else if (character == close) {
            --depth;
            if (depth == 0) {
                return index;
            }
        }
    }
    throw std::runtime_error("unterminated JSON value");
}

std::string_view raw_value(std::string_view object, std::string_view key) {
    const std::string marker = "\"" + std::string(key) + "\"";
    const std::size_t key_position = object.find(marker);
    if (key_position == std::string_view::npos) {
        throw std::runtime_error("missing manifest key: " + std::string(key));
    }
    const std::size_t colon = object.find(':', key_position + marker.size());
    if (colon == std::string_view::npos) {
        throw std::runtime_error("missing colon for key: " + std::string(key));
    }
    std::size_t begin = colon + 1;
    while (begin < object.size()
           && std::isspace(static_cast<unsigned char>(object[begin])) != 0) {
        ++begin;
    }
    if (begin >= object.size()) {
        throw std::runtime_error("missing value for key: " + std::string(key));
    }
    if (object[begin] == '[') {
        const std::size_t end =
            matching_delimiter(object, begin, '[', ']');
        return object.substr(begin, end - begin + 1);
    }
    if (object[begin] == '{') {
        const std::size_t end =
            matching_delimiter(object, begin, '{', '}');
        return object.substr(begin, end - begin + 1);
    }
    if (object[begin] == '"') {
        std::size_t end = begin + 1;
        while (end < object.size() && object[end] != '"') {
            if (object[end] == '\\') {
                ++end;
            }
            ++end;
        }
        return object.substr(begin, end - begin + 1);
    }
    std::size_t end = begin;
    while (end < object.size() && object[end] != ','
           && object[end] != '}' && object[end] != ']') {
        ++end;
    }
    return object.substr(begin, end - begin);
}

double number_value(std::string_view object, std::string_view key) {
    const std::string text(raw_value(object, key));
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    if (used == 0) {
        throw std::runtime_error("invalid number for key: " + std::string(key));
    }
    return value;
}

int integer_value(std::string_view object, std::string_view key) {
    return static_cast<int>(std::llround(number_value(object, key)));
}

std::uint64_t unsigned_value(
    std::string_view object,
    std::string_view key
) {
    const std::string text(raw_value(object, key));
    return std::stoull(text);
}

std::string string_value(std::string_view object, std::string_view key) {
    const std::string_view raw = raw_value(object, key);
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        throw std::runtime_error("invalid string for key: " + std::string(key));
    }
    return std::string(raw.substr(1, raw.size() - 2));
}

std::vector<double> all_numbers(std::string_view array) {
    std::vector<double> result;
    const char* cursor = array.data();
    const char* const end = array.data() + array.size();
    while (cursor < end) {
        if ((*cursor >= '0' && *cursor <= '9') || *cursor == '-'
            || *cursor == '+') {
            char* parsed_end = nullptr;
            const double value = std::strtod(cursor, &parsed_end);
            if (parsed_end != cursor) {
                result.push_back(value);
                cursor = parsed_end;
                continue;
            }
        }
        ++cursor;
    }
    return result;
}

std::vector<Point> parse_points(std::string_view array) {
    const std::vector<double> values = all_numbers(array);
    if (values.size() < 6 || values.size() % 2 != 0) {
        throw std::runtime_error("boundary must contain coordinate pairs");
    }
    std::vector<Point> result;
    result.reserve(values.size() / 2);
    for (std::size_t index = 0; index < values.size(); index += 2) {
        result.push_back({values[index], values[index + 1]});
    }
    return result;
}

std::vector<WindBin> parse_wind_bins(std::string_view array) {
    std::vector<WindBin> result;
    std::size_t position = 0;
    while (true) {
        const std::size_t opening = array.find('{', position);
        if (opening == std::string_view::npos) {
            break;
        }
        const std::size_t closing =
            matching_delimiter(array, opening, '{', '}');
        const std::string_view object =
            array.substr(opening, closing - opening + 1);
        result.push_back({
            number_value(object, "flow_to_degrees"),
            number_value(object, "free_speed_mps"),
            number_value(object, "probability")
        });
        position = closing + 1;
    }
    return result;
}

double squared_distance(const Point& left, const Point& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
}

std::vector<Point> generate_candidates(const Problem& problem) {
    double minimum_x = std::numeric_limits<double>::infinity();
    double maximum_x = -std::numeric_limits<double>::infinity();
    double minimum_y = std::numeric_limits<double>::infinity();
    double maximum_y = -std::numeric_limits<double>::infinity();
    for (const Point& point : problem.boundary) {
        minimum_x = std::min(minimum_x, point.x_m);
        maximum_x = std::max(maximum_x, point.x_m);
        minimum_y = std::min(minimum_y, point.y_m);
        maximum_y = std::max(maximum_y, point.y_m);
    }
    const fode::CounterRng rng(problem.poisson_seed);
    Point initial;
    bool initial_found = false;
    for (std::uint64_t trial = 0; trial < 100000; ++trial) {
        initial = {
            minimum_x + (maximum_x - minimum_x)
                * rng.uniform(0, 7101, trial, 0),
            minimum_y + (maximum_y - minimum_y)
                * rng.uniform(0, 7101, trial, 1)
        };
        if (point_in_boundary(problem, initial)) {
            initial_found = true;
            break;
        }
    }
    if (!initial_found) {
        throw std::runtime_error("Poisson sampler could not seed polygon");
    }

    std::vector<Point> candidates = {initial};
    std::vector<int> active = {0};
    std::uint64_t event = 0;
    const double minimum_squared =
        problem.minimum_spacing_m * problem.minimum_spacing_m;
    while (!active.empty()
           && static_cast<int>(candidates.size())
               < problem.target_candidate_count) {
        const int active_position = rng.integer(
            0, static_cast<int>(active.size()), 0, 7102, event
        );
        const Point base =
            candidates[static_cast<std::size_t>(
                active[static_cast<std::size_t>(active_position)]
            )];
        bool accepted = false;
        for (int trial = 0; trial < problem.poisson_max_trials; ++trial) {
            const double angle = 2.0 * std::numbers::pi
                * rng.uniform(
                    0, 7103, event,
                    static_cast<std::uint64_t>(trial), 0
                );
            const double radius = problem.minimum_spacing_m
                * (1.0 + rng.uniform(
                    0, 7103, event,
                    static_cast<std::uint64_t>(trial), 1
                ));
            const Point proposed = {
                base.x_m + radius * std::cos(angle),
                base.y_m + radius * std::sin(angle)
            };
            if (!point_in_boundary(problem, proposed)) {
                continue;
            }
            bool clear = true;
            for (const Point& existing : candidates) {
                if (squared_distance(proposed, existing)
                    < minimum_squared) {
                    clear = false;
                    break;
                }
            }
            if (clear) {
                candidates.push_back(proposed);
                active.push_back(static_cast<int>(candidates.size()) - 1);
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            active.erase(active.begin() + active_position);
        }
        ++event;
    }
    if (static_cast<int>(candidates.size())
        != problem.target_candidate_count) {
        throw std::runtime_error(
            "declared polygon produced only "
            + std::to_string(candidates.size())
            + " Poisson candidates; expected "
            + std::to_string(problem.target_candidate_count)
        );
    }
    return candidates;
}

void hash_byte(std::uint64_t& state, std::uint8_t byte) {
    state ^= byte;
    state *= 1099511628211ULL;
}

template <typename Value>
void hash_value(std::uint64_t& state, const Value& value) {
    const auto bytes = std::bit_cast<
        std::array<std::uint8_t, sizeof(Value)>
    >(value);
    for (const std::uint8_t byte : bytes) {
        hash_byte(state, byte);
    }
}

void hash_text(std::uint64_t& state, std::string_view value) {
    for (const unsigned char character : value) {
        hash_byte(state, character);
    }
    hash_byte(state, 0xffU);
}

std::string hex_hash(std::uint64_t state) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << state;
    return output.str();
}

}  // namespace

Problem load_problem(const std::string& path) {
    const std::string text = read_text(path);
    if (string_value(text, "problem_semantic_id") != kProblemSemanticId) {
        throw std::runtime_error("unexpected GeoGA problem semantic ID");
    }
    Problem problem;
    problem.case_id = string_value(text, "case_id");
    problem.turbine_count = integer_value(text, "turbine_count");
    problem.target_candidate_count =
        integer_value(text, "target_candidate_count");
    problem.rotor_diameter_m = number_value(text, "rotor_diameter_m");
    problem.hub_height_m = number_value(text, "hub_height_m");
    problem.rated_power_kw = number_value(text, "rated_power_kw");
    problem.cut_in_speed_mps = number_value(text, "cut_in_speed_mps");
    problem.rated_speed_mps = number_value(text, "rated_speed_mps");
    problem.cut_out_speed_mps = number_value(text, "cut_out_speed_mps");
    problem.thrust_coefficient =
        number_value(text, "thrust_coefficient");
    problem.wake_expansion = number_value(text, "wake_expansion");
    problem.air_density_kg_m3 = number_value(text, "air_density_kg_m3");
    problem.roughness_length_m =
        number_value(text, "roughness_length_m");
    problem.turbulence_intensity =
        number_value(text, "turbulence_intensity");
    problem.minimum_spacing_m = number_value(text, "minimum_spacing_m");
    problem.poisson_max_trials = integer_value(text, "max_trials");
    problem.poisson_seed = unsigned_value(text, "seed");
    problem.boundary = parse_points(raw_value(text, "boundary_vertices_m"));
    problem.wind_bins = parse_wind_bins(raw_value(text, "wind_bins"));

    if (problem.turbine_count <= 0
        || problem.target_candidate_count <= problem.turbine_count) {
        throw std::runtime_error("invalid GeoGA turbine/candidate counts");
    }
    if (problem.wind_bins.size() != 12) {
        throw std::runtime_error("GeoGA declared case requires 12 wind bins");
    }
    double probability_sum = 0.0;
    for (const WindBin& bin : problem.wind_bins) {
        if (!(bin.probability > 0.0) || !(bin.free_speed_mps > 0.0)) {
            throw std::runtime_error("invalid GeoGA wind bin");
        }
        probability_sum += bin.probability;
    }
    if (std::abs(probability_sum - 1.0) > 1.0e-12) {
        throw std::runtime_error("GeoGA wind probabilities do not sum to one");
    }
    if (std::abs(
        problem.minimum_spacing_m - 5.0 * problem.rotor_diameter_m
    ) > 1.0e-12) {
        throw std::runtime_error("GeoGA spacing must equal five diameters");
    }
    problem.candidates = generate_candidates(problem);
    return problem;
}

bool point_in_boundary(const Problem& problem, const Point& point) {
    bool inside = false;
    const std::size_t count = problem.boundary.size();
    for (std::size_t first = 0, second = count - 1;
         first < count; second = first++) {
        const Point& a = problem.boundary[first];
        const Point& b = problem.boundary[second];
        const bool crosses = (a.y_m > point.y_m) != (b.y_m > point.y_m);
        if (crosses) {
            const double intersection_x = (b.x_m - a.x_m)
                * (point.y_m - a.y_m) / (b.y_m - a.y_m) + a.x_m;
            if (point.x_m < intersection_x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

double minimum_candidate_spacing_m(const Problem& problem) {
    double minimum_squared = std::numeric_limits<double>::infinity();
    for (std::size_t first = 0; first < problem.candidates.size(); ++first) {
        for (std::size_t second = first + 1;
             second < problem.candidates.size(); ++second) {
            minimum_squared = std::min(
                minimum_squared,
                squared_distance(
                    problem.candidates[first],
                    problem.candidates[second]
                )
            );
        }
    }
    return std::sqrt(minimum_squared);
}

double turbine_power_kw(const Problem& problem, double speed_mps) {
    if (speed_mps < problem.cut_in_speed_mps
        || speed_mps > problem.cut_out_speed_mps) {
        return 0.0;
    }
    if (speed_mps >= problem.rated_speed_mps) {
        return problem.rated_power_kw;
    }
    const double numerator =
        speed_mps * speed_mps * speed_mps
        - std::pow(problem.cut_in_speed_mps, 3);
    const double denominator =
        std::pow(problem.rated_speed_mps, 3)
        - std::pow(problem.cut_in_speed_mps, 3);
    return problem.rated_power_kw * numerator / denominator;
}

double single_wake_deficit_fraction(
    const Problem& problem,
    double downstream_distance_m,
    double crosswind_distance_m
) {
    if (!(downstream_distance_m > 0.0)) {
        return 0.0;
    }
    const double rotor_radius = 0.5 * problem.rotor_diameter_m;
    const double wake_radius =
        rotor_radius + problem.wake_expansion * downstream_distance_m;
    if (std::abs(crosswind_distance_m) > wake_radius) {
        return 0.0;
    }
    const double axial_induction =
        0.5 * (1.0 - std::sqrt(1.0 - problem.thrust_coefficient));
    const double expansion =
        1.0 + problem.wake_expansion * downstream_distance_m / rotor_radius;
    return 2.0 * axial_induction / (expansion * expansion);
}

LayoutEvaluation evaluate_layout(
    const Problem& problem,
    const std::vector<int>& layout_0based
) {
    if (static_cast<int>(layout_0based.size()) != problem.turbine_count) {
        throw std::runtime_error("GeoGA layout has wrong turbine count");
    }
    std::vector<int> layout = layout_0based;
    std::sort(layout.begin(), layout.end());
    if (std::adjacent_find(layout.begin(), layout.end()) != layout.end()
        || layout.front() < 0
        || layout.back() >= static_cast<int>(problem.candidates.size())) {
        throw std::runtime_error("GeoGA layout contains invalid candidates");
    }

    double expected_power_kw = 0.0;
    double no_wake_power_kw = 0.0;
    for (const WindBin& bin : problem.wind_bins) {
        const double radians =
            bin.flow_to_degrees * std::numbers::pi / 180.0;
        const double flow_x = std::cos(radians);
        const double flow_y = std::sin(radians);
        const double cross_x = -flow_y;
        const double cross_y = flow_x;
        double total_power_kw = 0.0;
        for (std::size_t target = 0; target < layout.size(); ++target) {
            const Point& target_point = problem.candidates[
                static_cast<std::size_t>(layout[target])
            ];
            double squared_deficit_sum = 0.0;
            for (std::size_t source = 0; source < layout.size(); ++source) {
                if (source == target) {
                    continue;
                }
                const Point& source_point = problem.candidates[
                    static_cast<std::size_t>(layout[source])
                ];
                const double dx = target_point.x_m - source_point.x_m;
                const double dy = target_point.y_m - source_point.y_m;
                const double downstream = dx * flow_x + dy * flow_y;
                const double crosswind = dx * cross_x + dy * cross_y;
                const double deficit = single_wake_deficit_fraction(
                    problem, downstream, crosswind
                );
                squared_deficit_sum += deficit * deficit;
            }
            const double effective_speed = bin.free_speed_mps * std::max(
                0.0, 1.0 - std::sqrt(squared_deficit_sum)
            );
            total_power_kw += turbine_power_kw(problem, effective_speed);
        }
        expected_power_kw += bin.probability * total_power_kw;
        no_wake_power_kw += bin.probability
            * static_cast<double>(problem.turbine_count)
            * turbine_power_kw(problem, bin.free_speed_mps);
    }

    LayoutEvaluation result;
    result.aep_kwh = 8760.0 * expected_power_kw;
    result.no_wake_aep_kwh = 8760.0 * no_wake_power_kw;
    result.capacity_factor = result.aep_kwh
        / (8760.0 * problem.rated_power_kw
           * static_cast<double>(problem.turbine_count));
    return result;
}

std::string problem_semantic_hash(const Problem& problem) {
    std::uint64_t state = 1469598103934665603ULL;
    hash_text(state, kProblemSemanticId);
    hash_text(state, problem.case_id);
    hash_text(state, kCandidateRule);
    hash_text(state, kBoundaryRule);
    hash_text(state, kPowerRule);
    hash_text(state, kWakeRule);
    hash_text(state, kObjectiveRule);
    hash_text(state, kCoordinateRule);
    hash_text(state, kActualLayoutBoundary);
    hash_value(state, problem.turbine_count);
    hash_value(state, problem.target_candidate_count);
    hash_value(state, problem.rotor_diameter_m);
    hash_value(state, problem.hub_height_m);
    hash_value(state, problem.rated_power_kw);
    hash_value(state, problem.cut_in_speed_mps);
    hash_value(state, problem.rated_speed_mps);
    hash_value(state, problem.cut_out_speed_mps);
    hash_value(state, problem.thrust_coefficient);
    hash_value(state, problem.wake_expansion);
    hash_value(state, problem.air_density_kg_m3);
    hash_value(state, problem.roughness_length_m);
    hash_value(state, problem.turbulence_intensity);
    hash_value(state, problem.minimum_spacing_m);
    hash_value(state, problem.poisson_max_trials);
    hash_value(state, problem.poisson_seed);
    for (const Point& point : problem.boundary) {
        hash_value(state, point.x_m);
        hash_value(state, point.y_m);
    }
    for (const WindBin& bin : problem.wind_bins) {
        hash_value(state, bin.flow_to_degrees);
        hash_value(state, bin.free_speed_mps);
        hash_value(state, bin.probability);
    }
    for (const Point& point : problem.candidates) {
        hash_value(state, point.x_m);
        hash_value(state, point.y_m);
    }
    return hex_hash(state);
}

std::string layout_hash(const std::vector<int>& layout_0based) {
    std::uint64_t state = 1469598103934665603ULL;
    for (const int candidate : layout_0based) {
        hash_value(state, candidate);
    }
    return hex_hash(state);
}

}  // namespace geoga
