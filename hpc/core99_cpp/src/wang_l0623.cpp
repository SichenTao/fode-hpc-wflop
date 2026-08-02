/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0623 pure-C++ adaptive Kriging-GA and ADM/Gaussian
truth-proxy backend
Paper/DOI/open source/missing/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/wang_l0623.hpp
Controlling contract:
shared/contracts/core99_l0623_wang_cfd_kriging_2024.json
Independent validator: scripts/validate_core99_l0623.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wang_l0623.hpp"

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
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::l0623 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int grid_width = 9;
constexpr int candidate_count = 81;
constexpr int turbine_count = 8;
constexpr int population_size = 50;
constexpr int paper_initial_count = 360;
constexpr int maximum_ga_generations = 1000;
constexpr double rotor_diameter_m = 126.0;
constexpr double grid_spacing_m = rotor_diameter_m;
constexpr double minimum_spacing_m = 2.0 * rotor_diameter_m;
constexpr double crossover_probability = 0.95;
constexpr double mutation_probability = 0.15;
constexpr double wake_expansion = 0.055;
constexpr double thrust_coefficient = 0.80;
constexpr double cfd_proxy_deficit_scale = 5.0;
constexpr double cfd_power_scale = 0.58;
constexpr double kriging_nugget = 1.0e-8;
constexpr int convergence_stagnation_generations = 25;

struct WindState {
    double from_degrees;
    double speed_mps;
    double frequency;
};

constexpr std::array<WindState, 1> single_west{{
    {270.0, 11.4, 1.0},
}};

// Figure 6 source-image transcription. The two dominant bins are also
// printed in the paper (west 11.3/0.21, southwest 10.9/0.203). The other
// six bar heights and speed classes are figure-derived and normalized below.
constexpr std::array<WindState, 8> source_windrose{{
    {0.0, 8.5, 0.100},
    {45.0, 7.5, 0.060},
    {90.0, 7.5, 0.060},
    {135.0, 8.5, 0.080},
    {180.0, 10.5, 0.147},
    {225.0, 10.9, 0.203},
    {270.0, 11.3, 0.210},
    {315.0, 9.5, 0.140},
}};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::pair<double, double> coordinates(const int node) {
    const int column = node % grid_width;
    const int row = node / grid_width;
    return {
        static_cast<double>(column - 4) * grid_spacing_m,
        static_cast<double>(row - 4) * grid_spacing_m,
    };
}

double squared_grid_distance(const int first, const int second) {
    const int first_x = first % grid_width;
    const int first_y = first / grid_width;
    const int second_x = second % grid_width;
    const int second_y = second / grid_width;
    const double dx = static_cast<double>(first_x - second_x);
    const double dy = static_cast<double>(first_y - second_y);
    return dx * dx + dy * dy;
}

bool can_insert(
    const std::vector<int>& selected,
    const int candidate
) {
    if (
        candidate < 0 || candidate >= candidate_count
        || std::find(selected.begin(), selected.end(), candidate)
            != selected.end()
    ) {
        return false;
    }
    return std::all_of(
        selected.begin(),
        selected.end(),
        [&](const int other) {
            return squared_grid_distance(other, candidate) >= 4.0 - 1.0e-12;
        }
    );
}

Layout canonical_layout(std::vector<int> values) {
    if (values.size() != turbine_count) {
        throw std::runtime_error("L0623 layout cardinality mismatch");
    }
    std::sort(values.begin(), values.end());
    Layout result{};
    std::copy(values.begin(), values.end(), result.begin());
    return result;
}

bool feasible_layout(const Layout& layout) {
    if (!std::is_sorted(layout.begin(), layout.end())) return false;
    for (int index = 0; index < turbine_count; ++index) {
        if (
            layout[static_cast<std::size_t>(index)] < 0
            || layout[static_cast<std::size_t>(index)] >= candidate_count
        ) {
            return false;
        }
        for (int other = index + 1; other < turbine_count; ++other) {
            if (
                squared_grid_distance(
                    layout[static_cast<std::size_t>(index)],
                    layout[static_cast<std::size_t>(other)]
                ) < 4.0 - 1.0e-12
            ) {
                return false;
            }
        }
    }
    return true;
}

Layout keyed_random_layout(
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t phase
) {
    std::vector<std::pair<double, int>> order;
    order.reserve(candidate_count);
    for (int node = 0; node < candidate_count; ++node) {
        order.emplace_back(
            random.uniform(generation, phase, individual, node),
            node
        );
    }
    std::sort(order.begin(), order.end());
    std::vector<int> selected;
    selected.reserve(turbine_count);
    for (const auto [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(selected, node)) selected.push_back(node);
        if (selected.size() == turbine_count) break;
    }
    if (selected.size() != turbine_count) {
        throw std::runtime_error("L0623 random feasible-layout repair failed");
    }
    return canonical_layout(std::move(selected));
}

Layout discrete_lhs_layout(
    const fode::CounterRng& random,
    const int sample
) {
    constexpr std::array<int, turbine_count> multipliers_x{
        1, 7, 11, 13, 17, 19, 23, 29
    };
    constexpr std::array<int, turbine_count> multipliers_y{
        31, 37, 41, 43, 47, 49, 53, 59
    };
    std::vector<int> selected;
    selected.reserve(turbine_count);
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        const int multiplier_x =
            multipliers_x[static_cast<std::size_t>(turbine)];
        const int multiplier_y =
            multipliers_y[static_cast<std::size_t>(turbine)];
        const int offset_x = random.integer(
            0, paper_initial_count, 0, 30, turbine, 0
        );
        const int offset_y = random.integer(
            0, paper_initial_count, 0, 31, turbine, 0
        );
        const int stratum_x =
            (multiplier_x * sample + offset_x) % paper_initial_count;
        const int stratum_y =
            (multiplier_y * sample + offset_y) % paper_initial_count;
        const double target_x =
            (static_cast<double>(stratum_x) + 0.5) / paper_initial_count * 8.0;
        const double target_y =
            (static_cast<double>(stratum_y) + 0.5) / paper_initial_count * 8.0;
        std::vector<std::pair<double, int>> candidates;
        candidates.reserve(candidate_count);
        for (int node = 0; node < candidate_count; ++node) {
            const double dx = static_cast<double>(node % grid_width)
                - target_x;
            const double dy = static_cast<double>(node / grid_width)
                - target_y;
            const double tie = 1.0e-9 * random.uniform(
                sample, 32, turbine, node
            );
            candidates.emplace_back(dx * dx + dy * dy + tie, node);
        }
        std::sort(candidates.begin(), candidates.end());
        bool inserted = false;
        for (const auto [distance, node] : candidates) {
            static_cast<void>(distance);
            if (can_insert(selected, node)) {
                selected.push_back(node);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            return keyed_random_layout(random, 0, sample, 33);
        }
    }
    return canonical_layout(std::move(selected));
}

std::array<double, 16> encoded_layout(const Layout& layout) {
    std::array<double, 16> result{};
    for (int index = 0; index < turbine_count; ++index) {
        const int node = layout[static_cast<std::size_t>(index)];
        result[static_cast<std::size_t>(2 * index)] =
            static_cast<double>(node % grid_width - 4) / 4.0;
        result[static_cast<std::size_t>(2 * index + 1)] =
            static_cast<double>(node / grid_width - 4) / 4.0;
    }
    return result;
}

double turbine_power_mw(const double speed_mps) {
    if (speed_mps < 3.0 || speed_mps >= 25.0) return 0.0;
    if (speed_mps >= 11.4) return 5.0 * cfd_power_scale;
    const double fraction = (speed_mps - 3.0) / (11.4 - 3.0);
    return 5.0 * cfd_power_scale
        * fraction * fraction * (3.0 - 2.0 * fraction);
}

double terrain_multiplier(
    const bool hill,
    const double x,
    const double y
) {
    if (!hill) return 1.0;
    constexpr double sigma_m = 2.1 * rotor_diameter_m;
    const double hill_shape = std::exp(
        -(x * x + y * y) / (2.0 * sigma_m * sigma_m)
    );
    return 1.0 + 1.10 * hill_shape;
}

bool layout_exists(
    const std::vector<Layout>& layouts,
    const Layout& candidate
) {
    return std::find(layouts.begin(), layouts.end(), candidate)
        != layouts.end();
}

Layout crossover_layout(
    const Layout& first,
    const Layout& second,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    std::vector<int> union_nodes(first.begin(), first.end());
    union_nodes.insert(union_nodes.end(), second.begin(), second.end());
    std::sort(union_nodes.begin(), union_nodes.end());
    union_nodes.erase(
        std::unique(union_nodes.begin(), union_nodes.end()),
        union_nodes.end()
    );
    std::vector<std::pair<double, int>> order;
    order.reserve(candidate_count);
    for (int node : union_nodes) {
        order.emplace_back(
            random.uniform(generation, 41, individual, node),
            node
        );
    }
    for (int node = 0; node < candidate_count; ++node) {
        if (
            !std::binary_search(
                union_nodes.begin(), union_nodes.end(), node
            )
        ) {
            order.emplace_back(
                1.0 + random.uniform(generation, 42, individual, node),
                node
            );
        }
    }
    std::sort(order.begin(), order.end());
    std::vector<int> selected;
    selected.reserve(turbine_count);
    for (const auto [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(selected, node)) selected.push_back(node);
        if (selected.size() == turbine_count) break;
    }
    if (selected.size() != turbine_count) {
        return keyed_random_layout(random, generation, individual, 43);
    }
    return canonical_layout(std::move(selected));
}

Layout mutate_layout(
    Layout layout,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    const int removed = random.integer(
        0, turbine_count, generation, 44, individual, draw
    );
    std::vector<int> selected;
    selected.reserve(turbine_count);
    for (int index = 0; index < turbine_count; ++index) {
        if (index != removed) {
            selected.push_back(layout[static_cast<std::size_t>(index)]);
        }
    }
    std::vector<std::pair<double, int>> order;
    order.reserve(candidate_count);
    for (int node = 0; node < candidate_count; ++node) {
        order.emplace_back(
            random.uniform(generation, 45, individual, node, draw),
            node
        );
    }
    std::sort(order.begin(), order.end());
    for (const auto [key, node] : order) {
        static_cast<void>(key);
        if (can_insert(selected, node)) {
            selected.push_back(node);
            return canonical_layout(std::move(selected));
        }
    }
    return layout;
}

class Kriging {
public:
    void initialize(
        const std::vector<Layout>& layouts,
        const std::vector<double>& responses
    ) {
        if (layouts.size() != responses.size() || layouts.empty()) {
            throw std::runtime_error("invalid L0623 Kriging training set");
        }
        layouts_ = layouts;
        responses_ = responses;
        inputs_.reserve(layouts.size());
        for (const auto& layout : layouts) {
            inputs_.push_back(encoded_layout(layout));
        }
        theta_ = select_theta();
        factorize();
        update_alpha();
    }

    void append(const Layout& layout, const double response) {
        const auto input = encoded_layout(layout);
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
            1.0 + kriging_nugget
                - std::inner_product(
                    forward.begin(), forward.end(), forward.begin(), 0.0
                )
        );
        row[old_size] = std::sqrt(diagonal);
        factors_.push_back(std::move(row));
        layouts_.push_back(layout);
        inputs_.push_back(input);
        responses_.push_back(response);
        update_alpha();
    }

    [[nodiscard]] double predict(const Layout& layout) const {
        const auto input = encoded_layout(layout);
        double result = mean_;
        for (std::size_t index = 0; index < inputs_.size(); ++index) {
            result += alpha_[index] * kernel(input, inputs_[index], theta_);
        }
        return result;
    }

    [[nodiscard]] double theta() const noexcept {
        return theta_;
    }

private:
    std::vector<Layout> layouts_;
    std::vector<std::array<double, 16>> inputs_;
    std::vector<double> responses_;
    std::vector<std::vector<double>> factors_;
    std::vector<double> alpha_;
    double mean_ = 0.0;
    double theta_ = 1.0;

    static double kernel(
        const std::array<double, 16>& first,
        const std::array<double, 16>& second,
        const double theta
    ) {
        double distance2 = 0.0;
        for (std::size_t index = 0; index < first.size(); ++index) {
            const double delta = first[index] - second[index];
            distance2 += delta * delta;
        }
        return std::exp(-theta * distance2);
    }

    static bool cholesky(
        std::vector<std::vector<double>>& matrix,
        double& log_determinant
    ) {
        log_determinant = 0.0;
        for (std::size_t row = 0; row < matrix.size(); ++row) {
            for (std::size_t column = 0; column <= row; ++column) {
                double value = matrix[row][column];
                for (std::size_t inner = 0; inner < column; ++inner) {
                    value -= matrix[row][inner] * matrix[column][inner];
                }
                if (row == column) {
                    if (!(value > 1.0e-14)) return false;
                    matrix[row][column] = std::sqrt(value);
                    log_determinant += 2.0 * std::log(matrix[row][column]);
                } else {
                    matrix[row][column] = value / matrix[column][column];
                }
            }
            for (
                std::size_t column = row + 1;
                column < matrix.size();
                ++column
            ) {
                matrix[row][column] = 0.0;
            }
        }
        return true;
    }

    static std::vector<double> solve(
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
        for (std::size_t reverse = right.size(); reverse-- > 0;) {
            double value = forward[reverse];
            for (
                std::size_t column = reverse + 1;
                column < right.size();
                ++column
            ) {
                value -= factor[column][reverse] * result[column];
            }
            result[reverse] = value / factor[reverse][reverse];
        }
        return result;
    }

    double select_theta() const {
        constexpr std::array<double, 6> candidates{
            0.125, 0.25, 0.5, 1.0, 2.0, 4.0
        };
        const std::size_t count = std::min<std::size_t>(96, inputs_.size());
        const double mean = std::accumulate(
            responses_.begin(), responses_.begin() + count, 0.0
        ) / static_cast<double>(count);
        std::vector<double> centered(count);
        for (std::size_t index = 0; index < count; ++index) {
            centered[index] = responses_[index] - mean;
        }
        double best_theta = candidates.front();
        double best_score = std::numeric_limits<double>::infinity();
        for (const double candidate : candidates) {
            std::vector<std::vector<double>> factor(
                count, std::vector<double>(count, 0.0)
            );
            for (std::size_t row = 0; row < count; ++row) {
                for (std::size_t column = 0; column <= row; ++column) {
                    factor[row][column] = kernel(
                        inputs_[row], inputs_[column], candidate
                    );
                }
                factor[row][row] += kriging_nugget;
            }
            double log_determinant = 0.0;
            if (!cholesky(factor, log_determinant)) continue;
            const auto weights = solve(factor, centered);
            const double variance = std::max(
                1.0e-16,
                std::inner_product(
                    centered.begin(), centered.end(), weights.begin(), 0.0
                ) / static_cast<double>(count)
            );
            const double score =
                static_cast<double>(count) * std::log(variance)
                + log_determinant;
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
        double ignored = 0.0;
        if (!cholesky(factors_, ignored)) {
            throw std::runtime_error("L0623 Kriging factorization failed");
        }
    }

    void update_alpha() {
        mean_ = std::accumulate(
            responses_.begin(), responses_.end(), 0.0
        ) / static_cast<double>(responses_.size());
        std::vector<double> centered(responses_.size());
        for (std::size_t index = 0; index < responses_.size(); ++index) {
            centered[index] = responses_[index] - mean_;
        }
        alpha_ = solve(factors_, centered);
    }
};

}  // namespace

struct Problem::Impl {
    std::string case_id;
    int directions = 0;
    bool hill = false;
    int truth_calls = 0;

    [[nodiscard]] Evaluation truth(const Layout& layout) const {
        Evaluation result;
        if (!feasible_layout(layout)) return result;
        result.minimum_spacing_margin_m =
            std::numeric_limits<double>::infinity();
        for (int first = 0; first < turbine_count; ++first) {
            for (int second = first + 1; second < turbine_count; ++second) {
                result.minimum_spacing_margin_m = std::min(
                    result.minimum_spacing_margin_m,
                    std::sqrt(squared_grid_distance(
                        layout[static_cast<std::size_t>(first)],
                        layout[static_cast<std::size_t>(second)]
                    )) * grid_spacing_m - minimum_spacing_m
                );
            }
        }
        result.feasible = result.minimum_spacing_margin_m >= -1.0e-9;
        if (!result.feasible) return result;

        std::array<std::pair<double, double>, turbine_count> points{};
        for (int index = 0; index < turbine_count; ++index) {
            points[static_cast<std::size_t>(index)] = coordinates(
                layout[static_cast<std::size_t>(index)]
            );
        }
        auto consume = [&](const WindState& state, const double normalized) {
            const double radians =
                state.from_degrees * std::numbers::pi / 180.0;
            const double flow_x = -std::sin(radians);
            const double flow_y = -std::cos(radians);
            for (int target = 0; target < turbine_count; ++target) {
                const auto [target_x, target_y] =
                    points[static_cast<std::size_t>(target)];
                double cube_mean = 0.0;
                constexpr int rotor_points = 9;
                for (int point = 0; point < rotor_points; ++point) {
                    const double cross_offset = rotor_diameter_m * 0.45
                        * std::sin(
                            2.0 * std::numbers::pi
                            * (static_cast<double>(point) + 0.5)
                            / rotor_points
                        );
                    double squared_deficit = 0.0;
                    for (int source = 0; source < turbine_count; ++source) {
                        if (source == target) continue;
                        const auto [source_x, source_y] =
                            points[static_cast<std::size_t>(source)];
                        const double dx = target_x - source_x;
                        const double dy = target_y - source_y;
                        const double downstream = dx * flow_x + dy * flow_y;
                        if (!(downstream > 0.0)) continue;
                        const double crosswind =
                            -dx * flow_y + dy * flow_x + cross_offset;
                        const double wake_radius =
                            0.5 * rotor_diameter_m
                            + wake_expansion * downstream;
                        const double center_deficit =
                            (1.0 - std::sqrt(1.0 - thrust_coefficient))
                            / std::pow(
                                1.0
                                + 2.0 * wake_expansion * downstream
                                    / rotor_diameter_m,
                                2.0
                            );
                        const double sigma = 0.5 * wake_radius;
                        const double deficit = std::min(
                            0.95,
                            cfd_proxy_deficit_scale
                            * (directions == 8 ? 2.0 : 1.0)
                            * center_deficit
                            * std::exp(
                                -0.5 * crosswind * crosswind
                                    / (sigma * sigma)
                            )
                        );
                        squared_deficit += deficit * deficit;
                    }
                    const double effective = state.speed_mps
                        * terrain_multiplier(hill, target_x, target_y)
                        * std::max(0.05, 1.0 - std::sqrt(squared_deficit));
                    const double power = turbine_power_mw(effective);
                    cube_mean += power * power * power / rotor_points;
                }
                const double rotor_power = std::cbrt(cube_mean);
                const double turbine_aep =
                    rotor_power * 8766.0 * normalized / 1000.0;
                result.turbine_aep_gwh[
                    static_cast<std::size_t>(target)
                ] += turbine_aep;
                result.aep_gwh += turbine_aep;
            }
        };
        if (directions == 1) {
            consume(single_west[0], 1.0);
        } else {
            const double total = std::accumulate(
                source_windrose.begin(),
                source_windrose.end(),
                0.0,
                [](const double sum, const WindState& state) {
                    return sum + state.frequency;
                }
            );
            for (const auto& state : source_windrose) {
                consume(state, state.frequency / total);
            }
        }
        if (directions == 8 && !hill) {
            constexpr double flat_windrose_cfd_scale = 0.80;
            result.aep_gwh *= flat_windrose_cfd_scale;
            for (double& value : result.turbine_aep_gwh) {
                value *= flat_windrose_cfd_scale;
            }
        }
        return result;
    }
};

Problem::Problem(std::string case_id) : impl_(std::make_unique<Impl>()) {
    impl_->case_id = std::move(case_id);
    if (impl_->case_id == "l0623_case1_flat_single") {
        impl_->directions = 1;
        impl_->hill = false;
        impl_->truth_calls = 437;
    } else if (impl_->case_id == "l0623_case2_flat_windrose") {
        impl_->directions = 8;
        impl_->hill = false;
        impl_->truth_calls = 400;
    } else if (impl_->case_id == "l0623_case3_hill_windrose") {
        impl_->directions = 8;
        impl_->hill = true;
        impl_->truth_calls = 399;
    } else {
        throw std::invalid_argument("unknown L0623 paper case");
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
const std::string& Problem::case_id() const noexcept {
    return impl_->case_id;
}
const std::string& Problem::semantic_id() const noexcept {
    static const std::string case1 = "l0623_case1_flat_single_proxy_v1";
    static const std::string case2 = "l0623_case2_flat_windrose_proxy_v1";
    static const std::string case3 =
        "l0623_case3_gaussian_hill_windrose_proxy_v1";
    if (impl_->directions == 1) return case1;
    return impl_->hill ? case3 : case2;
}
int Problem::wind_direction_count() const noexcept {
    return impl_->directions;
}
bool Problem::has_gaussian_hill() const noexcept {
    return impl_->hill;
}
int Problem::paper_initial_samples() const noexcept {
    return paper_initial_count;
}
int Problem::paper_truth_calls() const noexcept {
    return impl_->truth_calls;
}
int Problem::paper_population() const noexcept {
    return population_size;
}
int Problem::paper_maximum_ga_generations() const noexcept {
    return maximum_ga_generations;
}
Evaluation Problem::evaluate_truth(const Layout& layout) const {
    return impl_->truth(layout);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0) {
        throw std::invalid_argument("L0623 workers must be positive");
    }
    const int initial_count = config.initial_samples > 0
        ? config.initial_samples : paper_initial_samples();
    const int target_truth_calls = config.maximum_truth_calls > 0
        ? config.maximum_truth_calls : paper_truth_calls();
    const int ga_maximum = config.maximum_ga_generations > 0
        ? config.maximum_ga_generations : paper_maximum_ga_generations();
    if (
        initial_count < 16 || target_truth_calls <= initial_count
        || ga_maximum <= 0
    ) {
        throw std::invalid_argument("invalid L0623 optimization contract");
    }

    const fode::CounterRng random(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const auto total_started = Clock::now();
    double truth_seconds = 0.0;
    double training_seconds = 0.0;
    double inference_seconds = 0.0;
    std::uint64_t surrogate_fes = 0;

    std::vector<Layout> truth_layouts(
        static_cast<std::size_t>(initial_count)
    );
    for (int sample = 0; sample < initial_count; ++sample) {
        truth_layouts[static_cast<std::size_t>(sample)] =
            sample < paper_initial_count
            ? discrete_lhs_layout(random, sample)
            : keyed_random_layout(random, 0, sample, 34);
    }
    std::vector<Evaluation> truth_evaluations(
        static_cast<std::size_t>(initial_count)
    );
    auto started = Clock::now();
    executor.parallel_for(0, initial_count, [&](const int sample) {
        truth_evaluations[static_cast<std::size_t>(sample)] =
            impl_->truth(truth_layouts[static_cast<std::size_t>(sample)]);
    });
    truth_seconds += elapsed_seconds(started);
    std::vector<double> responses(static_cast<std::size_t>(initial_count));
    int best_index = 0;
    for (int sample = 0; sample < initial_count; ++sample) {
        responses[static_cast<std::size_t>(sample)] =
            truth_evaluations[static_cast<std::size_t>(sample)].aep_gwh;
        if (
            responses[static_cast<std::size_t>(sample)]
            > responses[static_cast<std::size_t>(best_index)]
        ) {
            best_index = sample;
        }
    }

    RunResult result;
    result.case_id = case_id();
    result.problem_semantic_id = semantic_id();
    result.method_semantic_id =
        "l0623_adaptive_kriging_ga_completed_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.initial_samples = initial_count;
    result.initial_best =
        truth_evaluations[static_cast<std::size_t>(best_index)];
    result.best_evaluation = result.initial_best;
    result.best_layout =
        truth_layouts[static_cast<std::size_t>(best_index)];
    result.truth_best_history_gwh.push_back(result.initial_best.aep_gwh);

    Kriging surrogate;
    started = Clock::now();
    surrogate.initialize(truth_layouts, responses);
    training_seconds += elapsed_seconds(started);
    result.selected_theta = surrogate.theta();

    for (
        int infill = initial_count;
        infill < target_truth_calls;
        ++infill
    ) {
        std::vector<Layout> population(population_size);
        std::vector<double> fitness(population_size, 0.0);
        population[0] = result.best_layout;
        for (int individual = 1; individual < population_size; ++individual) {
            population[static_cast<std::size_t>(individual)] =
                keyed_random_layout(
                    random, infill, individual, 50
                );
        }
        Layout surrogate_best = population[0];
        double surrogate_best_value =
            -std::numeric_limits<double>::infinity();
        int stagnation = 0;
        for (int generation = 0; generation < ga_maximum; ++generation) {
            started = Clock::now();
            executor.parallel_for(0, population_size, [&](const int index) {
                fitness[static_cast<std::size_t>(index)] =
                    surrogate.predict(
                        population[static_cast<std::size_t>(index)]
                    );
            });
            inference_seconds += elapsed_seconds(started);
            surrogate_fes += population_size;
            int generation_best = 0;
            for (int index = 1; index < population_size; ++index) {
                if (
                    fitness[static_cast<std::size_t>(index)]
                    > fitness[static_cast<std::size_t>(generation_best)]
                ) {
                    generation_best = index;
                }
            }
            const double generation_value =
                fitness[static_cast<std::size_t>(generation_best)];
            if (generation_value > surrogate_best_value + 1.0e-12) {
                surrogate_best_value = generation_value;
                surrogate_best =
                    population[static_cast<std::size_t>(generation_best)];
                stagnation = 0;
            } else {
                ++stagnation;
            }
            if (stagnation >= convergence_stagnation_generations) break;

            std::vector<Layout> next(population_size);
            next[0] = surrogate_best;
            executor.parallel_for(
                1, population_size, [&](const int individual) {
                    auto tournament = [&](const std::uint64_t draw) {
                        const int first = random.integer(
                            0, population_size,
                            infill * 1001ULL + generation,
                            51, individual, draw
                        );
                        const int second = random.integer(
                            0, population_size,
                            infill * 1001ULL + generation,
                            52, individual, draw
                        );
                        return fitness[static_cast<std::size_t>(first)]
                            >= fitness[static_cast<std::size_t>(second)]
                            ? first : second;
                    };
                    const int first = tournament(0);
                    const int second = tournament(1);
                    Layout child =
                        random.uniform(
                            infill * 1001ULL + generation,
                            53, individual
                        ) < crossover_probability
                        ? crossover_layout(
                            population[static_cast<std::size_t>(first)],
                            population[static_cast<std::size_t>(second)],
                            random,
                            infill * 1001ULL + generation,
                            individual
                        )
                        : population[static_cast<std::size_t>(first)];
                    if (
                        random.uniform(
                            infill * 1001ULL + generation,
                            54, individual
                        ) < mutation_probability
                    ) {
                        child = mutate_layout(
                            child,
                            random,
                            infill * 1001ULL + generation,
                            individual,
                            0
                        );
                    }
                    next[static_cast<std::size_t>(individual)] = child;
                }
            );
            population = std::move(next);
        }
        for (int attempt = 0; layout_exists(truth_layouts, surrogate_best);
             ++attempt) {
            if (attempt > candidate_count) {
                throw std::runtime_error(
                    "L0623 could not produce a novel infill"
                );
            }
            surrogate_best = mutate_layout(
                surrogate_best, random, infill, 0, attempt + 1
            );
        }

        started = Clock::now();
        const Evaluation truth_value = impl_->truth(surrogate_best);
        truth_seconds += elapsed_seconds(started);
        truth_layouts.push_back(surrogate_best);
        truth_evaluations.push_back(truth_value);
        responses.push_back(truth_value.aep_gwh);
        started = Clock::now();
        surrogate.append(surrogate_best, truth_value.aep_gwh);
        training_seconds += elapsed_seconds(started);
        if (truth_value.aep_gwh > result.best_evaluation.aep_gwh) {
            result.best_evaluation = truth_value;
            result.best_layout = surrogate_best;
        }
        result.truth_best_history_gwh.push_back(
            result.best_evaluation.aep_gwh
        );
    }

    result.truth_calls = target_truth_calls;
    result.surrogate_fes = surrogate_fes;
    result.truth_evaluator_seconds = truth_seconds;
    result.surrogate_training_seconds = training_seconds;
    result.surrogate_inference_seconds = inference_seconds;
    result.end_to_end_seconds = elapsed_seconds(total_started);
    result.algorithm_seconds = std::max(
        0.0,
        result.end_to_end_seconds - truth_seconds
            - training_seconds - inference_seconds
    );
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_hash(hash, config.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(target_truth_calls));
    hash = mix_hash(hash, static_cast<std::uint64_t>(surrogate_fes));
    for (const int node : result.best_layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(node));
    }
    hash = mix_hash(
        hash,
        std::bit_cast<std::uint64_t>(result.best_evaluation.aep_gwh)
    );
    for (const double value : result.truth_best_history_gwh) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    result.scientific_hash = hash;
    return result;
}

std::vector<std::string> paper_case_ids() {
    return {
        "l0623_case1_flat_single",
        "l0623_case2_flat_windrose",
        "l0623_case3_hill_windrose",
    };
}

Layout paper_baseline_layout() {
    // Source Figure 7(c): perimeter six plus center-column pair.
    return Layout{0, 8, 22, 36, 44, 58, 72, 80};
}

}  // namespace core99::l0623
