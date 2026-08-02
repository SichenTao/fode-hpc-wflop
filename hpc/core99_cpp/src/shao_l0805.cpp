/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0805 pure-C++ PCE-Kriging-MSP-EI-GA implementation
Paper: Shao et al.; DOI: 10.1016/J.ENERGY.2025.138820.
Public source, missing assets, conflicts, reconstruction, semantic IDs, HPC
and claim boundary: hpc/core99_cpp/include/core99/shao_l0805.hpp.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
Independent validator: scripts/validate_core99_l0805.py.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/shao_l0805.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::l0805 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double rotor_diameter_m = 126.0;
constexpr double minimum_spacing_diameters = 2.0;
constexpr int low_fidelity_direction_bins = 72;
constexpr int low_fidelity_speed_bins = 22;
constexpr int pce_samples = 50;
constexpr int population_size = 50;
constexpr double crossover_probability = 0.95;
constexpr double mutation_probability = 0.15;
constexpr double kriging_nugget = 1.0e-8;
constexpr int ga_stagnation_limit = 24;

struct TimedEvaluation {
    Evaluation value;
    double wake_seconds = 0.0;
    double pce_seconds = 0.0;
};

struct Prediction {
    double mean = 0.0;
    double standard_deviation = 0.0;
};

struct PceSelection {
    int degree = 1;
    TimedEvaluation first_layout_evaluation;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

double circular_distance_degrees(double left, double right) {
    double value = std::abs(left - right);
    while (value >= 360.0) value -= 360.0;
    return std::min(value, 360.0 - value);
}

std::vector<double> direction_weights() {
    std::vector<double> weights(low_fidelity_direction_bins, 0.0);
    for (int index = 0; index < low_fidelity_direction_bins; ++index) {
        const double degrees = 5.0 * static_cast<double>(index);
        auto peak = [&](const double center, const double width,
                        const double amplitude) {
            const double distance = circular_distance_degrees(degrees, center);
            return amplitude * std::exp(
                -0.5 * distance * distance / (width * width)
            );
        };
        weights[static_cast<std::size_t>(index)] = 0.12
            + peak(225.0, 25.0, 2.7)
            + peak(270.0, 23.0, 1.25)
            + peak(92.0, 18.0, 1.05)
            + peak(315.0, 24.0, 0.85)
            + peak(180.0, 22.0, 0.65)
            + peak(35.0, 20.0, 0.45);
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    for (double& value : weights) value /= total;
    return weights;
}

std::vector<double> speed_weights() {
    constexpr double shape = 2.0;
    constexpr double scale = 12.5;
    std::vector<double> weights(low_fidelity_speed_bins, 0.0);
    for (int index = 0; index < low_fidelity_speed_bins; ++index) {
        const double speed = 3.0 + static_cast<double>(index);
        const double ratio = speed / scale;
        weights[static_cast<std::size_t>(index)] =
            shape / scale * std::pow(ratio, shape - 1.0)
            * std::exp(-std::pow(ratio, shape));
    }
    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    for (double& value : weights) value /= total;
    return weights;
}

int weighted_quantile_index(
    const std::vector<double>& weights,
    const double probability
) {
    double cumulative = 0.0;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        cumulative += weights[index];
        if (probability <= cumulative || index + 1 == weights.size()) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(weights.size() - 1U);
}

CaseSpec make_spec(const std::string& case_id) {
    if (case_id == "l0805_case_i") {
        return {
            case_id, "l0805_case_i_n8_gaussian_proxy_v1",
            8, 9, 80, 343, 50, 30, false,
        };
    }
    if (case_id == "l0805_case_ii") {
        return {
            case_id, "l0805_case_ii_n16_gaussian_proxy_v1",
            16, 13, 160, 567, 50, 30, false,
        };
    }
    if (case_id == "l0805_case_iii") {
        return {
            case_id, "l0805_case_iii_n32_gaussian_proxy_v1",
            32, 17, 320, 839, 50, 30, false,
        };
    }
    if (case_id == "l0805_case_iv") {
        return {
            case_id, "l0805_case_iv_n8_adm_gaussian_proxy_v1",
            8, 9, 160, 272, 8, 1, true,
        };
    }
    throw std::invalid_argument("unknown L0805 paper case");
}

double squared_grid_distance(
    const CaseSpec& spec,
    const int first,
    const int second
) {
    const int first_x = first % spec.grid_width;
    const int first_y = first / spec.grid_width;
    const int second_x = second % spec.grid_width;
    const int second_y = second / spec.grid_width;
    const double dx = static_cast<double>(first_x - second_x);
    const double dy = static_cast<double>(first_y - second_y);
    return dx * dx + dy * dy;
}

bool can_insert(
    const CaseSpec& spec,
    const std::vector<int>& selected,
    const int candidate
) {
    const int candidates = spec.grid_width * spec.grid_width;
    if (
        candidate < 0 || candidate >= candidates
        || std::find(selected.begin(), selected.end(), candidate)
            != selected.end()
    ) {
        return false;
    }
    return std::all_of(
        selected.begin(), selected.end(), [&](const int other) {
            return squared_grid_distance(spec, other, candidate)
                >= minimum_spacing_diameters * minimum_spacing_diameters
                    - 1.0e-12;
        }
    );
}

Layout canonical_layout(const CaseSpec& spec, std::vector<int> values) {
    if (static_cast<int>(values.size()) != spec.turbines) {
        throw std::runtime_error("L0805 layout cardinality mismatch");
    }
    std::sort(values.begin(), values.end());
    if (std::adjacent_find(values.begin(), values.end()) != values.end()) {
        throw std::runtime_error("L0805 duplicate layout node");
    }
    return values;
}

bool feasible_layout(const CaseSpec& spec, const Layout& layout) {
    if (
        static_cast<int>(layout.size()) != spec.turbines
        || !std::is_sorted(layout.begin(), layout.end())
    ) {
        return false;
    }
    const int candidates = spec.grid_width * spec.grid_width;
    for (int first = 0; first < spec.turbines; ++first) {
        const int node = layout[static_cast<std::size_t>(first)];
        if (node < 0 || node >= candidates) return false;
        for (int second = first + 1; second < spec.turbines; ++second) {
            if (
                squared_grid_distance(
                    spec, node, layout[static_cast<std::size_t>(second)]
                ) < 4.0 - 1.0e-12
            ) {
                return false;
            }
        }
    }
    return true;
}

Layout keyed_random_layout(
    const CaseSpec& spec,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t phase
) {
    const int candidates = spec.grid_width * spec.grid_width;
    std::vector<std::pair<double, int>> order;
    order.reserve(static_cast<std::size_t>(candidates));
    for (int node = 0; node < candidates; ++node) {
        order.emplace_back(
            random.uniform(generation, phase, individual, node), node
        );
    }
    std::sort(order.begin(), order.end());
    std::vector<int> selected;
    selected.reserve(static_cast<std::size_t>(spec.turbines));
    for (const auto& [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(spec, selected, node)) selected.push_back(node);
        if (static_cast<int>(selected.size()) == spec.turbines) break;
    }
    if (static_cast<int>(selected.size()) != spec.turbines) {
        throw std::runtime_error("L0805 feasible random layout failed");
    }
    return canonical_layout(spec, std::move(selected));
}

int latin_multiplier(
    const int sample_count,
    const int turbine,
    const int phase
) {
    std::vector<int> coprime;
    for (int candidate = 1; candidate < sample_count; ++candidate) {
        if (std::gcd(candidate, sample_count) == 1) {
            coprime.push_back(candidate);
        }
    }
    if (coprime.empty()) {
        throw std::runtime_error("L0805 Latin multiplier unavailable");
    }
    const std::size_t index = static_cast<std::size_t>(
        (phase == 0 ? 17 : 29) * turbine + (phase == 0 ? 3 : 11)
    ) % coprime.size();
    return coprime[index];
}

Layout lhs_layout(
    const CaseSpec& spec,
    const fode::CounterRng& random,
    const int sample,
    const int sample_count
) {
    std::vector<int> selected;
    selected.reserve(static_cast<std::size_t>(spec.turbines));
    for (int turbine = 0; turbine < spec.turbines; ++turbine) {
        const int multiplier_x = latin_multiplier(sample_count, turbine, 0);
        const int multiplier_y = latin_multiplier(sample_count, turbine, 1);
        const int offset_x = random.integer(
            0, sample_count, 0, 80, turbine, 0
        );
        const int offset_y = random.integer(
            0, sample_count, 0, 81, turbine, 0
        );
        const int stratum_x =
            (multiplier_x * sample + offset_x) % sample_count;
        const int stratum_y =
            (multiplier_y * sample + offset_y) % sample_count;
        const double target_x =
            (static_cast<double>(stratum_x) + 0.5)
            / static_cast<double>(sample_count)
            * static_cast<double>(spec.grid_width - 1);
        const double target_y =
            (static_cast<double>(stratum_y) + 0.5)
            / static_cast<double>(sample_count)
            * static_cast<double>(spec.grid_width - 1);
        std::vector<std::pair<double, int>> order;
        const int candidates = spec.grid_width * spec.grid_width;
        order.reserve(static_cast<std::size_t>(candidates));
        for (int node = 0; node < candidates; ++node) {
            const double dx = static_cast<double>(node % spec.grid_width)
                - target_x;
            const double dy = static_cast<double>(node / spec.grid_width)
                - target_y;
            const double tie = 1.0e-9 * random.uniform(
                sample, 82, turbine, node
            );
            order.emplace_back(dx * dx + dy * dy + tie, node);
        }
        std::sort(order.begin(), order.end());
        bool inserted = false;
        for (const auto& [distance, node] : order) {
            static_cast<void>(distance);
            if (can_insert(spec, selected, node)) {
                selected.push_back(node);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            return keyed_random_layout(spec, random, 0, sample, 83);
        }
    }
    return canonical_layout(spec, std::move(selected));
}

std::vector<double> encode_layout(
    const CaseSpec& spec,
    const Layout& layout
) {
    std::vector<double> result(
        static_cast<std::size_t>(2 * spec.turbines), 0.0
    );
    const double scale = static_cast<double>(spec.grid_width - 1);
    for (int turbine = 0; turbine < spec.turbines; ++turbine) {
        const int node = layout[static_cast<std::size_t>(turbine)];
        result[static_cast<std::size_t>(2 * turbine)] =
            2.0 * static_cast<double>(node % spec.grid_width) / scale - 1.0;
        result[static_cast<std::size_t>(2 * turbine + 1)] =
            2.0 * static_cast<double>(node / spec.grid_width) / scale - 1.0;
    }
    return result;
}

bool layout_exists(const std::vector<Layout>& layouts, const Layout& layout) {
    return std::find(layouts.begin(), layouts.end(), layout) != layouts.end();
}

Layout mutate_layout(
    const CaseSpec& spec,
    const Layout& layout,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    std::vector<int> selected(layout.begin(), layout.end());
    const int remove = random.integer(
        0, spec.turbines, generation, 90, individual, 0, draw
    );
    selected.erase(selected.begin() + remove);
    const int candidates = spec.grid_width * spec.grid_width;
    std::vector<std::pair<double, int>> order;
    order.reserve(static_cast<std::size_t>(candidates));
    for (int node = 0; node < candidates; ++node) {
        order.emplace_back(
            random.uniform(generation, 91, individual, node, draw), node
        );
    }
    std::sort(order.begin(), order.end());
    for (const auto& [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(spec, selected, node)) {
            selected.push_back(node);
            return canonical_layout(spec, std::move(selected));
        }
    }
    return layout;
}

Layout crossover_layout(
    const CaseSpec& spec,
    const Layout& first,
    const Layout& second,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    std::vector<int> pool(first.begin(), first.end());
    pool.insert(pool.end(), second.begin(), second.end());
    std::sort(pool.begin(), pool.end());
    pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
    std::vector<std::pair<double, int>> order;
    const int candidates = spec.grid_width * spec.grid_width;
    order.reserve(static_cast<std::size_t>(candidates));
    for (int node : pool) {
        order.emplace_back(
            random.uniform(generation, 92, individual, node), node
        );
    }
    for (int node = 0; node < candidates; ++node) {
        if (!std::binary_search(pool.begin(), pool.end(), node)) {
            order.emplace_back(
                1.0 + random.uniform(generation, 93, individual, node), node
            );
        }
    }
    std::sort(order.begin(), order.end());
    std::vector<int> selected;
    selected.reserve(static_cast<std::size_t>(spec.turbines));
    for (const auto& [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(spec, selected, node)) selected.push_back(node);
        if (static_cast<int>(selected.size()) == spec.turbines) break;
    }
    if (static_cast<int>(selected.size()) != spec.turbines) return first;
    return canonical_layout(spec, std::move(selected));
}

double turbine_power_mw(const double speed_mps) {
    if (speed_mps < 3.0 || speed_mps >= 25.0) return 0.0;
    if (speed_mps >= 11.4) return 5.0;
    const double fraction = (speed_mps - 3.0) / (11.4 - 3.0);
    return 5.0 * fraction * fraction * (3.0 - 2.0 * fraction);
}

double state_power_mw(
    const CaseSpec& spec,
    const Layout& layout,
    const double direction_degrees,
    const double speed_mps
) {
    const double radians = direction_degrees * std::numbers::pi / 180.0;
    const double flow_x = -std::sin(radians);
    const double flow_y = -std::cos(radians);
    std::vector<std::pair<double, double>> points(
        static_cast<std::size_t>(spec.turbines)
    );
    for (int turbine = 0; turbine < spec.turbines; ++turbine) {
        const int node = layout[static_cast<std::size_t>(turbine)];
        points[static_cast<std::size_t>(turbine)] = {
            rotor_diameter_m * static_cast<double>(node % spec.grid_width),
            rotor_diameter_m * static_cast<double>(node / spec.grid_width),
        };
    }
    double total_power = 0.0;
    for (int target = 0; target < spec.turbines; ++target) {
        const auto [target_x, target_y] =
            points[static_cast<std::size_t>(target)];
        double total_deficit = 0.0;
        for (int source = 0; source < spec.turbines; ++source) {
            if (source == target) continue;
            const auto [source_x, source_y] =
                points[static_cast<std::size_t>(source)];
            const double dx = target_x - source_x;
            const double dy = target_y - source_y;
            const double downstream = dx * flow_x + dy * flow_y;
            if (!(downstream > 0.0)) continue;
            const double crosswind = -dx * flow_y + dy * flow_x;
            constexpr double wake_expansion = 0.05;
            constexpr double epsilon = 0.25;
            constexpr double thrust = 0.80;
            const double sigma_ratio =
                wake_expansion * downstream / rotor_diameter_m + epsilon;
            const double radical = std::max(
                0.0, 1.0 - thrust / (8.0 * sigma_ratio * sigma_ratio)
            );
            double amplitude = 1.0 - std::sqrt(radical);
            if (spec.high_fidelity_proxy) {
                amplitude *= 1.08 * (
                    1.0 + 0.04 * std::sin(
                        2.0 * std::numbers::pi * downstream
                        / (8.0 * rotor_diameter_m)
                    )
                );
            }
            const double sigma = sigma_ratio * rotor_diameter_m;
            total_deficit += amplitude * std::exp(
                -0.5 * crosswind * crosswind / (sigma * sigma)
            );
        }
        const double effective_speed = speed_mps
            * std::max(0.05, 1.0 - std::min(0.95, total_deficit));
        total_power += turbine_power_mw(effective_speed);
    }
    if (spec.high_fidelity_proxy) total_power *= 0.94;
    return total_power;
}

std::vector<std::vector<double>> orthogonal_table(
    const std::vector<double>& weights,
    const int maximum_degree,
    const bool circular_direction
) {
    const std::size_t count = weights.size();
    std::vector<std::vector<double>> table(
        count,
        std::vector<double>(static_cast<std::size_t>(maximum_degree + 1), 0.0)
    );
    for (int degree = 0; degree <= maximum_degree; ++degree) {
        for (std::size_t row = 0; row < count; ++row) {
            double coordinate = count == 1U ? 0.0
                : 2.0 * static_cast<double>(row)
                    / static_cast<double>(count - 1U) - 1.0;
            if (circular_direction) {
                double offset_degrees = 5.0 * static_cast<double>(row) - 225.0;
                while (offset_degrees > 180.0) offset_degrees -= 360.0;
                while (offset_degrees <= -180.0) offset_degrees += 360.0;
                coordinate = offset_degrees / 180.0;
            }
            table[row][static_cast<std::size_t>(degree)] =
                std::pow(coordinate, degree);
        }
        for (int previous = 0; previous < degree; ++previous) {
            double projection = 0.0;
            for (std::size_t row = 0; row < count; ++row) {
                projection += weights[row]
                    * table[row][static_cast<std::size_t>(degree)]
                    * table[row][static_cast<std::size_t>(previous)];
            }
            for (std::size_t row = 0; row < count; ++row) {
                table[row][static_cast<std::size_t>(degree)] -= projection
                    * table[row][static_cast<std::size_t>(previous)];
            }
        }
        double norm2 = 0.0;
        for (std::size_t row = 0; row < count; ++row) {
            const double value = table[row][static_cast<std::size_t>(degree)];
            norm2 += weights[row] * value * value;
        }
        if (!(norm2 > 1.0e-18)) {
            throw std::runtime_error("L0805 orthogonal basis rank failure");
        }
        const double inverse = 1.0 / std::sqrt(norm2);
        for (std::size_t row = 0; row < count; ++row) {
            table[row][static_cast<std::size_t>(degree)] *= inverse;
        }
    }
    return table;
}

std::vector<std::pair<int, int>> total_degree_terms(const int degree) {
    std::vector<std::pair<int, int>> terms;
    for (int total = 0; total <= degree; ++total) {
        for (int direction = 0; direction <= total; ++direction) {
            terms.emplace_back(direction, total - direction);
        }
    }
    return terms;
}

bool cholesky_in_place(std::vector<std::vector<double>>& matrix) {
    for (std::size_t row = 0; row < matrix.size(); ++row) {
        for (std::size_t column = 0; column <= row; ++column) {
            double value = matrix[row][column];
            for (std::size_t inner = 0; inner < column; ++inner) {
                value -= matrix[row][inner] * matrix[column][inner];
            }
            if (row == column) {
                if (!(value > 1.0e-16)) return false;
                matrix[row][column] = std::sqrt(value);
            } else {
                matrix[row][column] = value / matrix[column][column];
            }
        }
        for (std::size_t column = row + 1; column < matrix.size(); ++column) {
            matrix[row][column] = 0.0;
        }
    }
    return true;
}

std::vector<double> solve_cholesky(
    const std::vector<std::vector<double>>& factor,
    const std::vector<double>& right
) {
    std::vector<double> forward(right.size(), 0.0);
    for (std::size_t row = 0; row < right.size(); ++row) {
        double value = right[row];
        for (std::size_t column = 0; column < row; ++column) {
            value -= factor[row][column] * forward[column];
        }
        forward[row] = value / factor[row][row];
    }
    std::vector<double> result(right.size(), 0.0);
    for (std::size_t row = right.size(); row-- > 0;) {
        double value = forward[row];
        for (std::size_t column = row + 1; column < right.size(); ++column) {
            value -= factor[column][row] * result[column];
        }
        result[row] = value / factor[row][row];
    }
    return result;
}

std::vector<double> ridge_fit(
    const std::vector<std::vector<double>>& design,
    const std::vector<double>& responses,
    const std::vector<bool>* include = nullptr
) {
    const std::size_t columns = design.front().size();
    std::vector<std::vector<double>> normal(
        columns, std::vector<double>(columns, 0.0)
    );
    std::vector<double> right(columns, 0.0);
    for (std::size_t row = 0; row < design.size(); ++row) {
        if (include != nullptr && !(*include)[row]) continue;
        for (std::size_t first = 0; first < columns; ++first) {
            right[first] += design[row][first] * responses[row];
            for (std::size_t second = 0; second <= first; ++second) {
                normal[first][second] +=
                    design[row][first] * design[row][second];
            }
        }
    }
    for (std::size_t first = 0; first < columns; ++first) {
        for (std::size_t second = first + 1; second < columns; ++second) {
            normal[first][second] = normal[second][first];
        }
        normal[first][first] += first == 0U ? 1.0e-12 : 1.0e-8;
    }
    if (!cholesky_in_place(normal)) {
        throw std::runtime_error("L0805 ridge factorization failure");
    }
    return solve_cholesky(normal, right);
}

struct PceSamples {
    std::vector<int> direction_indices;
    std::vector<int> speed_indices;
};

PceSamples pce_lhs_samples(const std::uint64_t seed) {
    const auto direction = direction_weights();
    const auto speed = speed_weights();
    const fode::CounterRng random(seed);
    PceSamples samples;
    samples.direction_indices.resize(pce_samples);
    samples.speed_indices.resize(pce_samples);
    const int direction_offset = random.integer(0, pce_samples, 0, 100, 0);
    const int speed_offset = random.integer(0, pce_samples, 0, 101, 0);
    for (int sample = 0; sample < pce_samples; ++sample) {
        const int direction_stratum = (17 * sample + direction_offset)
            % pce_samples;
        const int speed_stratum = (29 * sample + speed_offset) % pce_samples;
        const double direction_probability =
            (static_cast<double>(direction_stratum) + 0.5) / pce_samples;
        const double speed_probability =
            (static_cast<double>(speed_stratum) + 0.5) / pce_samples;
        samples.direction_indices[static_cast<std::size_t>(sample)] =
            weighted_quantile_index(direction, direction_probability);
        samples.speed_indices[static_cast<std::size_t>(sample)] =
            weighted_quantile_index(speed, speed_probability);
    }
    return samples;
}

std::vector<std::vector<double>> pce_design(
    const PceSamples& samples,
    const int degree
) {
    const auto direction = direction_weights();
    const auto speed = speed_weights();
    const auto direction_basis = orthogonal_table(direction, degree, true);
    const auto speed_basis = orthogonal_table(speed, degree, false);
    const auto terms = total_degree_terms(degree);
    std::vector<std::vector<double>> design(
        pce_samples, std::vector<double>(terms.size(), 0.0)
    );
    for (int sample = 0; sample < pce_samples; ++sample) {
        const int direction_index =
            samples.direction_indices[static_cast<std::size_t>(sample)];
        const int speed_index =
            samples.speed_indices[static_cast<std::size_t>(sample)];
        for (std::size_t term = 0; term < terms.size(); ++term) {
            const auto [direction_degree, speed_degree] = terms[term];
            design[static_cast<std::size_t>(sample)][term] =
                direction_basis[static_cast<std::size_t>(direction_index)]
                    [static_cast<std::size_t>(direction_degree)]
                * speed_basis[static_cast<std::size_t>(speed_index)]
                    [static_cast<std::size_t>(speed_degree)];
        }
    }
    return design;
}

std::vector<double> pce_responses(
    const CaseSpec& spec,
    const Layout& layout,
    const PceSamples& samples
) {
    std::vector<double> values(pce_samples, 0.0);
    for (int sample = 0; sample < pce_samples; ++sample) {
        const int direction =
            samples.direction_indices[static_cast<std::size_t>(sample)];
        const int speed =
            samples.speed_indices[static_cast<std::size_t>(sample)];
        values[static_cast<std::size_t>(sample)] = state_power_mw(
            spec, layout, 5.0 * static_cast<double>(direction),
            3.0 + static_cast<double>(speed)
        );
    }
    return values;
}

Evaluation geometry_evaluation(
    const CaseSpec& spec,
    const Layout& layout
) {
    Evaluation result;
    result.feasible = feasible_layout(spec, layout);
    result.minimum_spacing_margin_m =
        std::numeric_limits<double>::infinity();
    if (!result.feasible) {
        result.minimum_spacing_margin_m =
            -std::numeric_limits<double>::infinity();
        return result;
    }
    for (int first = 0; first < spec.turbines; ++first) {
        for (int second = first + 1; second < spec.turbines; ++second) {
            const double spacing = std::sqrt(squared_grid_distance(
                spec, layout[static_cast<std::size_t>(first)],
                layout[static_cast<std::size_t>(second)]
            ));
            result.minimum_spacing_margin_m = std::min(
                result.minimum_spacing_margin_m,
                (spacing - minimum_spacing_diameters) * rotor_diameter_m
            );
        }
    }
    return result;
}

PceSelection select_pce_degree(
    const CaseSpec& spec,
    const Layout& layout,
    const std::uint64_t seed
) {
    if (spec.high_fidelity_proxy) {
        throw std::invalid_argument("L0805 PCE selection on high fidelity");
    }
    const auto samples = pce_lhs_samples(seed);
    auto started = Clock::now();
    const auto responses = pce_responses(spec, layout, samples);
    const double wake_seconds = elapsed_seconds(started);
    started = Clock::now();
    int best_degree = 1;
    double best_error = std::numeric_limits<double>::infinity();
    for (int degree = 1; degree <= 8; ++degree) {
        const auto design = pce_design(samples, degree);
        double squared_error = 0.0;
        for (int fold = 0; fold < 5; ++fold) {
            std::vector<bool> include(pce_samples, true);
            for (int sample = fold; sample < pce_samples; sample += 5) {
                include[static_cast<std::size_t>(sample)] = false;
            }
            const auto coefficients = ridge_fit(design, responses, &include);
            for (int sample = fold; sample < pce_samples; sample += 5) {
                const double prediction = std::inner_product(
                    design[static_cast<std::size_t>(sample)].begin(),
                    design[static_cast<std::size_t>(sample)].end(),
                    coefficients.begin(), 0.0
                );
                const double difference = prediction
                    - responses[static_cast<std::size_t>(sample)];
                squared_error += difference * difference;
            }
        }
        if (squared_error < best_error) {
            best_error = squared_error;
            best_degree = degree;
        }
    }
    const auto design = pce_design(samples, best_degree);
    const auto coefficients = ridge_fit(design, responses);
    PceSelection result;
    result.degree = best_degree;
    result.first_layout_evaluation.value = geometry_evaluation(spec, layout);
    result.first_layout_evaluation.value.mean_power_mw =
        std::max(0.0, coefficients.front());
    result.first_layout_evaluation.value.aep_gwh =
        result.first_layout_evaluation.value.mean_power_mw * 8760.0 / 1000.0;
    result.first_layout_evaluation.value.pce_degree = best_degree;
    result.first_layout_evaluation.value.physical_wake_simulations =
        pce_samples;
    result.first_layout_evaluation.wake_seconds = wake_seconds;
    result.first_layout_evaluation.pce_seconds = elapsed_seconds(started);
    return result;
}

TimedEvaluation evaluate_problem(
    const CaseSpec& spec,
    const Layout& layout,
    const std::uint64_t seed,
    const int pce_degree
) {
    TimedEvaluation result;
    result.value = geometry_evaluation(spec, layout);
    if (!result.value.feasible) {
        return result;
    }
    if (spec.high_fidelity_proxy) {
        const auto started = Clock::now();
        const auto direction = direction_weights();
        double weighted_power = 0.0;
        for (int sample = 0; sample < 8; ++sample) {
            const double probability =
                (static_cast<double>(sample) + 0.5) / 8.0;
            const int direction_index =
                weighted_quantile_index(direction, probability);
            weighted_power += state_power_mw(
                spec, layout, 5.0 * direction_index, 8.0
            ) / 8.0;
        }
        result.wake_seconds = elapsed_seconds(started);
        result.value.mean_power_mw = weighted_power;
        result.value.aep_gwh = weighted_power * 8760.0 / 1000.0;
        result.value.pce_degree = 0;
        result.value.physical_wake_simulations = 8;
        return result;
    }
    if (pce_degree == 0) {
        const auto started = Clock::now();
        const auto direction = direction_weights();
        const auto speed = speed_weights();
        double weighted_power = 0.0;
        for (int direction_index = 0;
             direction_index < low_fidelity_direction_bins;
             ++direction_index) {
            for (int speed_index = 0;
                 speed_index < low_fidelity_speed_bins;
                 ++speed_index) {
                weighted_power +=
                    direction[static_cast<std::size_t>(direction_index)]
                    * speed[static_cast<std::size_t>(speed_index)]
                    * state_power_mw(
                        spec, layout, 5.0 * direction_index,
                        3.0 + speed_index
                    );
            }
        }
        result.wake_seconds = elapsed_seconds(started);
        result.value.mean_power_mw = weighted_power;
        result.value.aep_gwh = weighted_power * 8760.0 / 1000.0;
        result.value.pce_degree = 0;
        result.value.physical_wake_simulations =
            low_fidelity_direction_bins * low_fidelity_speed_bins;
        return result;
    }
    if (pce_degree < 1 || pce_degree > 8) {
        throw std::invalid_argument("L0805 identifiable PCE degree invalid");
    }
    const auto samples = pce_lhs_samples(seed);
    auto started = Clock::now();
    const auto responses = pce_responses(spec, layout, samples);
    result.wake_seconds = elapsed_seconds(started);
    started = Clock::now();
    const auto design = pce_design(samples, pce_degree);
    const auto coefficients = ridge_fit(design, responses);
    result.pce_seconds = elapsed_seconds(started);
    result.value.mean_power_mw = std::max(0.0, coefficients.front());
    result.value.aep_gwh = result.value.mean_power_mw * 8760.0 / 1000.0;
    result.value.pce_degree = pce_degree;
    result.value.physical_wake_simulations = pce_samples;
    return result;
}

std::vector<double> trend_features(const std::vector<double>& input) {
    std::vector<double> values;
    values.reserve(1U + 2U * input.size());
    values.push_back(1.0);
    values.insert(values.end(), input.begin(), input.end());
    for (const double item : input) values.push_back(item * item);
    return values;
}

class RecursiveTrend {
public:
    void initialize(
        const std::vector<std::vector<double>>& inputs,
        const std::vector<double>& responses
    ) {
        const std::size_t dimension = trend_features(inputs.front()).size();
        std::vector<std::vector<double>> normal(
            dimension, std::vector<double>(dimension, 0.0)
        );
        std::vector<double> right(dimension, 0.0);
        for (std::size_t row = 0; row < inputs.size(); ++row) {
            const auto features = trend_features(inputs[row]);
            for (std::size_t first = 0; first < dimension; ++first) {
                right[first] += features[first] * responses[row];
                for (std::size_t second = 0; second <= first; ++second) {
                    normal[first][second] += features[first] * features[second];
                }
            }
        }
        for (std::size_t first = 0; first < dimension; ++first) {
            for (std::size_t second = first + 1; second < dimension; ++second) {
                normal[first][second] = normal[second][first];
            }
            normal[first][first] += first == 0U ? 1.0e-8 : 1.0e-4;
        }
        auto factor = normal;
        if (!cholesky_in_place(factor)) {
            throw std::runtime_error("L0805 trend factorization failed");
        }
        coefficients_ = solve_cholesky(factor, right);
        inverse_.assign(dimension, std::vector<double>(dimension, 0.0));
        for (std::size_t column = 0; column < dimension; ++column) {
            std::vector<double> unit(dimension, 0.0);
            unit[column] = 1.0;
            const auto solution = solve_cholesky(factor, unit);
            for (std::size_t row = 0; row < dimension; ++row) {
                inverse_[row][column] = solution[row];
            }
        }
    }

    void append(const std::vector<double>& input, const double response) {
        const auto features = trend_features(input);
        std::vector<double> projected(features.size(), 0.0);
        for (std::size_t row = 0; row < features.size(); ++row) {
            projected[row] = std::inner_product(
                inverse_[row].begin(), inverse_[row].end(),
                features.begin(), 0.0
            );
        }
        const double denominator = 1.0 + std::inner_product(
            features.begin(), features.end(), projected.begin(), 0.0
        );
        const double residual = response - predict_features(features);
        for (std::size_t row = 0; row < features.size(); ++row) {
            coefficients_[row] += projected[row] * residual / denominator;
        }
        for (std::size_t row = 0; row < features.size(); ++row) {
            for (std::size_t column = 0; column < features.size(); ++column) {
                inverse_[row][column] -=
                    projected[row] * projected[column] / denominator;
            }
        }
    }

    [[nodiscard]] double predict(const std::vector<double>& input) const {
        return predict_features(trend_features(input));
    }

private:
    std::vector<double> coefficients_;
    std::vector<std::vector<double>> inverse_;

    double predict_features(const std::vector<double>& features) const {
        return std::inner_product(
            coefficients_.begin(), coefficients_.end(), features.begin(), 0.0
        );
    }
};

class Kriging {
public:
    void initialize(
        const std::vector<std::vector<double>>& inputs,
        const std::vector<double>& responses
    ) {
        if (inputs.empty() || inputs.size() != responses.size()) {
            throw std::invalid_argument("L0805 Kriging training mismatch");
        }
        inputs_ = inputs;
        responses_ = responses;
        trend_.initialize(inputs_, responses_);
        theta_ = select_theta();
        factorize();
        update_alpha();
    }

    void append(const std::vector<double>& input, const double response) {
        const std::size_t old_size = inputs_.size();
        std::vector<double> row(old_size + 1U, 0.0);
        std::vector<double> forward(old_size, 0.0);
        for (std::size_t index = 0; index < old_size; ++index) {
            double value = kernel(input, inputs_[index], theta_);
            for (std::size_t column = 0; column < index; ++column) {
                value -= factors_[index][column] * forward[column];
            }
            forward[index] = value / factors_[index][index];
            row[index] = forward[index];
        }
        const double diagonal = std::max(
            kriging_nugget,
            1.0 + kriging_nugget - std::inner_product(
                forward.begin(), forward.end(), forward.begin(), 0.0
            )
        );
        row[old_size] = std::sqrt(diagonal);
        factors_.push_back(std::move(row));
        inputs_.push_back(input);
        responses_.push_back(response);
        trend_.append(input, response);
        update_alpha();
    }

    [[nodiscard]] Prediction predict(const std::vector<double>& input) const {
        double mean = trend_.predict(input);
        double maximum_correlation = 0.0;
        for (std::size_t index = 0; index < inputs_.size(); ++index) {
            const double correlation = kernel(input, inputs_[index], theta_);
            mean += alpha_[index] * correlation;
            maximum_correlation = std::max(maximum_correlation, correlation);
        }
        const double variance_fraction = std::max(
            kriging_nugget, 1.0 - maximum_correlation * maximum_correlation
        );
        return {
            mean,
            std::sqrt(std::max(1.0e-12, process_variance_)
                      * variance_fraction),
        };
    }

    [[nodiscard]] double theta() const noexcept { return theta_; }

private:
    std::vector<std::vector<double>> inputs_;
    std::vector<double> responses_;
    std::vector<std::vector<double>> factors_;
    std::vector<double> alpha_;
    RecursiveTrend trend_;
    double theta_ = 1.0;
    double process_variance_ = 1.0;

    static double kernel(
        const std::vector<double>& first,
        const std::vector<double>& second,
        const double theta
    ) {
        double distance2 = 0.0;
        for (std::size_t index = 0; index < first.size(); ++index) {
            const double delta = first[index] - second[index];
            distance2 += delta * delta;
        }
        return std::exp(
            -theta * distance2 / static_cast<double>(first.size())
        );
    }

    double select_theta() const {
        constexpr double candidates[]{0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
        const std::size_t count = std::min<std::size_t>(64, inputs_.size());
        double best_theta = candidates[0];
        double best_score = std::numeric_limits<double>::infinity();
        for (const double candidate : candidates) {
            std::vector<std::vector<double>> factor(
                count, std::vector<double>(count, 0.0)
            );
            std::vector<double> residuals(count, 0.0);
            for (std::size_t row = 0; row < count; ++row) {
                residuals[row] = responses_[row] - trend_.predict(inputs_[row]);
                for (std::size_t column = 0; column <= row; ++column) {
                    factor[row][column] = kernel(
                        inputs_[row], inputs_[column], candidate
                    );
                }
                factor[row][row] += kriging_nugget;
            }
            if (!cholesky_in_place(factor)) continue;
            const auto weights = solve_cholesky(factor, residuals);
            const double quadratic = std::inner_product(
                residuals.begin(), residuals.end(), weights.begin(), 0.0
            );
            if (!(quadratic > 0.0)) continue;
            double log_determinant = 0.0;
            for (std::size_t index = 0; index < count; ++index) {
                log_determinant += 2.0 * std::log(factor[index][index]);
            }
            const double score = static_cast<double>(count) * std::log(
                quadratic / static_cast<double>(count)
            ) + log_determinant;
            if (score < best_score) {
                best_score = score;
                best_theta = candidate;
            }
        }
        return best_theta;
    }

    void factorize() {
        const std::size_t count = inputs_.size();
        factors_.assign(count, std::vector<double>(count, 0.0));
        for (std::size_t row = 0; row < count; ++row) {
            for (std::size_t column = 0; column <= row; ++column) {
                factors_[row][column] = kernel(
                    inputs_[row], inputs_[column], theta_
                );
            }
            factors_[row][row] += kriging_nugget;
        }
        if (!cholesky_in_place(factors_)) {
            throw std::runtime_error("L0805 Kriging factorization failed");
        }
    }

    void update_alpha() {
        std::vector<double> residuals(responses_.size(), 0.0);
        for (std::size_t index = 0; index < responses_.size(); ++index) {
            residuals[index] = responses_[index]
                - trend_.predict(inputs_[index]);
        }
        alpha_ = solve_cholesky(factors_, residuals);
        process_variance_ = std::max(
            1.0e-12,
            std::inner_product(
                residuals.begin(), residuals.end(), alpha_.begin(), 0.0
            ) / static_cast<double>(residuals.size())
        );
    }
};

struct AcquisitionResult {
    Layout layout;
    double value = -std::numeric_limits<double>::infinity();
    std::uint64_t evaluations = 0;
};

AcquisitionResult optimize_acquisition(
    const CaseSpec& spec,
    const Kriging& surrogate,
    const std::vector<Layout>& truth_layouts,
    const double best_observed,
    const bool ei_phase,
    const int maximum_generations,
    const int infill,
    const fode::CounterRng& random,
    fode::PersistentExecutor& executor,
    double& inference_seconds
) {
    std::vector<Layout> population(population_size);
    std::vector<double> fitness(population_size, 0.0);
    for (int individual = 0; individual < population_size; ++individual) {
        population[static_cast<std::size_t>(individual)] =
            keyed_random_layout(spec, random, infill, individual, 110);
    }
    AcquisitionResult result;
    int stagnation = 0;
    for (int generation = 0; generation < maximum_generations; ++generation) {
        const auto started = Clock::now();
        executor.parallel_for(0, population_size, [&](const int individual) {
            const Layout& layout =
                population[static_cast<std::size_t>(individual)];
            const Prediction prediction = surrogate.predict(
                encode_layout(spec, layout)
            );
            fitness[static_cast<std::size_t>(individual)] = ei_phase
                ? expected_improvement(
                    prediction.mean, prediction.standard_deviation,
                    best_observed
                )
                : prediction.mean;
            if (ei_phase && layout_exists(truth_layouts, layout)) {
                fitness[static_cast<std::size_t>(individual)] = -1.0;
            }
        });
        inference_seconds += elapsed_seconds(started);
        result.evaluations += population_size;
        int generation_best = 0;
        for (int individual = 1; individual < population_size; ++individual) {
            if (fitness[static_cast<std::size_t>(individual)]
                > fitness[static_cast<std::size_t>(generation_best)]) {
                generation_best = individual;
            }
        }
        const double value = fitness[static_cast<std::size_t>(generation_best)];
        if (value > result.value + 1.0e-12) {
            result.value = value;
            result.layout = population[static_cast<std::size_t>(generation_best)];
            stagnation = 0;
        } else {
            ++stagnation;
        }
        if (stagnation >= ga_stagnation_limit) break;
        std::vector<Layout> next(population_size);
        next[0] = result.layout;
        executor.parallel_for(1, population_size, [&](const int individual) {
            auto tournament = [&](const std::uint64_t draw) {
                const int first = random.integer(
                    0, population_size, infill * 1001ULL + generation,
                    111, individual, 0, draw
                );
                const int second = random.integer(
                    0, population_size, infill * 1001ULL + generation,
                    112, individual, 0, draw
                );
                return fitness[static_cast<std::size_t>(first)]
                    >= fitness[static_cast<std::size_t>(second)]
                    ? first : second;
            };
            const int first = tournament(0);
            const int second = tournament(1);
            Layout child = random.uniform(
                infill * 1001ULL + generation, 113, individual
            ) < crossover_probability
                ? crossover_layout(
                    spec, population[static_cast<std::size_t>(first)],
                    population[static_cast<std::size_t>(second)], random,
                    infill * 1001ULL + generation, individual
                )
                : population[static_cast<std::size_t>(first)];
            if (random.uniform(
                    infill * 1001ULL + generation, 114, individual
                ) < mutation_probability) {
                child = mutate_layout(
                    spec, child, random, infill * 1001ULL + generation,
                    individual, 0
                );
            }
            next[static_cast<std::size_t>(individual)] = std::move(child);
        });
        population = std::move(next);
    }
    return result;
}

}  // namespace

struct Problem::Impl {
    explicit Impl(std::string case_id) : spec(make_spec(case_id)) {}
    CaseSpec spec;
};

Problem::Problem(std::string case_id)
    : impl_(std::make_unique<Impl>(std::move(case_id))) {}
Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const CaseSpec& Problem::spec() const noexcept { return impl_->spec; }

Evaluation Problem::evaluate(
    const Layout& layout,
    const std::uint64_t seed,
    const int pce_degree
) const {
    return evaluate_problem(impl_->spec, layout, seed, pce_degree).value;
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0 || config.maximum_ga_generations <= 0) {
        throw std::invalid_argument("L0805 run configuration invalid");
    }
    const CaseSpec& paper = impl_->spec;
    int initial_count = config.initial_samples > 0
        ? config.initial_samples : paper.initial_layout_samples;
    int target_count = config.target_truth_calls > 0
        ? config.target_truth_calls : paper.target_layout_evaluations;
    int maximum_generations = config.maximum_ga_generations;
    if (config.smoke) {
        initial_count = std::min(initial_count, 24);
        target_count = std::min(target_count, initial_count + 6);
        maximum_generations = std::min(maximum_generations, 8);
    }
    if (initial_count < 8 || target_count <= initial_count) {
        throw std::invalid_argument("L0805 truth-call contract invalid");
    }

    const fode::CounterRng random(config.seed);
    const std::uint64_t pce_sampling_seed = config.seed ^ 0x805005ULL;
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const auto total_started = Clock::now();
    double wake_seconds = 0.0;
    double pce_seconds = 0.0;
    double training_seconds = 0.0;
    double inference_seconds = 0.0;

    std::vector<Layout> layouts(static_cast<std::size_t>(initial_count));
    for (int sample = 0; sample < initial_count; ++sample) {
        layouts[static_cast<std::size_t>(sample)] = lhs_layout(
            paper, random, sample, initial_count
        );
    }
    int selected_degree = paper.high_fidelity_proxy ? 0 : 2;
    std::optional<TimedEvaluation> selected_first;
    if (!config.smoke && !paper.high_fidelity_proxy) {
        PceSelection selection = select_pce_degree(
            paper, layouts.front(), pce_sampling_seed
        );
        selected_degree = selection.degree;
        selected_first = std::move(selection.first_layout_evaluation);
        wake_seconds += selected_first->wake_seconds;
        pce_seconds += selected_first->pce_seconds;
    }
    std::vector<TimedEvaluation> timed(
        static_cast<std::size_t>(initial_count)
    );
    const int first_parallel_sample = selected_first.has_value() ? 1 : 0;
    if (selected_first.has_value()) timed.front() = *selected_first;
    const auto initial_batch_started = Clock::now();
    executor.parallel_for(first_parallel_sample, initial_count, [&](const int sample) {
        timed[static_cast<std::size_t>(sample)] = evaluate_problem(
            paper, layouts[static_cast<std::size_t>(sample)],
            pce_sampling_seed, selected_degree
        );
    });
    const double initial_batch_wall = elapsed_seconds(initial_batch_started);
    double initial_wake_work = 0.0;
    double initial_pce_work = 0.0;
    for (int sample = first_parallel_sample; sample < initial_count; ++sample) {
        const TimedEvaluation& item = timed[static_cast<std::size_t>(sample)];
        initial_wake_work += item.wake_seconds;
        initial_pce_work += item.pce_seconds;
    }
    const double initial_work = initial_wake_work + initial_pce_work;
    const double wall_scale = initial_work > 0.0
        ? initial_batch_wall / initial_work : 0.0;
    wake_seconds += initial_wake_work * wall_scale;
    pce_seconds += initial_pce_work * wall_scale;

    std::vector<Evaluation> evaluations(static_cast<std::size_t>(initial_count));
    std::vector<double> responses(static_cast<std::size_t>(initial_count));
    std::vector<std::vector<double>> inputs(
        static_cast<std::size_t>(initial_count)
    );
    int best_index = 0;
    for (int sample = 0; sample < initial_count; ++sample) {
        const TimedEvaluation& item = timed[static_cast<std::size_t>(sample)];
        evaluations[static_cast<std::size_t>(sample)] = item.value;
        responses[static_cast<std::size_t>(sample)] = item.value.aep_gwh;
        inputs[static_cast<std::size_t>(sample)] = encode_layout(
            paper, layouts[static_cast<std::size_t>(sample)]
        );
        if (responses[static_cast<std::size_t>(sample)]
            > responses[static_cast<std::size_t>(best_index)]) {
            best_index = sample;
        }
    }

    RunResult result;
    result.case_id = paper.case_id;
    result.problem_semantic_id = paper.problem_semantic_id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.selected_pce_degree = selected_degree;
    result.initial_samples = initial_count;
    result.initial_best = evaluations[static_cast<std::size_t>(best_index)];
    result.best_evaluation = result.initial_best;
    result.best_layout = layouts[static_cast<std::size_t>(best_index)];
    result.best_history_gwh.push_back(result.best_evaluation.aep_gwh);
    result.physical_wake_simulations = std::accumulate(
        evaluations.begin(), evaluations.end(), std::uint64_t{0},
        [](const std::uint64_t sum, const Evaluation& evaluation) {
            return sum + evaluation.physical_wake_simulations;
        }
    );

    Kriging surrogate;
    auto started = Clock::now();
    surrogate.initialize(inputs, responses);
    training_seconds += elapsed_seconds(started);
    result.selected_kernel_theta = surrogate.theta();
    bool ei_phase = false;

    for (int infill = initial_count; infill < target_count; ++infill) {
        AcquisitionResult acquisition = optimize_acquisition(
            paper, surrogate, layouts, result.best_evaluation.aep_gwh,
            ei_phase, maximum_generations, infill, random, executor,
            inference_seconds
        );
        result.surrogate_fes += acquisition.evaluations;
        if (!ei_phase && layout_exists(layouts, acquisition.layout)) {
            ei_phase = true;
            acquisition = optimize_acquisition(
                paper, surrogate, layouts, result.best_evaluation.aep_gwh,
                true, maximum_generations, infill, random, executor,
                inference_seconds
            );
            result.surrogate_fes += acquisition.evaluations;
        }
        if (acquisition.layout.empty()) {
            acquisition.layout = keyed_random_layout(
                paper, random, infill, 0, 115
            );
        }
        for (int attempt = 0; layout_exists(layouts, acquisition.layout);
             ++attempt) {
            if (attempt > paper.grid_width * paper.grid_width) {
                acquisition.layout = keyed_random_layout(
                    paper, random, infill, attempt, 116
                );
            } else {
                acquisition.layout = mutate_layout(
                    paper, acquisition.layout, random, infill, 0, attempt + 1
                );
            }
        }
        if (ei_phase) ++result.ei_infills;
        else ++result.msp_infills;

        const TimedEvaluation truth = evaluate_problem(
            paper, acquisition.layout, pce_sampling_seed, selected_degree
        );
        wake_seconds += truth.wake_seconds;
        pce_seconds += truth.pce_seconds;
        result.physical_wake_simulations +=
            truth.value.physical_wake_simulations;
        layouts.push_back(acquisition.layout);
        evaluations.push_back(truth.value);
        responses.push_back(truth.value.aep_gwh);
        inputs.push_back(encode_layout(paper, acquisition.layout));
        started = Clock::now();
        surrogate.append(inputs.back(), responses.back());
        training_seconds += elapsed_seconds(started);
        if (truth.value.aep_gwh > result.best_evaluation.aep_gwh) {
            result.best_evaluation = truth.value;
            result.best_layout = acquisition.layout;
        }
        result.best_history_gwh.push_back(result.best_evaluation.aep_gwh);
        if (!ei_phase && result.msp_infills >= std::max(8, initial_count / 3)) {
            ei_phase = true;
        }
    }

    result.truth_calls = target_count;
    result.evaluator_seconds = wake_seconds;
    result.pce_seconds = pce_seconds;
    result.surrogate_training_seconds = training_seconds;
    result.surrogate_inference_seconds = inference_seconds;
    result.end_to_end_seconds = elapsed_seconds(total_started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - wake_seconds - pce_seconds
            - training_seconds - inference_seconds
    );
    result.observed_workers = executor.work_receipt().distinct_participants;
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_hash(hash, config.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(target_count));
    hash = mix_hash(hash, static_cast<std::uint64_t>(selected_degree));
    hash = mix_hash(hash, result.surrogate_fes);
    for (const int node : result.best_layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(node));
    }
    for (const double value : result.best_history_gwh) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    result.scientific_hash = hash;
    return result;
}

BatchReceipt Problem::profile_batch(
    const int layouts_count,
    const std::uint64_t seed,
    const int workers
) const {
    if (layouts_count <= 0 || workers <= 0) {
        throw std::invalid_argument("L0805 batch profile configuration");
    }
    const fode::CounterRng random(seed);
    const std::uint64_t pce_sampling_seed = seed ^ 0x805005ULL;
    std::vector<Layout> layouts(static_cast<std::size_t>(layouts_count));
    for (int sample = 0; sample < layouts_count; ++sample) {
        layouts[static_cast<std::size_t>(sample)] = lhs_layout(
            impl_->spec, random, sample, layouts_count
        );
    }
    std::vector<Evaluation> evaluations(static_cast<std::size_t>(layouts_count));
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const auto started = Clock::now();
    executor.parallel_for(0, layouts_count, [&](const int sample) {
        evaluations[static_cast<std::size_t>(sample)] = evaluate_problem(
            impl_->spec, layouts[static_cast<std::size_t>(sample)],
            pce_sampling_seed,
            impl_->spec.high_fidelity_proxy ? 0 : 4
        ).value;
    });
    BatchReceipt receipt;
    receipt.case_id = impl_->spec.case_id;
    receipt.layouts = layouts_count;
    receipt.requested_workers = workers;
    receipt.observed_workers = executor.work_receipt().distinct_participants;
    receipt.seconds = elapsed_seconds(started);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Evaluation& evaluation : evaluations) {
        receipt.aep_checksum_gwh += evaluation.aep_gwh;
        receipt.physical_wake_simulations +=
            evaluation.physical_wake_simulations;
        hash = mix_hash(
            hash, std::bit_cast<std::uint64_t>(evaluation.aep_gwh)
        );
    }
    receipt.scientific_hash = hash;
    return receipt;
}

std::vector<std::string> paper_case_ids() {
    return {
        "l0805_case_i", "l0805_case_ii",
        "l0805_case_iii", "l0805_case_iv",
    };
}

CaseSpec case_spec(const std::string& case_id) { return make_spec(case_id); }

Layout perimeter_layout(const CaseSpec& spec) {
    std::vector<int> boundary;
    for (int column = 0; column < spec.grid_width; column += 2) {
        boundary.push_back(column);
    }
    for (int row = 2; row < spec.grid_width; row += 2) {
        boundary.push_back(row * spec.grid_width + spec.grid_width - 1);
    }
    for (int column = spec.grid_width - 3; column >= 0; column -= 2) {
        boundary.push_back(
            (spec.grid_width - 1) * spec.grid_width + column
        );
    }
    for (int row = spec.grid_width - 3; row >= 2; row -= 2) {
        boundary.push_back(row * spec.grid_width);
    }
    if (static_cast<int>(boundary.size()) < spec.turbines) {
        throw std::runtime_error("L0805 boundary lattice too small");
    }
    std::vector<int> selected;
    selected.reserve(static_cast<std::size_t>(spec.turbines));
    for (int turbine = 0; turbine < spec.turbines; ++turbine) {
        const std::size_t index = static_cast<std::size_t>(
            static_cast<long long>(turbine)
            * static_cast<long long>(boundary.size()) / spec.turbines
        );
        selected.push_back(boundary[index]);
    }
    if (!feasible_layout(spec, canonical_layout(spec, selected))) {
        throw std::runtime_error("L0805 perimeter layout construction failed");
    }
    return canonical_layout(spec, std::move(selected));
}

double expected_improvement(
    const double prediction_mean,
    const double prediction_standard_deviation,
    const double best_observed
) {
    if (!(prediction_standard_deviation > 0.0)) {
        return std::max(0.0, prediction_mean - best_observed);
    }
    const double improvement = prediction_mean - best_observed;
    const double z = improvement / prediction_standard_deviation;
    const double cdf = 0.5 * std::erfc(-z / std::sqrt(2.0));
    const double density = std::exp(-0.5 * z * z)
        / std::sqrt(2.0 * std::numbers::pi);
    return std::max(
        0.0, improvement * cdf + prediction_standard_deviation * density
    );
}

}  // namespace core99::l0805
