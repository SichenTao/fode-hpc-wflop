/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0371 pure-C++ IET-GWM, paper grids, DEEM and HPC cache
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/guo_l0371.hpp
Controlling contract:
shared/contracts/core99_l0371_guo_stability_deem_2021.json
Independent validator: scripts/validate_core99_l0371.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/guo_l0371.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::l0371 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double gamma_turbulence = 0.38;
constexpr double earth_rotation_rad_s = 72.9e-6;
constexpr double von_karman = 0.4;
constexpr double differential_weight = 0.9;
constexpr double crossover_rate = 0.9;
constexpr double comparison_tolerance = 1.0e-12;

constexpr std::array<const char*, 7> stability_names{
    "vu", "u", "nu", "n", "ns", "s", "vs"
};

template <class T>
T read_binary(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!stream) throw std::runtime_error("truncated L0371 proxy fixture");
    return value;
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

int stability_from_name(const std::string& name) {
    for (int index = 0; index < 7; ++index) {
        if (name == stability_names[static_cast<std::size_t>(index)]) {
            return index;
        }
    }
    throw std::invalid_argument("unknown L0371 stability label: " + name);
}

double linear_interpolate(
    const std::vector<std::array<double, 3>>& curve,
    const double speed,
    const int value_index
) {
    if (speed <= curve.front()[0]) return curve.front()[value_index];
    for (std::size_t index = 1; index < curve.size(); ++index) {
        if (speed <= curve[index][0]) {
            const double width = curve[index][0] - curve[index - 1][0];
            const double fraction = width > 0.0
                ? (speed - curve[index - 1][0]) / width : 0.0;
            return curve[index - 1][value_index]
                + fraction * (
                    curve[index][value_index]
                    - curve[index - 1][value_index]
                );
        }
    }
    return curve.back()[value_index];
}

double weibull_cdf(
    const double speed,
    const double scale,
    const double shape
) {
    if (speed <= 0.0) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

int nearest_cardinal_group(const double direction_degrees) {
    const int rounded = static_cast<int>(
        std::floor((direction_degrees + 45.0) / 90.0)
    ) % 4;
    return rounded < 0 ? rounded + 4 : rounded;
}

double stability_function(const double hub_height, const double length) {
    if (std::abs(length) > 1.0e20) return 0.0;
    const double parameter = hub_height / length;
    if (parameter > 0.0) return -4.7 * parameter;
    if (parameter == 0.0) return 0.0;
    const double t = std::pow(1.0 - 15.0 * parameter, 0.25);
    return 2.0 * std::log((1.0 + t) / 2.0)
        + std::log((1.0 + t * t) / 2.0)
        - std::atan(t) + std::numbers::pi / 2.0;
}

double wake_growth(
    const double hub_height,
    const double roughness,
    const double length,
    const double latitude_degrees,
    const double ambient_speed
) {
    const double phi = stability_function(hub_height, length);
    const double friction_ratio = von_karman
        / (std::log(hub_height / roughness) - phi);
    const double streamwise_intensity =
        gamma_turbulence * 2.5 * friction_ratio;
    const double friction_velocity = friction_ratio * ambient_speed;
    const double latitude = latitude_degrees
        * std::numbers::pi / 180.0;
    const double boundary_height = friction_velocity
        / (12.0 * earth_rotation_rad_s * std::sin(latitude));
    // Target Eq. (8) explicitly prints z0 in this cosine. This deliberately
    // preserves the target-paper equation even though Cheng Eq. (18) uses z.
    const double cosine = std::cos(
        std::numbers::pi * roughness / (2.0 * boundary_height)
    );
    const double lateral_intensity = streamwise_intensity
        * (1.0 - 0.22 * std::pow(cosine, 4.0));
    return 0.223 * lateral_intensity + 0.022;
}

bool power_better(const double candidate, const double incumbent) {
    return candidate > incumbent + comparison_tolerance;
}

}  // namespace

struct Problem::Impl {
    std::string case_id;
    std::string semantic_id;
    bool horns = false;
    int ideal_wind_case = -1;
    int fixed_stability = 3;
    bool actual_stability = false;
    int turbines = 0;
    int workers = 20;
    int observed_precompute_workers = 0;
    double rotor_diameter_m = 40.0;
    double hub_height_m = 60.0;
    double latitude_degrees = 47.0;
    double spacing_m = 200.0;
    double precomputation_seconds = 0.0;
    std::vector<Point> candidates;
    std::vector<WindState> states;
    std::vector<std::array<double, 3>> turbine_curve;
    std::array<std::array<double, 2>, 7> ideal_stability{};
    std::array<std::array<double, 2>, 7> horns_stability{};
    std::array<std::array<double, 7>, 4> stability_probabilities{};
    std::array<std::array<double, 3>, 12> horns_weibull{};
    std::array<double, 108> case_c_probabilities{};
    std::vector<float> deficit_ratios;

    [[nodiscard]] std::size_t deficit_index(
        const int state,
        const int source,
        const int target
    ) const {
        const std::size_t count = candidates.size();
        return (
            static_cast<std::size_t>(state) * count
            + static_cast<std::size_t>(source)
        ) * count + static_cast<std::size_t>(target);
    }

    void load_proxy(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open L0371 proxy");
        std::array<char, 7> magic{};
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!stream || std::string(magic.data(), magic.size()) != "L0371P1") {
            throw std::runtime_error("invalid L0371 proxy magic");
        }
        const auto case_c_count = read_binary<std::uint32_t>(stream);
        const auto curve_count = read_binary<std::uint32_t>(stream);
        const auto ideal_count = read_binary<std::uint32_t>(stream);
        const auto horns_count = read_binary<std::uint32_t>(stream);
        const auto stability_count = read_binary<std::uint32_t>(stream);
        if (
            case_c_count != 108U || ideal_count != 7U
            || horns_count != 7U || stability_count != 28U
        ) {
            throw std::runtime_error("L0371 proxy dimensions mismatch");
        }
        for (double& value : case_c_probabilities) {
            value = read_binary<float>(stream);
        }
        turbine_curve.resize(curve_count);
        for (auto& point : turbine_curve) {
            point[0] = read_binary<float>(stream);
            point[1] = read_binary<float>(stream);
            point[2] = read_binary<float>(stream);
        }
        for (auto& point : ideal_stability) {
            point[0] = read_binary<double>(stream);
            point[1] = read_binary<double>(stream);
        }
        for (auto& point : horns_stability) {
            point[0] = read_binary<double>(stream);
            point[1] = read_binary<double>(stream);
        }
        for (auto& direction : stability_probabilities) {
            for (double& value : direction) {
                value = read_binary<float>(stream);
            }
        }
        for (auto& point : horns_weibull) {
            point[0] = read_binary<float>(stream);
            point[1] = read_binary<float>(stream);
            point[2] = read_binary<float>(stream);
        }
        if (stream.peek() != std::ifstream::traits_type::eof()) {
            throw std::runtime_error("unexpected trailing L0371 proxy data");
        }
    }

    void configure_case() {
        const std::string ideal_prefix = "l0371_ideal_";
        const std::string horns_prefix = "l0371_horns_";
        if (case_id.starts_with(ideal_prefix)) {
            horns = false;
            semantic_id = "l0371_ideal_ietgwm_grid_v1";
            const std::string suffix = case_id.substr(ideal_prefix.size());
            if (suffix.size() < 3U || suffix[1] != '_') {
                throw std::invalid_argument("invalid L0371 ideal case ID");
            }
            ideal_wind_case = suffix[0] - 'a';
            if (ideal_wind_case < 0 || ideal_wind_case > 2) {
                throw std::invalid_argument("invalid L0371 ideal wind case");
            }
            fixed_stability = stability_from_name(suffix.substr(2));
            turbines = ideal_wind_case == 0 ? 30 : 39;
            rotor_diameter_m = 40.0;
            hub_height_m = 60.0;
            latitude_degrees = 47.0;
            spacing_m = 200.0;
            for (int y = 0; y < 10; ++y) {
                for (int x = 0; x < 10; ++x) {
                    candidates.push_back({
                        100.0 + 200.0 * static_cast<double>(x),
                        100.0 + 200.0 * static_cast<double>(y),
                    });
                }
            }
            return;
        }
        if (!case_id.starts_with(horns_prefix)) {
            throw std::invalid_argument("unknown L0371 case ID: " + case_id);
        }
        horns = true;
        semantic_id = "l0371_horns_ietgwm_grid_proxy_v1";
        const std::string suffix = case_id.substr(horns_prefix.size());
        actual_stability = suffix == "actual";
        fixed_stability = actual_stability ? -1 : stability_from_name(suffix);
        turbines = 80;
        rotor_diameter_m = 80.0;
        hub_height_m = 70.0;
        latitude_degrees = 55.5;
        spacing_m = 400.0;
        for (int y = 0; y <= 4000; y += 200) {
            const double left = 1000.0 - 0.25 * static_cast<double>(y);
            const double right = 6000.0 - 0.25 * static_cast<double>(y);
            for (int x = 0; x <= 6000; x += 200) {
                if (
                    static_cast<double>(x) + 1.0e-9 >= left
                    && static_cast<double>(x) <= right + 1.0e-9
                ) {
                    candidates.push_back({
                        static_cast<double>(x),
                        static_cast<double>(y),
                    });
                }
            }
        }
    }

    void build_states() {
        if (!horns) {
            if (ideal_wind_case == 0) {
                states.push_back({90.0, 12.0, 1.0, fixed_stability});
            } else if (ideal_wind_case == 1) {
                for (int direction = 0; direction < 36; ++direction) {
                    states.push_back({
                        10.0 * static_cast<double>(direction),
                        12.0,
                        1.0 / 36.0,
                        fixed_stability,
                    });
                }
            } else {
                constexpr std::array<double, 3> speeds{8.0, 12.0, 17.0};
                for (int direction = 0; direction < 36; ++direction) {
                    for (int speed = 0; speed < 3; ++speed) {
                        states.push_back({
                            10.0 * static_cast<double>(direction),
                            speeds[static_cast<std::size_t>(speed)],
                            case_c_probabilities[
                                static_cast<std::size_t>(3 * direction + speed)
                            ],
                            fixed_stability,
                        });
                    }
                }
            }
            return;
        }
        constexpr std::array<std::array<double, 2>, 6> speed_bins{{
            {3.0, 4.0}, {4.0, 6.0}, {6.0, 8.0},
            {8.0, 10.0}, {10.0, 12.0}, {12.0, 25.0},
        }};
        constexpr std::array<double, 6> representative_speeds{
            3.5, 5.0, 7.0, 9.0, 11.0, 14.5
        };
        for (int direction = 0; direction < 12; ++direction) {
            const auto& parameters =
                horns_weibull[static_cast<std::size_t>(direction)];
            const int group = nearest_cardinal_group(
                30.0 * static_cast<double>(direction)
            );
            for (int speed = 0; speed < 6; ++speed) {
                const auto bounds =
                    speed_bins[static_cast<std::size_t>(speed)];
                const double speed_probability = weibull_cdf(
                    bounds[1], parameters[0], parameters[1]
                ) - weibull_cdf(bounds[0], parameters[0], parameters[1]);
                if (actual_stability) {
                    for (int stability = 0; stability < 7; ++stability) {
                        states.push_back({
                            30.0 * static_cast<double>(direction),
                            representative_speeds[
                                static_cast<std::size_t>(speed)
                            ],
                            parameters[2] * speed_probability
                                * stability_probabilities[
                                    static_cast<std::size_t>(group)
                                ][static_cast<std::size_t>(stability)],
                            stability,
                        });
                    }
                } else {
                    states.push_back({
                        30.0 * static_cast<double>(direction),
                        representative_speeds[
                            static_cast<std::size_t>(speed)
                        ],
                        parameters[2] * speed_probability,
                        fixed_stability,
                    });
                }
            }
        }
    }

    [[nodiscard]] double power_kw(const double speed) const {
        if (!horns) return 0.3 * speed * speed * speed;
        return 1000.0 * linear_interpolate(turbine_curve, speed, 1);
    }

    [[nodiscard]] double thrust(const double speed) const {
        if (!horns) return 0.88;
        return std::clamp(
            linear_interpolate(turbine_curve, speed, 2),
            0.0,
            0.999
        );
    }

    [[nodiscard]] const std::array<double, 2>& stability(
        const int index
    ) const {
        return horns
            ? horns_stability[static_cast<std::size_t>(index)]
            : ideal_stability[static_cast<std::size_t>(index)];
    }

    void precompute() {
        const auto begin = Clock::now();
        const int candidate_count = static_cast<int>(candidates.size());
        const int state_count = static_cast<int>(states.size());
        deficit_ratios.assign(
            static_cast<std::size_t>(state_count)
                * candidates.size() * candidates.size(),
            0.0F
        );
        fode::PersistentExecutor executor(workers);
        executor.reset_work_receipt();
        executor.parallel_for(
            0, state_count * candidate_count, [&](const int item) {
                const int state_index = item / candidate_count;
                const int source_index = item % candidate_count;
                const WindState& state =
                    states[static_cast<std::size_t>(state_index)];
                const Point& source =
                    candidates[static_cast<std::size_t>(source_index)];
                const auto& stability_values =
                    stability(state.stability_index);
                const double growth = wake_growth(
                    hub_height_m,
                    stability_values[1],
                    stability_values[0],
                    latitude_degrees,
                    state.speed_mps
                );
                const double epsilon = -1.91 * growth + 0.34;
                const double ct = thrust(state.speed_mps);
                const double theta = state.from_degrees
                    * std::numbers::pi / 180.0;
                const double flow_x = -std::sin(theta);
                const double flow_y = -std::cos(theta);
                for (int target_index = 0;
                     target_index < candidate_count; ++target_index) {
                    if (source_index == target_index) continue;
                    const Point& target =
                        candidates[static_cast<std::size_t>(target_index)];
                    const double delta_x = target.x_m - source.x_m;
                    const double delta_y = target.y_m - source.y_m;
                    const double downstream =
                        delta_x * flow_x + delta_y * flow_y;
                    if (downstream <= 0.0) continue;
                    const double crosswind = std::abs(
                        -delta_x * flow_y + delta_y * flow_x
                    );
                    const double sigma_over_d =
                        growth * downstream / rotor_diameter_m + epsilon;
                    if (sigma_over_d <= 0.0) continue;
                    const double square = std::max(
                        0.0,
                        1.0 - ct
                            / (8.0 * sigma_over_d * sigma_over_d)
                    );
                    const double amplitude = 1.0 - std::sqrt(square);
                    const double normalized_crosswind =
                        crosswind / rotor_diameter_m;
                    const double deficit = amplitude * std::exp(
                        -0.5 * normalized_crosswind * normalized_crosswind
                            / (sigma_over_d * sigma_over_d)
                    );
                    deficit_ratios[deficit_index(
                        state_index, source_index, target_index
                    )] = static_cast<float>(deficit);
                }
            }
        );
        observed_precompute_workers =
            executor.work_receipt().distinct_participants;
        precomputation_seconds = std::chrono::duration<double>(
            Clock::now() - begin
        ).count();
    }

    [[nodiscard]] bool feasible_layout(
        const std::vector<int>& layout
    ) const {
        if (static_cast<int>(layout.size()) != turbines) return false;
        for (std::size_t first = 0; first < layout.size(); ++first) {
            if (
                layout[first] < 0
                || layout[first] >= static_cast<int>(candidates.size())
            ) {
                return false;
            }
            for (std::size_t second = first + 1;
                 second < layout.size(); ++second) {
                const Point& a =
                    candidates[static_cast<std::size_t>(layout[first])];
                const Point& b =
                    candidates[static_cast<std::size_t>(layout[second])];
                if (
                    std::hypot(a.x_m - b.x_m, a.y_m - b.y_m)
                    + 1.0e-9 < spacing_m
                ) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] double cost() const {
        const double count = static_cast<double>(turbines);
        return count * (
            2.0 / 3.0
            + std::exp(-0.00174 * count * count) / 3.0
        );
    }

    struct Cache {
        std::vector<double> squared_deficit;
        std::vector<double> turbine_power;
        std::vector<double> state_total_power;
        double expected_power = 0.0;
    };

    void build_cache(
        const std::vector<int>& layout,
        Cache& cache,
        fode::PersistentExecutor* executor = nullptr
    ) const {
        const int state_count = static_cast<int>(states.size());
        cache.squared_deficit.assign(
            states.size() * layout.size(), 0.0
        );
        cache.turbine_power.assign(states.size() * layout.size(), 0.0);
        cache.state_total_power.assign(states.size(), 0.0);
        auto task = [&](const int state_index) {
            double state_total = 0.0;
            for (int target = 0; target < turbines; ++target) {
                double squared = 0.0;
                for (int source = 0; source < turbines; ++source) {
                    if (source == target) continue;
                    const double deficit = deficit_ratios[deficit_index(
                        state_index,
                        layout[static_cast<std::size_t>(source)],
                        layout[static_cast<std::size_t>(target)]
                    )];
                    squared += deficit * deficit;
                }
                const double effective = states[
                    static_cast<std::size_t>(state_index)
                ].speed_mps * (
                    1.0 - std::min(1.0, std::sqrt(squared))
                );
                const double power = power_kw(effective);
                const std::size_t index =
                    static_cast<std::size_t>(state_index) * layout.size()
                    + static_cast<std::size_t>(target);
                cache.squared_deficit[index] = squared;
                cache.turbine_power[index] = power;
                state_total += power;
            }
            cache.state_total_power[
                static_cast<std::size_t>(state_index)
            ] = state_total;
        };
        if (executor != nullptr && state_count >= 16) {
            executor->parallel_for(0, state_count, task);
        } else {
            for (int state = 0; state < state_count; ++state) task(state);
        }
        cache.expected_power = 0.0;
        for (int state = 0; state < state_count; ++state) {
            cache.expected_power +=
                states[static_cast<std::size_t>(state)].probability
                * cache.state_total_power[static_cast<std::size_t>(state)];
        }
    }

    [[nodiscard]] double no_wake_power() const {
        double value = 0.0;
        for (const WindState& state : states) {
            value += state.probability
                * static_cast<double>(turbines)
                * power_kw(state.speed_mps);
        }
        return value;
    }

    [[nodiscard]] Evaluation evaluation_from_cache(
        const Cache& cache,
        const bool feasible
    ) const {
        Evaluation result;
        result.feasible = feasible;
        result.average_power_kw = cache.expected_power;
        result.no_wake_power_kw = no_wake_power();
        result.efficiency = result.no_wake_power_kw > 0.0
            ? result.average_power_kw / result.no_wake_power_kw : 0.0;
        result.coe = result.average_power_kw > 0.0
            ? cost() / result.average_power_kw
            : std::numeric_limits<double>::infinity();
        return result;
    }

    [[nodiscard]] int nearest_candidate(
        const double x,
        const double y
    ) const {
        int best = 0;
        double best_squared = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const double dx = candidates[index].x_m - x;
            const double dy = candidates[index].y_m - y;
            const double squared = dx * dx + dy * dy;
            if (squared < best_squared) {
                best_squared = squared;
                best = static_cast<int>(index);
            }
        }
        return best;
    }

    [[nodiscard]] std::vector<int> initialize(
        const fode::CounterRng& rng
    ) const {
        for (std::uint64_t restart = 0; restart < 100000; ++restart) {
            std::vector<int> layout;
            layout.reserve(static_cast<std::size_t>(turbines));
            bool failed = false;
            for (int turbine = 0; turbine < turbines; ++turbine) {
                bool placed = false;
                for (int attempt = 0; attempt <= 200; ++attempt) {
                    const int candidate = rng.integer(
                        0,
                        static_cast<int>(candidates.size()),
                        restart,
                        1,
                        static_cast<std::uint64_t>(turbine),
                        static_cast<std::uint64_t>(attempt)
                    );
                    std::vector<int> trial = layout;
                    trial.push_back(candidate);
                    bool valid = true;
                    const Point& point =
                        candidates[static_cast<std::size_t>(candidate)];
                    for (std::size_t index = 0;
                         index + 1U < trial.size(); ++index) {
                        const Point& other = candidates[
                            static_cast<std::size_t>(trial[index])
                        ];
                        if (
                            std::hypot(
                                point.x_m - other.x_m,
                                point.y_m - other.y_m
                            ) + 1.0e-9 < spacing_m
                        ) {
                            valid = false;
                            break;
                        }
                    }
                    if (valid) {
                        layout.push_back(candidate);
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    failed = true;
                    break;
                }
            }
            if (!failed && feasible_layout(layout)) return layout;
        }
        throw std::runtime_error("L0371 initialization did not converge");
    }

    [[nodiscard]] bool valid_replacement(
        const std::vector<int>& layout,
        const int replaced,
        const int candidate
    ) const {
        const Point& point =
            candidates[static_cast<std::size_t>(candidate)];
        for (int index = 0; index < turbines; ++index) {
            if (index == replaced) continue;
            const Point& other = candidates[
                static_cast<std::size_t>(
                    layout[static_cast<std::size_t>(index)]
                )
            ];
            if (
                std::hypot(
                    point.x_m - other.x_m, point.y_m - other.y_m
                ) + 1.0e-9 < spacing_m
            ) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] double trial_cache(
        const std::vector<int>& layout,
        const int replaced,
        const int candidate,
        const Cache& current,
        Cache& scratch,
        fode::PersistentExecutor& executor
    ) const {
        const int state_count = static_cast<int>(states.size());
        scratch.squared_deficit.resize(current.squared_deficit.size());
        scratch.turbine_power.resize(current.turbine_power.size());
        scratch.state_total_power.resize(states.size());
        const int old_candidate =
            layout[static_cast<std::size_t>(replaced)];
        auto task = [&](const int state_index) {
            double state_total = 0.0;
            const std::size_t base =
                static_cast<std::size_t>(state_index)
                * static_cast<std::size_t>(turbines);
            for (int target = 0; target < turbines; ++target) {
                double squared = 0.0;
                if (target == replaced) {
                    for (int source = 0; source < turbines; ++source) {
                        if (source == replaced) continue;
                        const double deficit = deficit_ratios[deficit_index(
                            state_index,
                            layout[static_cast<std::size_t>(source)],
                            candidate
                        )];
                        squared += deficit * deficit;
                    }
                } else {
                    squared = current.squared_deficit[
                        base + static_cast<std::size_t>(target)
                    ];
                    const int target_candidate =
                        layout[static_cast<std::size_t>(target)];
                    const double old_deficit =
                        deficit_ratios[deficit_index(
                            state_index, old_candidate, target_candidate
                        )];
                    const double new_deficit =
                        deficit_ratios[deficit_index(
                            state_index, candidate, target_candidate
                        )];
                    squared = std::max(
                        0.0,
                        squared - old_deficit * old_deficit
                            + new_deficit * new_deficit
                    );
                }
                const double effective = states[
                    static_cast<std::size_t>(state_index)
                ].speed_mps * (
                    1.0 - std::min(1.0, std::sqrt(squared))
                );
                const double power = power_kw(effective);
                scratch.squared_deficit[
                    base + static_cast<std::size_t>(target)
                ] = squared;
                scratch.turbine_power[
                    base + static_cast<std::size_t>(target)
                ] = power;
                state_total += power;
            }
            scratch.state_total_power[
                static_cast<std::size_t>(state_index)
            ] = state_total;
        };
        // Small ideal cases are faster in one thread. Heavy Horns/actual
        // cases use the persistent team; formal scheduling parallelizes
        // independent light cases/seeds so all Waffle cores remain useful.
        if (
            static_cast<std::uint64_t>(state_count)
                * static_cast<std::uint64_t>(turbines)
            >= 10000U
        ) {
            executor.parallel_for(0, state_count, task);
        } else {
            for (int state = 0; state < state_count; ++state) task(state);
        }
        scratch.expected_power = 0.0;
        for (int state = 0; state < state_count; ++state) {
            scratch.expected_power +=
                states[static_cast<std::size_t>(state)].probability
                * scratch.state_total_power[static_cast<std::size_t>(state)];
        }
        return scratch.expected_power;
    }

    [[nodiscard]] RunResult run(const RunConfig& config) const {
        const auto end_to_end_begin = Clock::now();
        RunResult result;
        result.case_id = case_id;
        result.problem_semantic_id = semantic_id;
        result.method_semantic_id =
            "l0371_deem_predecessor_completed_v1";
        result.seed = config.seed;
        result.requested_workers = config.workers;
        result.precomputation_seconds = precomputation_seconds;
        const fode::CounterRng rng(config.seed);
        fode::PersistentExecutor executor(config.workers);
        executor.reset_work_receipt();
        std::vector<int> layout = initialize(rng);
        Cache current;
        build_cache(layout, current, &executor);
        result.initial_evaluation =
            evaluation_from_cache(current, true);
        result.best_evaluation = result.initial_evaluation;
        result.best_candidate_indices = layout;
        result.best_power_history_kw.push_back(current.expected_power);
        Cache scratch;
        std::uint64_t generation = 0;
        const auto algorithm_begin = Clock::now();
        while (result.physical_fes < config.max_physical_fes) {
            const std::vector<int> snapshot = layout;
            std::vector<int> offspring(
                static_cast<std::size_t>(turbines)
            );
            std::vector<int> replacement(
                static_cast<std::size_t>(turbines)
            );
            for (int index = 0; index < turbines; ++index) {
                std::array<int, 3> parents{};
                for (int parent = 0; parent < 3; ++parent) {
                    int draw = 0;
                    int value = 0;
                    do {
                        value = rng.integer(
                            0,
                            turbines,
                            generation,
                            2,
                            static_cast<std::uint64_t>(index),
                            static_cast<std::uint64_t>(parent),
                            static_cast<std::uint64_t>(draw++)
                        );
                    } while (
                        value == index
                        || (
                            parent >= 1 && value == parents[0]
                        )
                        || (
                            parent >= 2 && value == parents[1]
                        )
                    );
                    parents[static_cast<std::size_t>(parent)] = value;
                }
                const Point& first = candidates[
                    static_cast<std::size_t>(snapshot[
                        static_cast<std::size_t>(parents[0])
                    ])
                ];
                const Point& second = candidates[
                    static_cast<std::size_t>(snapshot[
                        static_cast<std::size_t>(parents[1])
                    ])
                ];
                const Point& third = candidates[
                    static_cast<std::size_t>(snapshot[
                        static_cast<std::size_t>(parents[2])
                    ])
                ];
                const Point& target = candidates[
                    static_cast<std::size_t>(
                        snapshot[static_cast<std::size_t>(index)]
                    )
                ];
                const double mutant_x = first.x_m
                    + differential_weight * (second.x_m - third.x_m);
                const double mutant_y = first.y_m
                    + differential_weight * (second.y_m - third.y_m);
                double trial_x = mutant_x;
                double trial_y = mutant_y;
                const double frand = rng.uniform(
                    generation, 3, static_cast<std::uint64_t>(index)
                );
                if (frand >= crossover_rate) {
                    const int coordinate = rng.integer(
                        0,
                        2,
                        generation,
                        4,
                        static_cast<std::uint64_t>(index)
                    );
                    if (coordinate == 0) trial_y = target.y_m;
                    else trial_x = target.x_m;
                }
                offspring[static_cast<std::size_t>(index)] =
                    nearest_candidate(trial_x, trial_y);
                replacement[static_cast<std::size_t>(index)] = rng.integer(
                    0,
                    turbines,
                    generation,
                    5,
                    static_cast<std::uint64_t>(index)
                );
            }
            for (int index = 0;
                 index < turbines
                    && result.physical_fes < config.max_physical_fes;
                 ++index) {
                ++result.proposed_trials;
                const int replaced =
                    replacement[static_cast<std::size_t>(index)];
                const int candidate =
                    offspring[static_cast<std::size_t>(index)];
                if (!valid_replacement(layout, replaced, candidate)) {
                    ++result.rejected_constraint_trials;
                    continue;
                }
                const auto evaluator_begin = Clock::now();
                const double trial_power = trial_cache(
                    layout,
                    replaced,
                    candidate,
                    current,
                    scratch,
                    executor
                );
                result.evaluator_seconds += std::chrono::duration<double>(
                    Clock::now() - evaluator_begin
                ).count();
                ++result.physical_fes;
                if (power_better(trial_power, current.expected_power)) {
                    layout[static_cast<std::size_t>(replaced)] = candidate;
                    std::swap(current, scratch);
                    const Evaluation evaluation =
                        evaluation_from_cache(current, true);
                    if (
                        power_better(
                            evaluation.average_power_kw,
                            result.best_evaluation.average_power_kw
                        )
                    ) {
                        result.best_evaluation = evaluation;
                        result.best_candidate_indices = layout;
                    }
                }
                if (
                    result.physical_fes % 1000U == 0U
                    || result.physical_fes == config.max_physical_fes
                ) {
                    result.best_power_history_kw.push_back(
                        result.best_evaluation.average_power_kw
                    );
                }
            }
            ++generation;
        }
        result.algorithm_seconds = std::chrono::duration<double>(
            Clock::now() - algorithm_begin
        ).count() - result.evaluator_seconds;
        const auto receipt = executor.work_receipt();
        result.observed_workers = std::max(
            observed_precompute_workers,
            receipt.distinct_participants
        );
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (const int candidate : result.best_candidate_indices) {
            hash = mix_hash(hash, static_cast<std::uint64_t>(candidate));
        }
        hash = mix_hash(
            hash,
            std::bit_cast<std::uint64_t>(
                result.best_evaluation.average_power_kw
            )
        );
        result.scientific_hash = hash;
        result.end_to_end_seconds = std::chrono::duration<double>(
            Clock::now() - end_to_end_begin
        ).count();
        return result;
    }
};

Problem::Problem(
    std::string case_id,
    const std::string& proxy_path,
    const int workers
) : impl_(std::make_unique<Impl>()) {
    if (workers <= 0) {
        throw std::invalid_argument("L0371 workers must be positive");
    }
    impl_->case_id = std::move(case_id);
    impl_->workers = workers;
    impl_->load_proxy(proxy_path);
    impl_->configure_case();
    impl_->build_states();
    impl_->precompute();
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const std::string& Problem::case_id() const noexcept {
    return impl_->case_id;
}

const std::string& Problem::semantic_id() const noexcept {
    return impl_->semantic_id;
}

const std::vector<Point>& Problem::candidates() const noexcept {
    return impl_->candidates;
}

const std::vector<WindState>& Problem::states() const noexcept {
    return impl_->states;
}

int Problem::turbine_count() const noexcept {
    return impl_->turbines;
}

double Problem::minimum_spacing_m() const noexcept {
    return impl_->spacing_m;
}

double Problem::precomputation_seconds() const noexcept {
    return impl_->precomputation_seconds;
}

int Problem::observed_precomputation_workers() const noexcept {
    return impl_->observed_precompute_workers;
}

bool Problem::is_horns() const noexcept {
    return impl_->horns;
}

Evaluation Problem::evaluate(
    const std::vector<int>& candidate_indices
) const {
    if (!impl_->feasible_layout(candidate_indices)) {
        Evaluation invalid;
        invalid.feasible = false;
        invalid.coe = std::numeric_limits<double>::infinity();
        return invalid;
    }
    Impl::Cache cache;
    impl_->build_cache(candidate_indices, cache);
    return impl_->evaluation_from_cache(cache, true);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0 || config.max_physical_fes == 0U) {
        throw std::invalid_argument("invalid L0371 run configuration");
    }
    return impl_->run(config);
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const char wind_case : {'a', 'b', 'c'}) {
        for (const char* stability : stability_names) {
            result.push_back(
                std::string("l0371_ideal_") + wind_case + "_" + stability
            );
        }
    }
    for (const char* stability : stability_names) {
        result.push_back(std::string("l0371_horns_") + stability);
    }
    result.push_back("l0371_horns_actual");
    return result;
}

}  // namespace core99::l0371
