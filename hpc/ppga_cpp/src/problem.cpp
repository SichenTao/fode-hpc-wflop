/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: PPGA Nantong-structured declared 3D problem evaluator
Paper title: Advanced 3D Wind Farm Layout Optimization Framework via Power-Law Perturbation-Based Genetic Algorithm
DOI: 10.1109/JAS.2025.125351
Paper-preserved fields: 16 by 27 grid, 300 m spacing, H171-6.2MW surface parameters, four 16-direction by 7-speed scenarios, terrain-aware Gaussian wakes, and conversion efficiency
Declared P3 completions: analytic 0--6 m seabed, factorized frozen winds, cubic-to-rated power curve, axial induction one third, Gaussian deficit, Tao-2020 root-sum-square multiple wakes, and shear exponent 0.1
Problem evidence tier: P3_DECLARED_PROXY
Problem semantic ID: ppga_nantong_structured_3d_declared_proxy_v1
Controlling contract: shared/contracts/ppga_nantong_structured_3d_declared_proxy_contract.json
Claim boundary: original Nantong arrays, manufacturer curves, layouts, efficiencies, and author implementation remain blocked
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "ppga/problem.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace ppga {
namespace {

constexpr double kRotorRadiusM = 85.5;
constexpr double kHubHeightM = 107.0;
constexpr double kTerrainOffsetM = 3.0;
constexpr double kTerrainAmplitudeM = 1.5;
constexpr double kAxialInduction = 1.0 / 3.0;
constexpr double kWakeExpansion = 0.05;
constexpr double kGaussianXi = 1.98;
constexpr double kShearExponent = 0.1;
constexpr double kCutInSpeedMps = 3.0;
constexpr double kRatedSpeedMps = 11.0;
constexpr double kCutOutSpeedMps = 25.0;
constexpr double kRatedPowerKw = 6200.0;
constexpr double kCostExponentCoefficient = 0.00174;
constexpr std::string_view kTerrainRule =
    "z=3+1.5*cos(2*pi*col/(cols-1))+1.5*cos(pi*row/(rows-1))";
constexpr std::string_view kPowerCurveRule =
    "zero_below_cut_in_cubic_to_rated_plateau_to_cut_out_zero_above";
constexpr std::string_view kMultipleWakeRule =
    "root_sum_square_absolute_velocity_deficits_fixed_source_order";
constexpr std::string_view kObjectiveRule =
    "maximize_expected_power_over_terrain_shear_ideal_expected_power";
constexpr std::string_view kCostDiagnosticRule =
    "M*(exp(-0.00174*M^2)/3+2/3)/expected_power";

std::string read_text(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open PPGA problem contract: " + path);
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
    for (std::size_t i = opening; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
        } else if (ch == '"') {
            in_string = true;
        } else if (ch == open) {
            ++depth;
        } else if (ch == close) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    throw std::runtime_error("unterminated JSON value");
}

std::string_view raw_value(std::string_view object, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = object.find(needle);
    if (key_pos == std::string_view::npos) {
        throw std::runtime_error("missing JSON key: " + std::string(key));
    }
    std::size_t pos = object.find(':', key_pos + needle.size());
    if (pos == std::string_view::npos) {
        throw std::runtime_error("invalid JSON field: " + std::string(key));
    }
    do {
        ++pos;
    } while (pos < object.size()
             && std::isspace(static_cast<unsigned char>(object[pos])));
    if (pos >= object.size()) {
        throw std::runtime_error("empty JSON field: " + std::string(key));
    }
    if (object[pos] == '[') {
        const std::size_t end = matching_delimiter(object, pos, '[', ']');
        return object.substr(pos, end - pos + 1);
    }
    if (object[pos] == '{') {
        const std::size_t end = matching_delimiter(object, pos, '{', '}');
        return object.substr(pos, end - pos + 1);
    }
    if (object[pos] == '"') {
        for (std::size_t end = pos + 1; end < object.size(); ++end) {
            if (object[end] == '"' && object[end - 1] != '\\') {
                return object.substr(pos, end - pos + 1);
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }
    std::size_t end = pos;
    while (end < object.size() && object[end] != ',' && object[end] != '}') {
        ++end;
    }
    return object.substr(pos, end - pos);
}

std::string parse_string(std::string_view raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') {
        throw std::runtime_error("expected JSON string");
    }
    return std::string(raw.substr(1, raw.size() - 2));
}

double parse_scalar(std::string_view raw) {
    const std::string value(raw);
    char* end = nullptr;
    const double result = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        throw std::runtime_error("expected JSON number");
    }
    return result;
}

std::vector<double> parse_numbers(std::string_view raw) {
    std::vector<double> result;
    const char* cursor = raw.data();
    const char* const finish = raw.data() + raw.size();
    while (cursor < finish) {
        if (std::isdigit(static_cast<unsigned char>(*cursor))
            || *cursor == '-' || *cursor == '+'
            || (*cursor == '.' && cursor + 1 < finish
                && std::isdigit(static_cast<unsigned char>(cursor[1])))) {
            char* end = nullptr;
            const double value = std::strtod(cursor, &end);
            if (end != cursor) {
                result.push_back(value);
                cursor = end;
                continue;
            }
        }
        ++cursor;
    }
    return result;
}

std::vector<std::string_view> objects_in_array(
    std::string_view document,
    std::string_view key_name
) {
    const std::string needle = "\"" + std::string(key_name) + "\"";
    const std::size_t key = document.find(needle);
    if (key == std::string_view::npos) {
        throw std::runtime_error("missing JSON object array: "
                                 + std::string(key_name));
    }
    const std::size_t begin = document.find('[', key);
    const std::size_t end = matching_delimiter(document, begin, '[', ']');
    std::vector<std::string_view> result;
    std::size_t pos = begin + 1;
    while (pos < end) {
        pos = document.find('{', pos);
        if (pos == std::string_view::npos || pos >= end) {
            break;
        }
        const std::size_t object_end =
            matching_delimiter(document, pos, '{', '}');
        result.push_back(document.substr(pos, object_end - pos + 1));
        pos = object_end + 1;
    }
    return result;
}

void validate_probability_vector(
    const std::vector<double>& probability,
    std::size_t expected_size,
    const std::string& label
) {
    if (probability.size() != expected_size
        || std::any_of(probability.begin(), probability.end(), [](double value) {
               return !std::isfinite(value) || value < 0.0;
           })
        || std::abs(std::accumulate(
               probability.begin(), probability.end(), 0.0
           ) - 1.0) > 1.0e-12) {
        throw std::runtime_error("invalid probability vector: " + label);
    }
}

void fnv_mix(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }
}

template <typename T>
void fnv_value(std::uint64_t& hash, const T& value) {
    fnv_mix(hash, &value, sizeof(value));
}

std::string hex_hash(std::uint64_t hash) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

struct Point {
    double x = 0.0;
    double y = 0.0;
    double hub_z = 0.0;
};

}  // namespace

Problem load_problem(const std::string& path, const std::string& case_id) {
    const std::string document = read_text(path);
    if (parse_string(raw_value(document, "problem_semantic_id"))
        != kProblemSemanticId) {
        throw std::runtime_error("unexpected PPGA problem semantic ID");
    }
    Problem problem;
    problem.case_id = case_id;
    const std::string_view grid = raw_value(document, "grid");
    problem.rows = static_cast<int>(std::llround(
        parse_scalar(raw_value(grid, "rows"))
    ));
    problem.cols = static_cast<int>(std::llround(
        parse_scalar(raw_value(grid, "cols"))
    ));
    problem.cell_width_m = parse_scalar(raw_value(grid, "cell_width_m"));
    const std::vector<double> direction_deg =
        parse_numbers(raw_value(document, "wind_directions_deg"));
    problem.wind_directions_rad.reserve(direction_deg.size());
    for (double degrees : direction_deg) {
        problem.wind_directions_rad.push_back(
            degrees * std::numbers::pi / 180.0
        );
    }
    problem.wind_speeds_mps =
        parse_numbers(raw_value(document, "wind_speeds_mps"));

    bool found_case = false;
    for (const std::string_view object : objects_in_array(document, "cases")) {
        if (parse_string(raw_value(object, "case_id")) == case_id) {
            problem.wind_scenario_id =
                parse_string(raw_value(object, "wind_scenario_id"));
            problem.turbine_count = static_cast<int>(std::llround(
                parse_scalar(raw_value(object, "turbine_count"))
            ));
            found_case = true;
            break;
        }
    }
    if (!found_case) {
        throw std::runtime_error("unknown PPGA case: " + case_id);
    }
    bool found_scenario = false;
    for (const std::string_view object :
         objects_in_array(document, "wind_scenarios")) {
        if (parse_string(raw_value(object, "id"))
            == problem.wind_scenario_id) {
            problem.wind.id = problem.wind_scenario_id;
            problem.wind.direction_probabilities =
                parse_numbers(raw_value(object, "direction_probabilities"));
            problem.wind.speed_probabilities =
                parse_numbers(raw_value(object, "speed_probabilities"));
            found_scenario = true;
            break;
        }
    }
    if (!found_scenario || problem.rows != 16 || problem.cols != 27
        || problem.cell_width_m != 300.0
        || problem.turbine_count < 1
        || problem.turbine_count > problem.rows * problem.cols
        || problem.wind_directions_rad.size() != 16
        || problem.wind_speeds_mps.size() != 7) {
        throw std::runtime_error("invalid frozen PPGA problem contract");
    }
    validate_probability_vector(
        problem.wind.direction_probabilities,
        problem.wind_directions_rad.size(),
        problem.wind_scenario_id + " directions"
    );
    validate_probability_vector(
        problem.wind.speed_probabilities,
        problem.wind_speeds_mps.size(),
        problem.wind_scenario_id + " speeds"
    );
    return problem;
}

std::string problem_semantic_hash(const Problem& problem) {
    std::uint64_t hash = 1469598103934665603ULL;
    fnv_mix(hash, kProblemSemanticId, std::char_traits<char>::length(
        kProblemSemanticId
    ));
    fnv_value(hash, problem.rows);
    fnv_value(hash, problem.cols);
    fnv_value(hash, problem.turbine_count);
    fnv_value(hash, problem.cell_width_m);
    for (const double value : {
             kRotorRadiusM,
             kHubHeightM,
             kTerrainOffsetM,
             kTerrainAmplitudeM,
             kAxialInduction,
             kWakeExpansion,
             kGaussianXi,
             kShearExponent,
             kCutInSpeedMps,
             kRatedSpeedMps,
             kCutOutSpeedMps,
             kRatedPowerKw,
             kCostExponentCoefficient,
         }) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (const std::string_view rule : {
             kTerrainRule,
             kPowerCurveRule,
             kMultipleWakeRule,
             kObjectiveRule,
             kCostDiagnosticRule,
         }) {
        fnv_mix(hash, rule.data(), rule.size());
    }
    for (double value : problem.wind_directions_rad) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (double value : problem.wind_speeds_mps) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (double value : problem.wind.direction_probabilities) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (double value : problem.wind.speed_probabilities) {
        fnv_value(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hex_hash(hash);
}

double foundation_elevation_m(const Problem& problem, int cell_1based) {
    const int dimension = problem.rows * problem.cols;
    if (cell_1based < 1 || cell_1based > dimension) {
        throw std::invalid_argument("PPGA cell is outside the grid");
    }
    const int zero = cell_1based - 1;
    const int row = zero / problem.cols;
    const int col = zero % problem.cols;
    return kTerrainOffsetM
        + kTerrainAmplitudeM * std::cos(
            2.0 * std::numbers::pi * static_cast<double>(col)
            / static_cast<double>(problem.cols - 1)
        )
        + kTerrainAmplitudeM * std::cos(
            std::numbers::pi * static_cast<double>(row)
            / static_cast<double>(problem.rows - 1)
        );
}

double turbine_power_kw(double effective_speed_mps) {
    if (!std::isfinite(effective_speed_mps)
        || effective_speed_mps < kCutInSpeedMps
        || effective_speed_mps > kCutOutSpeedMps) {
        return 0.0;
    }
    if (effective_speed_mps >= kRatedSpeedMps) {
        return kRatedPowerKw;
    }
    const double numerator =
        effective_speed_mps * effective_speed_mps * effective_speed_mps
        - kCutInSpeedMps * kCutInSpeedMps * kCutInSpeedMps;
    const double denominator =
        kRatedSpeedMps * kRatedSpeedMps * kRatedSpeedMps
        - kCutInSpeedMps * kCutInSpeedMps * kCutInSpeedMps;
    return kRatedPowerKw * numerator / denominator;
}

double single_wake_deficit_fraction(
    double downstream_distance_m,
    double crosswind_distance_m,
    double vertical_distance_m
) {
    if (!(downstream_distance_m > 0.0)) {
        return 0.0;
    }
    const double wake_radius =
        kRotorRadiusM + kWakeExpansion * downstream_distance_m;
    const double sigma = wake_radius / kGaussianXi;
    const double radial_square =
        crosswind_distance_m * crosswind_distance_m
        + vertical_distance_m * vertical_distance_m;
    return 2.0 * kAxialInduction
        * (kRotorRadiusM * kRotorRadiusM)
        / (wake_radius * wake_radius)
        * std::exp(-radial_square / (2.0 * sigma * sigma));
}

LayoutEvaluation evaluate_layout(
    const Problem& problem,
    const std::vector<int>& layout_1based
) {
    if (static_cast<int>(layout_1based.size()) != problem.turbine_count
        || !std::is_sorted(layout_1based.begin(), layout_1based.end())
        || std::adjacent_find(layout_1based.begin(), layout_1based.end())
            != layout_1based.end()
        || layout_1based.front() < 1
        || layout_1based.back() > problem.rows * problem.cols) {
        throw std::invalid_argument(
            "PPGA layout must be a sorted unique complete layout"
        );
    }
    std::vector<Point> points;
    points.reserve(layout_1based.size());
    for (int cell : layout_1based) {
        const int zero = cell - 1;
        points.push_back({
            static_cast<double>(zero % problem.cols) * problem.cell_width_m,
            static_cast<double>(zero / problem.cols) * problem.cell_width_m,
            kHubHeightM + foundation_elevation_m(problem, cell)
        });
    }

    LayoutEvaluation result;
    for (std::size_t direction_index = 0;
         direction_index < problem.wind_directions_rad.size();
         ++direction_index) {
        const double theta = problem.wind_directions_rad[direction_index];
        const double cos_theta = std::cos(theta);
        const double sin_theta = std::sin(theta);
        std::vector<double> downwind(points.size());
        std::vector<double> crosswind(points.size());
        for (std::size_t i = 0; i < points.size(); ++i) {
            downwind[i] = points[i].x * cos_theta + points[i].y * sin_theta;
            crosswind[i] = -points[i].x * sin_theta + points[i].y * cos_theta;
        }
        for (std::size_t speed_index = 0;
             speed_index < problem.wind_speeds_mps.size();
             ++speed_index) {
            const double probability =
                problem.wind.direction_probabilities[direction_index]
                * problem.wind.speed_probabilities[speed_index];
            const double reference_speed =
                problem.wind_speeds_mps[speed_index];
            double state_power = 0.0;
            double state_ideal = 0.0;
            for (std::size_t target = 0; target < points.size(); ++target) {
                const double ambient = reference_speed * std::pow(
                    points[target].hub_z / kHubHeightM,
                    kShearExponent
                );
                double squared_velocity_deficit = 0.0;
                for (std::size_t source = 0; source < points.size(); ++source) {
                    const double downstream =
                        downwind[target] - downwind[source];
                    if (source == target || !(downstream > 0.0)) {
                        continue;
                    }
                    const double fraction = single_wake_deficit_fraction(
                        downstream,
                        crosswind[target] - crosswind[source],
                        points[target].hub_z - points[source].hub_z
                    );
                    const double velocity_deficit = ambient * fraction;
                    squared_velocity_deficit +=
                        velocity_deficit * velocity_deficit;
                }
                const double effective = std::max(
                    0.0,
                    ambient - std::sqrt(squared_velocity_deficit)
                );
                state_power += turbine_power_kw(effective);
                state_ideal += turbine_power_kw(ambient);
            }
            result.expected_power_kw += probability * state_power;
            result.ideal_expected_power_kw += probability * state_ideal;
        }
    }
    if (!(result.ideal_expected_power_kw > 0.0)
        || !(result.expected_power_kw > 0.0)) {
        throw std::runtime_error("invalid non-positive PPGA layout power");
    }
    result.conversion_efficiency =
        result.expected_power_kw / result.ideal_expected_power_kw;
    const double count = static_cast<double>(problem.turbine_count);
    const double cost =
        count * (
            std::exp(-kCostExponentCoefficient * count * count) / 3.0
            + 2.0 / 3.0
        );
    result.cost_per_expected_power = cost / result.expected_power_kw;
    return result;
}

}  // namespace ppga
