/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T74 pure-C++ SIGA, MARS-style surrogate and grid wake
Paper/source/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/ju_t74.hpp
Public source: pinned MIT WFLOP_Python asset declared in the header.
Missing/completion: declared in the header and controlling contract.
Claim boundary: academic reconstruction, not author numerical replay.
Contract: shared/contracts/core99_t74_ju_siga_2019.json.
Independent validator: scripts/validate_core99_t74.py.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ju_t74.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t74 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int grid_side = 21;
constexpr int candidate_count = grid_side * grid_side;
constexpr int turbine_count = 60;
constexpr int population_size = 100;
constexpr int paper_generation_count = 200;
constexpr int paper_mc_layouts = 10000;
constexpr double rotor_radius_m = 38.5;
constexpr double hub_height_m = 80.0;
constexpr double surface_roughness_m = 0.00025;
constexpr double entrainment =
    0.5 / std::log(hub_height_m / surface_roughness_m);
constexpr double elite_rate = 0.2;
constexpr double selection_rate = 0.5;
constexpr double mutation_rate = 0.1;
constexpr int mars_candidate_locations = 5;
constexpr int mars_maximum_bases = 100;
constexpr double mars_difference = 1.0e-3;

struct WindState {
    double from_radians;
    double speed_mps;
    double probability;
};

struct SplineTerm {
    int x_sign = 0;
    int x_knot = 0;
    int y_sign = 0;
    int y_knot = 0;
};

struct SplineSurface {
    std::vector<SplineTerm> terms;
    std::vector<double> coefficients;

    [[nodiscard]] double predict(const int node) const {
        const double x = static_cast<double>(node % grid_side);
        const double y = static_cast<double>(node / grid_side);
        double value = 0.0;
        for (std::size_t index = 0; index < terms.size(); ++index) {
            const auto& term = terms[index];
            double basis = 1.0;
            if (term.x_sign > 0) {
                basis *= std::max(0.0, x - term.x_knot);
            } else if (term.x_sign < 0) {
                basis *= std::max(0.0, term.x_knot - x);
            }
            if (term.y_sign > 0) {
                basis *= std::max(0.0, y - term.y_knot);
            } else if (term.y_sign < 0) {
                basis *= std::max(0.0, term.y_knot - y);
            }
            value += coefficients[index] * basis;
        }
        return value;
    }
};

struct CachedSurrogate {
    SplineSurface surface;
    double monte_carlo_truth_seconds = 0.0;
    double surrogate_training_seconds = 0.0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<WindState> wind_states(const int wind_case) {
    if (wind_case == 1) return {{0.0, 12.0, 1.0}};
    if (wind_case == 2) {
        return {{std::numbers::pi / 4.0, 12.0, 1.0}};
    }
    if (wind_case == 3) {
        return {
            {0.0, 12.0, 0.25},
            {std::numbers::pi / 2.0, 12.0, 0.25},
            {std::numbers::pi, 12.0, 0.25},
            {3.0 * std::numbers::pi / 2.0, 12.0, 0.25},
        };
    }
    if (wind_case == 4) {
        return {
            {0.0, 12.0, 0.20},
            {std::numbers::pi / 3.0, 12.0, 0.30},
            {2.0 * std::numbers::pi / 3.0, 12.0, 0.20},
            {std::numbers::pi, 12.0, 0.10},
            {4.0 * std::numbers::pi / 3.0, 12.0, 0.10},
            {5.0 * std::numbers::pi / 3.0, 12.0, 0.10},
        };
    }
    std::vector<WindState> result;
    for (int direction = 0; direction < 12; ++direction) {
        const double angle =
            static_cast<double>(direction) * std::numbers::pi / 6.0;
        result.push_back({angle, 8.0, 0.10 / 12.0});
        result.push_back({angle, 10.0, 0.20 / 12.0});
        result.push_back({angle, 12.0, 0.70 / 12.0});
    }
    return result;
}

double turbine_power_kw(const double speed_mps) {
    if (speed_mps < 2.0 || speed_mps >= 18.0) return 0.0;
    if (speed_mps < 12.8) return 0.3 * speed_mps * speed_mps * speed_mps;
    return 629.1;
}

double circle_overlap(
    const double center_distance,
    const double wake_radius,
    const double rotor_radius
) {
    if (center_distance >= wake_radius + rotor_radius) return 0.0;
    if (center_distance <= std::abs(wake_radius - rotor_radius)) {
        const double radius = std::min(wake_radius, rotor_radius);
        return std::numbers::pi * radius * radius;
    }
    const double d = std::max(center_distance, 1.0e-12);
    const double first_argument = std::clamp(
        (d * d + wake_radius * wake_radius - rotor_radius * rotor_radius)
            / (2.0 * d * wake_radius),
        -1.0,
        1.0
    );
    const double second_argument = std::clamp(
        (d * d + rotor_radius * rotor_radius - wake_radius * wake_radius)
            / (2.0 * d * rotor_radius),
        -1.0,
        1.0
    );
    const double first = wake_radius * wake_radius * std::acos(first_argument);
    const double second =
        rotor_radius * rotor_radius * std::acos(second_argument);
    const double radicand = std::max(
        0.0,
        (-d + wake_radius + rotor_radius)
            * (d + wake_radius - rotor_radius)
            * (d - wake_radius + rotor_radius)
            * (d + wake_radius + rotor_radius)
    );
    return first + second - 0.5 * std::sqrt(radicand);
}

bool feasible_layout(const Layout& layout) {
    if (layout.size() != turbine_count) return false;
    if (!std::is_sorted(layout.begin(), layout.end())) return false;
    return layout.front() >= 0 && layout.back() < candidate_count
        && std::adjacent_find(layout.begin(), layout.end()) == layout.end();
}

Layout random_layout(
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t phase
) {
    std::array<std::pair<double, int>, candidate_count> keys{};
    for (int node = 0; node < candidate_count; ++node) {
        keys[static_cast<std::size_t>(node)] = {
            random.uniform(generation, phase, individual, node),
            node,
        };
    }
    std::nth_element(
        keys.begin(),
        keys.begin() + turbine_count,
        keys.end()
    );
    Layout layout;
    layout.reserve(turbine_count);
    for (int index = 0; index < turbine_count; ++index) {
        layout.push_back(keys[static_cast<std::size_t>(index)].second);
    }
    std::sort(layout.begin(), layout.end());
    return layout;
}

Evaluation evaluate_layout(
    const Layout& layout,
    const int wind_case,
    const double cell_width_m
) {
    Evaluation result;
    result.turbine_power_kw.assign(turbine_count, 0.0);
    if (!feasible_layout(layout)) return result;
    const auto states = wind_states(wind_case);
    double ideal_power_kw = 0.0;
    for (const auto& state : states) {
        ideal_power_kw += state.probability * turbine_power_kw(state.speed_mps);
        std::array<double, turbine_count> rotated_x{};
        std::array<double, turbine_count> rotated_y{};
        std::array<int, turbine_count> order{};
        for (int turbine = 0; turbine < turbine_count; ++turbine) {
            const int node = layout[static_cast<std::size_t>(turbine)];
            const double x =
                (static_cast<double>(node % grid_side) + 0.5) * cell_width_m;
            const double y =
                (static_cast<double>(node / grid_side) + 0.5) * cell_width_m;
            rotated_x[static_cast<std::size_t>(turbine)] =
                std::cos(state.from_radians) * x
                - std::sin(state.from_radians) * y;
            rotated_y[static_cast<std::size_t>(turbine)] =
                std::sin(state.from_radians) * x
                + std::cos(state.from_radians) * y;
            order[static_cast<std::size_t>(turbine)] = turbine;
        }
        std::sort(
            order.begin(),
            order.end(),
            [&](const int first, const int second) {
                return rotated_y[static_cast<std::size_t>(first)]
                    > rotated_y[static_cast<std::size_t>(second)];
            }
        );
        std::array<double, turbine_count> sum_squared_deficit{};
        for (int downstream_index = 0;
             downstream_index < turbine_count;
             ++downstream_index) {
            const int downstream =
                order[static_cast<std::size_t>(downstream_index)];
            for (int upstream_index = 0;
                 upstream_index < downstream_index;
                 ++upstream_index) {
                const int upstream =
                    order[static_cast<std::size_t>(upstream_index)];
                const double dx = std::abs(
                    rotated_x[static_cast<std::size_t>(downstream)]
                    - rotated_x[static_cast<std::size_t>(upstream)]
                );
                const double dy = std::abs(
                    rotated_y[static_cast<std::size_t>(downstream)]
                    - rotated_y[static_cast<std::size_t>(upstream)]
                );
                if (dy <= 0.0) continue;
                const double wake_radius = rotor_radius_m + entrainment * dy;
                const double overlap =
                    circle_overlap(dx, wake_radius, rotor_radius_m);
                const double deficit =
                    (2.0 / 3.0)
                    * (rotor_radius_m * rotor_radius_m)
                    / (wake_radius * wake_radius)
                    * overlap
                    / (std::numbers::pi * rotor_radius_m * rotor_radius_m);
                sum_squared_deficit[static_cast<std::size_t>(downstream)]
                    += deficit * deficit;
            }
            const double speed = state.speed_mps
                * (1.0 - std::sqrt(
                    sum_squared_deficit[static_cast<std::size_t>(downstream)]
                ));
            result.turbine_power_kw[static_cast<std::size_t>(downstream)]
                += state.probability * turbine_power_kw(speed);
        }
    }
    result.expected_power_kw = std::accumulate(
        result.turbine_power_kw.begin(),
        result.turbine_power_kw.end(),
        0.0
    );
    result.efficiency_percent =
        100.0 * result.expected_power_kw
        / (static_cast<double>(turbine_count) * ideal_power_kw);
    result.feasible = std::isfinite(result.expected_power_kw);
    return result;
}

std::vector<double> solve_ridge(
    const std::vector<std::vector<double>>& columns,
    const std::vector<double>& target
) {
    const int count = static_cast<int>(columns.size());
    std::vector<std::vector<double>> matrix(
        static_cast<std::size_t>(count),
        std::vector<double>(static_cast<std::size_t>(count + 1), 0.0)
    );
    for (int row = 0; row < count; ++row) {
        for (int column = 0; column < count; ++column) {
            matrix[static_cast<std::size_t>(row)]
                  [static_cast<std::size_t>(column)] =
                std::inner_product(
                    columns[static_cast<std::size_t>(row)].begin(),
                    columns[static_cast<std::size_t>(row)].end(),
                    columns[static_cast<std::size_t>(column)].begin(),
                    0.0
                );
        }
        matrix[static_cast<std::size_t>(row)]
              [static_cast<std::size_t>(count)] =
            std::inner_product(
                columns[static_cast<std::size_t>(row)].begin(),
                columns[static_cast<std::size_t>(row)].end(),
                target.begin(),
                0.0
            );
        matrix[static_cast<std::size_t>(row)]
              [static_cast<std::size_t>(row)] += 1.0e-9;
    }
    for (int pivot = 0; pivot < count; ++pivot) {
        int best = pivot;
        for (int row = pivot + 1; row < count; ++row) {
            if (std::abs(matrix[static_cast<std::size_t>(row)]
                               [static_cast<std::size_t>(pivot)])
                > std::abs(matrix[static_cast<std::size_t>(best)]
                                 [static_cast<std::size_t>(pivot)])) {
                best = row;
            }
        }
        std::swap(
            matrix[static_cast<std::size_t>(pivot)],
            matrix[static_cast<std::size_t>(best)]
        );
        const double diagonal =
            matrix[static_cast<std::size_t>(pivot)]
                  [static_cast<std::size_t>(pivot)];
        if (std::abs(diagonal) < 1.0e-12) continue;
        for (int column = pivot; column <= count; ++column) {
            matrix[static_cast<std::size_t>(pivot)]
                  [static_cast<std::size_t>(column)] /= diagonal;
        }
        for (int row = 0; row < count; ++row) {
            if (row == pivot) continue;
            const double factor =
                matrix[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(pivot)];
            for (int column = pivot; column <= count; ++column) {
                matrix[static_cast<std::size_t>(row)]
                      [static_cast<std::size_t>(column)]
                    -= factor
                    * matrix[static_cast<std::size_t>(pivot)]
                            [static_cast<std::size_t>(column)];
            }
        }
    }
    std::vector<double> result(static_cast<std::size_t>(count), 0.0);
    for (int row = 0; row < count; ++row) {
        result[static_cast<std::size_t>(row)] =
            matrix[static_cast<std::size_t>(row)]
                  [static_cast<std::size_t>(count)];
    }
    return result;
}

double basis_value(const SplineTerm& term, const int node) {
    const double x = static_cast<double>(node % grid_side);
    const double y = static_cast<double>(node / grid_side);
    double value = 1.0;
    if (term.x_sign > 0) value *= std::max(0.0, x - term.x_knot);
    if (term.x_sign < 0) value *= std::max(0.0, term.x_knot - x);
    if (term.y_sign > 0) value *= std::max(0.0, y - term.y_knot);
    if (term.y_sign < 0) value *= std::max(0.0, term.y_knot - y);
    return value;
}

SplineSurface fit_mars_surface(const std::vector<double>& target) {
    std::vector<SplineTerm> candidates;
    for (int knot = 1; knot < grid_side - 1; ++knot) {
        candidates.push_back({1, knot, 0, 0});
        candidates.push_back({-1, knot, 0, 0});
        candidates.push_back({0, 0, 1, knot});
        candidates.push_back({0, 0, -1, knot});
    }
    constexpr std::array<int, 5> interaction_knots{3, 7, 10, 13, 17};
    for (const int x_knot : interaction_knots) {
        for (const int y_knot : interaction_knots) {
            for (const int x_sign : {-1, 1}) {
                for (const int y_sign : {-1, 1}) {
                    candidates.push_back(
                        {x_sign, x_knot, y_sign, y_knot}
                    );
                }
            }
        }
    }
    std::vector<std::vector<double>> candidate_columns;
    candidate_columns.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        std::vector<double> column(candidate_count, 0.0);
        for (int node = 0; node < candidate_count; ++node) {
            column[static_cast<std::size_t>(node)] =
                basis_value(candidate, node);
        }
        candidate_columns.push_back(std::move(column));
    }

    SplineSurface surface;
    surface.terms.push_back({});
    std::vector<std::vector<double>> selected_columns{
        std::vector<double>(candidate_count, 1.0)
    };
    std::vector<bool> selected(candidates.size(), false);
    std::vector<double> prediction(candidate_count, 0.0);
    double previous_sse = std::inner_product(
        target.begin(), target.end(), target.begin(), 0.0
    );
    while (static_cast<int>(surface.terms.size()) < mars_maximum_bases) {
        std::vector<double> residual(candidate_count, 0.0);
        for (int node = 0; node < candidate_count; ++node) {
            residual[static_cast<std::size_t>(node)] =
                target[static_cast<std::size_t>(node)]
                - prediction[static_cast<std::size_t>(node)];
        }
        int best_index = -1;
        double best_score = -1.0;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (selected[index]) continue;
            const auto& column = candidate_columns[index];
            const double numerator = std::abs(std::inner_product(
                column.begin(), column.end(), residual.begin(), 0.0
            ));
            const double denominator = std::sqrt(std::max(
                1.0e-20,
                std::inner_product(
                    column.begin(), column.end(), column.begin(), 0.0
                )
            ));
            const double score = numerator / denominator;
            if (score > best_score) {
                best_score = score;
                best_index = static_cast<int>(index);
            }
        }
        if (best_index < 0) break;
        selected[static_cast<std::size_t>(best_index)] = true;
        surface.terms.push_back(
            candidates[static_cast<std::size_t>(best_index)]
        );
        selected_columns.push_back(
            candidate_columns[static_cast<std::size_t>(best_index)]
        );
        surface.coefficients = solve_ridge(selected_columns, target);
        std::fill(prediction.begin(), prediction.end(), 0.0);
        for (std::size_t basis = 0; basis < selected_columns.size(); ++basis) {
            for (int node = 0; node < candidate_count; ++node) {
                prediction[static_cast<std::size_t>(node)] +=
                    surface.coefficients[basis]
                    * selected_columns[basis][static_cast<std::size_t>(node)];
            }
        }
        double sse = 0.0;
        for (int node = 0; node < candidate_count; ++node) {
            const double residual_value =
                target[static_cast<std::size_t>(node)]
                - prediction[static_cast<std::size_t>(node)];
            sse += residual_value * residual_value;
        }
        const double relative_gain =
            (previous_sse - sse) / std::max(previous_sse, 1.0e-20);
        previous_sse = sse;
        if (relative_gain < mars_difference && surface.terms.size() >= 8U) {
            break;
        }
    }
    if (surface.coefficients.empty()) {
        surface.coefficients = solve_ridge(selected_columns, target);
    }
    return surface;
}

bool probability_event(
    const fode::CounterRng& random,
    const std::string& variant,
    const double threshold,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual,
    const std::uint64_t draw = 0
) {
    if (variant == "paper_probability") {
        return random.uniform(generation, phase, individual, 0, draw)
            < threshold;
    }
    if (variant == "source_normal_threshold") {
        return random.normal(generation, phase, individual, 0, draw)
            < threshold;
    }
    throw std::invalid_argument("unknown T74 variant " + variant);
}

Layout replace_node(Layout layout, const int old_node, const int new_node) {
    const auto iterator = std::find(layout.begin(), layout.end(), old_node);
    if (iterator == layout.end()) {
        throw std::runtime_error("T74 replacement node absent");
    }
    *iterator = new_node;
    std::sort(layout.begin(), layout.end());
    return layout;
}

int random_empty_node(
    const Layout& layout,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    for (int attempt = 0; attempt < candidate_count * 2; ++attempt) {
        const int candidate = random.integer(
            0,
            candidate_count,
            generation,
            phase,
            individual,
            static_cast<std::uint64_t>(attempt),
            draw
        );
        if (!std::binary_search(layout.begin(), layout.end(), candidate)) {
            return candidate;
        }
    }
    for (int candidate = 0; candidate < candidate_count; ++candidate) {
        if (!std::binary_search(layout.begin(), layout.end(), candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("T74 layout has no empty node");
}

Layout crossover_layout(
    const Layout& first,
    const Layout& second,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    for (int attempt = 0; attempt < 64; ++attempt) {
        const int cut = random.integer(
            1,
            turbine_count,
            generation,
            60,
            individual,
            attempt
        );
        if (first[static_cast<std::size_t>(cut - 1)]
            >= second[static_cast<std::size_t>(cut)]) {
            continue;
        }
        Layout child;
        child.insert(
            child.end(),
            first.begin(),
            first.begin() + cut
        );
        child.insert(
            child.end(),
            second.begin() + cut,
            second.end()
        );
        if (feasible_layout(child)) return child;
    }
    Layout child = first;
    for (int index = turbine_count / 2; index < turbine_count; ++index) {
        const int candidate = second[static_cast<std::size_t>(index)];
        if (!std::binary_search(child.begin(), child.end(), candidate)) {
            child[static_cast<std::size_t>(index)] = candidate;
            std::sort(child.begin(), child.end());
        }
    }
    if (!feasible_layout(child)) {
        return random_layout(random, generation, individual, 61);
    }
    return child;
}

std::uint64_t result_hash(
    const Layout& layout,
    const std::vector<double>& history,
    const SplineSurface& surface
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int node : layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(node));
    }
    for (const double value : history) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (const double value : surface.coefficients) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hash;
}

}  // namespace

Problem::Problem(std::string case_id) : case_id_(std::move(case_id)) {
    if (!case_id_.starts_with("t74_case")) {
        throw std::invalid_argument("invalid T74 case id " + case_id_);
    }
    const auto case_position = case_id_.find("case");
    const auto size_position = case_id_.find('_', case_position);
    if (case_position == std::string::npos || size_position == std::string::npos) {
        throw std::invalid_argument("invalid T74 case id " + case_id_);
    }
    wind_case_ = std::stoi(
        case_id_.substr(case_position + 4, size_position - case_position - 4)
    );
    const std::string size = case_id_.substr(size_position + 1);
    if (wind_case_ < 1 || wind_case_ > 5) {
        throw std::invalid_argument("invalid T74 wind case");
    }
    if (size == "small") cell_width_m_ = 231.0;
    else if (size == "medium") cell_width_m_ = 308.0;
    else if (size == "large") cell_width_m_ = 385.0;
    else throw std::invalid_argument("invalid T74 farm size");
    semantic_id_ = "t74_grid60_case" + std::to_string(wind_case_)
        + "_" + size + "_v1";
}

const std::string& Problem::case_id() const noexcept { return case_id_; }
const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}
int Problem::wind_case() const noexcept { return wind_case_; }
double Problem::cell_width_m() const noexcept { return cell_width_m_; }
int Problem::wind_state_count() const noexcept {
    return static_cast<int>(wind_states(wind_case_).size());
}
int Problem::paper_population() const noexcept { return population_size; }
int Problem::paper_generations() const noexcept {
    return paper_generation_count;
}
int Problem::paper_monte_carlo_layouts() const noexcept {
    return paper_mc_layouts;
}

Evaluation Problem::evaluate(const Layout& layout) const {
    return evaluate_layout(layout, wind_case_, cell_width_m_);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers < 1) {
        throw std::invalid_argument("T74 workers must be positive");
    }
    const int mc_layouts =
        config.monte_carlo_layouts < 0
        ? paper_mc_layouts
        : config.monte_carlo_layouts;
    const int generations =
        config.generations < 0
        ? paper_generation_count
        : config.generations;
    if (mc_layouts < 1 || generations < 1) {
        throw std::invalid_argument("T74 work budget must be positive");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng training_random(
        740000ULL + static_cast<std::uint64_t>(wind_case_) * 10ULL
        + static_cast<std::uint64_t>(std::llround(cell_width_m_))
    );
    const fode::CounterRng random(config.seed);

    static std::mutex cache_mutex;
    static std::map<std::string, std::shared_ptr<const CachedSurrogate>> cache;
    const std::string cache_key =
        case_id_ + ":" + std::to_string(mc_layouts);
    std::shared_ptr<const CachedSurrogate> cached;
    bool surrogate_reused = false;
    if (config.reuse_surrogate) {
        const std::lock_guard lock(cache_mutex);
        const auto iterator = cache.find(cache_key);
        if (iterator != cache.end()) {
            cached = iterator->second;
            surrogate_reused = true;
        }
    }
    if (!cached) {
        auto training = std::make_shared<CachedSurrogate>();
        std::vector<Layout> mc_population(
            static_cast<std::size_t>(mc_layouts)
        );
        std::vector<Evaluation> mc_evaluations(
            static_cast<std::size_t>(mc_layouts)
        );
        const auto mc_start = Clock::now();
        executor.parallel_for(0, mc_layouts, [&](const int sample) {
            auto layout = random_layout(
                training_random,
                0,
                static_cast<std::uint64_t>(sample),
                10
            );
            mc_evaluations[static_cast<std::size_t>(sample)] =
                evaluate_layout(layout, wind_case_, cell_width_m_);
            mc_population[static_cast<std::size_t>(sample)] =
                std::move(layout);
        });
        training->monte_carlo_truth_seconds = elapsed_seconds(mc_start);
        std::vector<double> cell_sum(candidate_count, 0.0);
        std::vector<int> cell_count(candidate_count, 0);
        for (int sample = 0; sample < mc_layouts; ++sample) {
            const auto& layout =
                mc_population[static_cast<std::size_t>(sample)];
            const auto& evaluation =
                mc_evaluations[static_cast<std::size_t>(sample)];
            for (int turbine = 0; turbine < turbine_count; ++turbine) {
                const int node = layout[static_cast<std::size_t>(turbine)];
                cell_sum[static_cast<std::size_t>(node)] +=
                    evaluation.turbine_power_kw[
                        static_cast<std::size_t>(turbine)
                    ];
                ++cell_count[static_cast<std::size_t>(node)];
            }
        }
        std::vector<double> surface_target(candidate_count, 0.0);
        for (int node = 0; node < candidate_count; ++node) {
            surface_target[static_cast<std::size_t>(node)] =
                cell_sum[static_cast<std::size_t>(node)]
                / static_cast<double>(
                    std::max(1, cell_count[static_cast<std::size_t>(node)])
                );
        }
        const auto training_start = Clock::now();
        training->surface = fit_mars_surface(surface_target);
        training->surrogate_training_seconds =
            elapsed_seconds(training_start);
        cached = training;
        if (config.reuse_surrogate) {
            const std::lock_guard lock(cache_mutex);
            cache[cache_key] = training;
        }
    }
    const SplineSurface& surface = cached->surface;

    std::vector<Layout> population(population_size);
    executor.parallel_for(0, population_size, [&](const int individual) {
        population[static_cast<std::size_t>(individual)] = random_layout(
            random,
            0,
            static_cast<std::uint64_t>(individual),
            20
        );
    });
    std::vector<Evaluation> evaluations(population_size);
    std::vector<double> history;
    history.reserve(static_cast<std::size_t>(generations));
    Evaluation initial_best;
    Evaluation best;
    Layout best_layout;
    double population_truth_seconds = 0.0;
    const auto algorithm_start = Clock::now();
    for (int generation = 0; generation < generations; ++generation) {
        const auto evaluation_start = Clock::now();
        executor.parallel_for(0, population_size, [&](const int individual) {
            evaluations[static_cast<std::size_t>(individual)] =
                evaluate_layout(
                    population[static_cast<std::size_t>(individual)],
                    wind_case_,
                    cell_width_m_
                );
        });
        population_truth_seconds += elapsed_seconds(evaluation_start);
        std::vector<int> order(population_size);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(
            order.begin(),
            order.end(),
            [&](const int first, const int second) {
                return evaluations[static_cast<std::size_t>(first)]
                    .expected_power_kw
                    > evaluations[static_cast<std::size_t>(second)]
                    .expected_power_kw;
            }
        );
        std::vector<Layout> sorted_population(population_size);
        std::vector<Evaluation> sorted_evaluations(population_size);
        for (int index = 0; index < population_size; ++index) {
            sorted_population[static_cast<std::size_t>(index)] =
                population[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(index)]
                )];
            sorted_evaluations[static_cast<std::size_t>(index)] =
                evaluations[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(index)]
                )];
        }
        population = std::move(sorted_population);
        evaluations = std::move(sorted_evaluations);
        if (generation == 0) initial_best = evaluations.front();
        if (
            !best.feasible
            || evaluations.front().expected_power_kw > best.expected_power_kw
        ) {
            best = evaluations.front();
            best_layout = population.front();
        }
        history.push_back(best.efficiency_percent);

        executor.parallel_for(0, population_size, [&](const int individual) {
            auto& layout = population[static_cast<std::size_t>(individual)];
            const auto& powers =
                evaluations[static_cast<std::size_t>(individual)]
                    .turbine_power_kw;
            const int worst_index = static_cast<int>(
                std::min_element(powers.begin(), powers.end()) - powers.begin()
            );
            const int worst_node =
                layout[static_cast<std::size_t>(worst_index)];
            int destination = random_empty_node(
                layout,
                random,
                static_cast<std::uint64_t>(generation),
                30,
                static_cast<std::uint64_t>(individual),
                0
            );
            if (!probability_event(
                    random,
                    config.variant,
                    0.5,
                    generation,
                    31,
                    individual
                )) {
                double best_prediction = -std::numeric_limits<double>::infinity();
                for (int candidate_index = 0;
                     candidate_index < mars_candidate_locations;
                     ++candidate_index) {
                    const int candidate = random_empty_node(
                        layout,
                        random,
                        generation,
                        32,
                        individual,
                        candidate_index
                    );
                    const double prediction = surface.predict(candidate);
                    if (prediction > best_prediction) {
                        best_prediction = prediction;
                        destination = candidate;
                    }
                }
            }
            layout = replace_node(layout, worst_node, destination);
        });

        std::vector<int> parents;
        for (int index = 0;
             index < static_cast<int>(population_size * elite_rate);
             ++index) {
            parents.push_back(index);
        }
        for (int index = static_cast<int>(population_size * elite_rate);
             index < population_size;
             ++index) {
            if (probability_event(
                    random,
                    config.variant,
                    selection_rate,
                    generation,
                    50,
                    index
                )) {
                parents.push_back(index);
            }
        }
        if (parents.size() < 2U) {
            parents = {0, 1};
        }
        std::vector<Layout> next_population(population_size);
        executor.parallel_for(0, population_size, [&](const int individual) {
            int first = random.integer(
                0,
                static_cast<int>(parents.size()),
                generation,
                51,
                individual
            );
            int second = random.integer(
                0,
                static_cast<int>(parents.size()),
                generation,
                52,
                individual
            );
            if (second == first) second = (second + 1) % parents.size();
            Layout child = crossover_layout(
                population[static_cast<std::size_t>(
                    parents[static_cast<std::size_t>(first)]
                )],
                population[static_cast<std::size_t>(
                    parents[static_cast<std::size_t>(second)]
                )],
                random,
                generation,
                individual
            );
            if (probability_event(
                    random,
                    config.variant,
                    mutation_rate,
                    generation,
                    70,
                    individual
                )) {
                const int removed_index = random.integer(
                    0,
                    turbine_count,
                    generation,
                    71,
                    individual
                );
                const int removed =
                    child[static_cast<std::size_t>(removed_index)];
                const int inserted = random_empty_node(
                    child,
                    random,
                    generation,
                    72,
                    individual,
                    0
                );
                child = replace_node(child, removed, inserted);
            }
            next_population[static_cast<std::size_t>(individual)] =
                std::move(child);
        });
        population = std::move(next_population);
    }
    const double algorithm_seconds = elapsed_seconds(algorithm_start);
    const auto receipt = executor.work_receipt();

    RunResult result;
    result.case_id = case_id_;
    result.problem_semantic_id = semantic_id_;
    result.method_semantic_id =
        config.variant == "paper_probability"
        ? "t74_siga_paper_probability_v1"
        : "t74_siga_source_normal_threshold_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.monte_carlo_layouts = mc_layouts;
    result.population = population_size;
    result.generations = generations;
    result.surrogate_reused = surrogate_reused;
    result.physical_fes =
        static_cast<std::uint64_t>(
            surrogate_reused ? 0 : mc_layouts
        )
        + static_cast<std::uint64_t>(population_size)
            * static_cast<std::uint64_t>(generations);
    result.initial_best = std::move(initial_best);
    result.best_evaluation = std::move(best);
    result.best_layout = std::move(best_layout);
    result.best_efficiency_history_percent = std::move(history);
    result.monte_carlo_truth_seconds =
        surrogate_reused ? 0.0 : cached->monte_carlo_truth_seconds;
    result.surrogate_training_seconds =
        surrogate_reused ? 0.0 : cached->surrogate_training_seconds;
    result.population_truth_seconds = population_truth_seconds;
    result.algorithm_seconds =
        std::max(0.0, algorithm_seconds - population_truth_seconds);
    result.end_to_end_seconds = elapsed_seconds(total_start);
    result.scientific_hash = result_hash(
        result.best_layout,
        result.best_efficiency_history_percent,
        surface
    );
    return result;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (int wind_case = 1; wind_case <= 5; ++wind_case) {
        for (const std::string size : {"small", "medium", "large"}) {
            result.push_back(
                "t74_case" + std::to_string(wind_case) + "_" + size
            );
        }
    }
    return result;
}

Layout regular_reference_layout() {
    Layout result;
    for (int row = 0; row < 10; row += 2) {
        for (int column = 0; column < 20; column += 2) {
            result.push_back(row * grid_side + column);
            if (result.size() == turbine_count) return result;
        }
    }
    for (int node = 0; node < candidate_count; ++node) {
        if (!std::binary_search(result.begin(), result.end(), node)) {
            result.push_back(node);
            std::sort(result.begin(), result.end());
        }
        if (result.size() == turbine_count) break;
    }
    return result;
}

}  // namespace core99::t74
