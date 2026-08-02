/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: shared parameterized scalar/discrete WFLOP JSON loader
Paper title and DOI: thirteen scalar WFLOP packages; see
docs/scalar_problem_package_registry.tsv
Paper/source basis: paper-native manifests and hashed local source arrays
Public asset: per-contract source fields and package registry
Missing/conflicts: optional fields retain FODE defaults for legacy contracts
Reconstruction: strict JSON, physical-model, probability, and mask validation
Method/problem semantic IDs: not_applicable_shared_infrastructure;
registry_defined
Controlling contract and claim boundary:
docs/scalar_problem_package_registry.tsv; P3 never becomes original data
Last evidence-audit date: 2026-07-30
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "fode/case.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fode {
namespace {

std::string read_text(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("cannot open benchmark contract: " + path);
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
            continue;
        }
        if (ch == '"') {
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
    ++pos;
    while (pos < object.size()
           && std::isspace(static_cast<unsigned char>(object[pos]))) {
        ++pos;
    }
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
        bool escaped = false;
        for (std::size_t end = pos + 1; end < object.size(); ++end) {
            const char ch = object[end];
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
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
    std::string result;
    result.reserve(raw.size() - 2);
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < raw.size(); ++i) {
        const char ch = raw[i];
        if (escaped) {
            switch (ch) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default:
                    throw std::runtime_error("unsupported JSON escape");
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else {
            result.push_back(ch);
        }
    }
    return result;
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

std::vector<std::string_view> case_objects(std::string_view document) {
    const std::size_t key = document.find("\"cases\"");
    if (key == std::string_view::npos) {
        throw std::runtime_error("benchmark contract has no cases array");
    }
    const std::size_t begin = document.find('[', key);
    if (begin == std::string_view::npos) {
        throw std::runtime_error("benchmark cases field is not an array");
    }
    const std::size_t end = matching_delimiter(document, begin, '[', ']');
    std::vector<std::string_view> objects;
    std::size_t pos = begin + 1;
    while (pos < end) {
        while (pos < end && document[pos] != '{') {
            ++pos;
        }
        if (pos >= end) {
            break;
        }
        const std::size_t object_end =
            matching_delimiter(document, pos, '{', '}');
        objects.push_back(document.substr(pos, object_end - pos + 1));
        pos = object_end + 1;
    }
    return objects;
}

CaseData parse_case(std::string_view object) {
    CaseData data;
    data.case_id = parse_string(raw_value(object, "case_id"));
    data.rows = static_cast<int>(
        std::llround(parse_scalar(raw_value(object, "rows")))
    );
    data.cols = static_cast<int>(
        std::llround(parse_scalar(raw_value(object, "cols")))
    );
    data.turbine_count = static_cast<int>(
        std::llround(parse_scalar(raw_value(object, "turbine_count")))
    );
    data.cell_width = parse_scalar(raw_value(object, "cell_width"));
    auto optional_scalar = [&](std::string_view key, double fallback) {
        const std::string needle = "\"" + std::string(key) + "\"";
        return object.find(needle) == std::string_view::npos
            ? fallback
            : parse_scalar(raw_value(object, key));
    };
    auto optional_string = [&](std::string_view key, std::string fallback) {
        const std::string needle = "\"" + std::string(key) + "\"";
        return object.find(needle) == std::string_view::npos
            ? std::move(fallback)
            : parse_string(raw_value(object, key));
    };
    auto optional_numbers = [&](std::string_view key) {
        const std::string needle = "\"" + std::string(key) + "\"";
        return object.find(needle) == std::string_view::npos
            ? std::vector<double>{}
            : parse_numbers(raw_value(object, key));
    };
    data.rotor_diameter = optional_scalar(
        "rotor_diameter", data.rotor_diameter
    );
    data.hub_height = optional_scalar("hub_height", data.hub_height);
    data.surface_roughness = optional_scalar(
        "surface_roughness", data.surface_roughness
    );
    data.wake_deficit_coefficient = optional_scalar(
        "wake_deficit_coefficient", data.wake_deficit_coefficient
    );
    data.power_curve_cubic_coefficient = optional_scalar(
        "power_curve_cubic_coefficient",
        data.power_curve_cubic_coefficient
    );
    data.power_curve_rated_kw = optional_scalar(
        "power_curve_rated_kw", data.power_curve_rated_kw
    );
    data.power_curve_cutin_mps = optional_scalar(
        "power_curve_cutin_mps", data.power_curve_cutin_mps
    );
    data.power_curve_rated_mps = optional_scalar(
        "power_curve_rated_mps", data.power_curve_rated_mps
    );
    data.power_curve_cutout_mps = optional_scalar(
        "power_curve_cutout_mps", data.power_curve_cutout_mps
    );
    data.wake_model = optional_string("wake_model", data.wake_model);
    data.power_curve_model = optional_string(
        "power_curve_model", data.power_curve_model
    );
    data.terrain_shear_exponent = optional_scalar(
        "terrain_shear_exponent", data.terrain_shear_exponent
    );
    data.gaussian_wake_expansion = optional_scalar(
        "gaussian_wake_expansion", data.gaussian_wake_expansion
    );
    data.terrain_elevation_m = optional_numbers("terrain_elevation_m");
    data.theta = parse_numbers(raw_value(object, "wind_directions_rad"));
    data.velocity = parse_numbers(raw_value(object, "wind_speeds_mps"));
    data.probability = parse_numbers(raw_value(object, "joint_probabilities"));
    const std::vector<double> unavailable =
        parse_numbers(raw_value(object, "unavailable_cells_1based"));
    data.unavailable_cells_1based.reserve(unavailable.size());
    for (const double value : unavailable) {
        data.unavailable_cells_1based.push_back(
            static_cast<int>(std::llround(value))
        );
    }

    const bool finite_directions = std::all_of(
        data.theta.begin(),
        data.theta.end(),
        [](double value) { return std::isfinite(value); }
    );
    const bool valid_speeds = std::all_of(
        data.velocity.begin(),
        data.velocity.end(),
        [](double value) { return std::isfinite(value) && value > 0.0; }
    );
    const bool valid_probabilities = std::all_of(
        data.probability.begin(),
        data.probability.end(),
        [](double value) { return std::isfinite(value) && value >= 0.0; }
    );
    const bool finite_terrain = std::all_of(
        data.terrain_elevation_m.begin(),
        data.terrain_elevation_m.end(),
        [](double value) { return std::isfinite(value); }
    );
    const double probability_sum = std::accumulate(
        data.probability.begin(),
        data.probability.end(),
        0.0
    );
    if (data.case_id.empty()
        || data.rows <= 0 || data.cols <= 0 || data.turbine_count <= 0
        || data.cell_width <= 0.0
        || data.rotor_diameter <= 0.0
        || data.hub_height <= 0.0
        || data.surface_roughness <= 0.0
        || data.wake_deficit_coefficient <= 0.0
        || data.power_curve_cubic_coefficient <= 0.0
        || data.power_curve_rated_kw <= 0.0
        || data.power_curve_cutin_mps < 0.0
        || data.power_curve_rated_mps <= data.power_curve_cutin_mps
        || data.power_curve_cutout_mps <= data.power_curve_rated_mps
        || (
            data.wake_model != "jensen_park_overlap_rss"
            && data.wake_model != "terrain_gaussian_rss"
        )
        || (
            data.power_curve_model != "legacy_cubic"
            && data.power_curve_model != "cutin_shifted_cubic"
        )
        || data.terrain_shear_exponent < 0.0
        || data.gaussian_wake_expansion <= 0.0
        || !finite_terrain
        || (
            !data.terrain_elevation_m.empty()
            && static_cast<int>(data.terrain_elevation_m.size())
                != data.rows * data.cols
        )
        || data.theta.empty()
        || data.velocity.empty()
        || data.probability.size()
            != data.theta.size() * data.velocity.size()
        || !finite_directions || !valid_speeds || !valid_probabilities
        || std::abs(probability_sum - 1.0) > 1.0e-4) {
        throw std::runtime_error(
            "invalid discrete WFLOP case contract for " + data.case_id
        );
    }
    const int grid_dimension = data.rows * data.cols;
    std::sort(
        data.unavailable_cells_1based.begin(),
        data.unavailable_cells_1based.end()
    );
    if (std::adjacent_find(
            data.unavailable_cells_1based.begin(),
            data.unavailable_cells_1based.end()
        ) != data.unavailable_cells_1based.end()
        || (!data.unavailable_cells_1based.empty()
            && (data.unavailable_cells_1based.front() < 1
                || data.unavailable_cells_1based.back() > grid_dimension))) {
        throw std::runtime_error(
            "invalid unavailable-cell set for " + data.case_id
        );
    }
    if (data.turbine_count
        > grid_dimension
            - static_cast<int>(data.unavailable_cells_1based.size())) {
        throw std::runtime_error(
            "insufficient available cells for " + data.case_id
        );
    }
    return data;
}

}  // namespace

std::vector<CaseData> load_cases(const std::string& path) {
    const std::string document = read_text(path);
    const auto objects = case_objects(document);
    std::vector<CaseData> result;
    result.reserve(objects.size());
    std::unordered_set<std::string> case_ids;
    for (const auto object : objects) {
        CaseData data = parse_case(object);
        if (!case_ids.insert(data.case_id).second) {
            throw std::runtime_error(
                "duplicate case identifier: " + data.case_id
            );
        }
        result.push_back(std::move(data));
    }
    if (result.empty()) {
        throw std::runtime_error("benchmark contract contains no cases");
    }
    return result;
}

CaseData load_case(const std::string& path, const std::string& case_id) {
    std::vector<CaseData> cases = load_cases(path);
    const auto match = std::find_if(
        cases.begin(),
        cases.end(),
        [&case_id](const CaseData& data) {
            return data.case_id == case_id;
        }
    );
    if (match == cases.end()) {
        throw std::runtime_error("unknown discrete WFLOP case: " + case_id);
    }
    return *match;
}

}  // namespace fode
