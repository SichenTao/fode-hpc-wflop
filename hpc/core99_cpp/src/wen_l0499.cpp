/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0499 pure-C++ uncertain-wind evaluator, CVaR and GA
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/wen_l0499.hpp
Controlling contract:
shared/contracts/core99_l0499_wen_uncertain_cvar_2022.json
Independent validator: scripts/validate_core99_l0499.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/wen_l0499.hpp"

#include "fode/executor.hpp"
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

namespace core99::l0499 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int population_size = 64;
constexpr int turbine_count_fixed = 50;
constexpr double crossover_probability = 0.9;
constexpr double ambient_speed_mps = 8.0;
constexpr double rotor_radius_m = 43.043;
constexpr double wake_expansion = 0.0707;
constexpr double minimum_spacing = 4.0 * rotor_radius_m;
constexpr double cvar_tail_coefficient_beta_0_8 =
    1.3998096020390416;
constexpr double mutation_probability = 1.0;

template <class T>
T read_binary(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!stream) throw std::runtime_error("truncated L0499 proxy fixture");
    return value;
}

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

double digamma(double value) {
    double result = 0.0;
    while (value < 8.0) {
        result -= 1.0 / value;
        value += 1.0;
    }
    const double inverse = 1.0 / value;
    const double inverse2 = inverse * inverse;
    result += std::log(value) - 0.5 * inverse
        - inverse2 * (
            1.0 / 12.0 - inverse2 * (
                1.0 / 120.0 - inverse2 * (
                    1.0 / 252.0 - inverse2 / 240.0
                )
            )
        );
    return result;
}

double circle_overlap(
    const double first_radius,
    const double second_radius,
    const double separation
) {
    if (separation >= first_radius + second_radius) return 0.0;
    if (separation <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double d2 = separation * separation;
    const double r12 = first_radius * first_radius;
    const double r22 = second_radius * second_radius;
    const double first_angle = std::acos(std::clamp(
        (d2 + r12 - r22) / (2.0 * separation * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (d2 + r22 - r12) / (2.0 * separation * second_radius),
        -1.0,
        1.0
    ));
    const double radicand = std::max(
        0.0,
        (-separation + first_radius + second_radius)
        * (separation + first_radius - second_radius)
        * (separation - first_radius + second_radius)
        * (separation + first_radius + second_radius)
    );
    return r12 * first_angle + r22 * second_angle
        - 0.5 * std::sqrt(radicand);
}

double interpolate_curve(
    const std::vector<std::array<double, 3>>& curve,
    const double speed,
    const int column
) {
    if (speed <= curve.front()[0]) return curve.front()[column];
    for (std::size_t index = 1; index < curve.size(); ++index) {
        if (speed <= curve[index][0]) {
            const double width = curve[index][0] - curve[index - 1][0];
            const double fraction = width > 0.0
                ? (speed - curve[index - 1][0]) / width : 0.0;
            return curve[index - 1][column] + fraction * (
                curve[index][column] - curve[index - 1][column]
            );
        }
    }
    return curve.back()[column];
}

std::vector<double> dm_covariance(
    const std::vector<double>& alpha
) {
    constexpr double yearly_count = 8760.0;
    const double alpha0 = std::accumulate(
        alpha.begin(), alpha.end(), 0.0
    );
    if (!(alpha0 > 0.0)) {
        throw std::runtime_error("invalid L0499 Dirichlet alpha sum");
    }
    const std::size_t sectors = alpha.size();
    std::vector<double> covariance(sectors * sectors, 0.0);
    const double common = (yearly_count + alpha0)
        / (
            yearly_count * alpha0 * alpha0 * (1.0 + alpha0)
        );
    for (std::size_t row = 0; row < sectors; ++row) {
        for (std::size_t column = 0; column < sectors; ++column) {
            const double numerator = row == column
                ? alpha[row] * (alpha0 - alpha[row])
                : -alpha[row] * alpha[column];
            covariance[row * sectors + column] = common * numerator;
        }
    }
    return covariance;
}

std::vector<double> fit_dirichlet_multinomial(
    const std::vector<std::vector<double>>& samples
) {
    if (samples.empty()) {
        throw std::runtime_error("no L0499 historical WRFV samples");
    }
    const std::size_t sectors = samples.front().size();
    std::vector<std::vector<int>> counts(
        samples.size(), std::vector<int>(sectors, 0)
    );
    std::vector<double> alpha(sectors, 0.0);
    for (std::size_t year = 0; year < samples.size(); ++year) {
        int assigned = 0;
        for (std::size_t sector = 0; sector < sectors; ++sector) {
            counts[year][sector] = static_cast<int>(
                std::floor(samples[year][sector] * 8760.0)
            );
            assigned += counts[year][sector];
        }
        for (int extra = assigned; extra < 8760; ++extra) {
            const auto sector = static_cast<std::size_t>(
                extra - assigned
            ) % sectors;
            ++counts[year][sector];
        }
        for (std::size_t sector = 0; sector < sectors; ++sector) {
            alpha[sector] += static_cast<double>(counts[year][sector]);
        }
    }
    for (double& value : alpha) {
        value = std::max(value / static_cast<double>(samples.size()), 1.0);
    }
    for (int iteration = 0; iteration < 500; ++iteration) {
        const double alpha0 = std::accumulate(
            alpha.begin(), alpha.end(), 0.0
        );
        double denominator = 0.0;
        for (std::size_t year = 0; year < samples.size(); ++year) {
            denominator += digamma(8760.0 + alpha0) - digamma(alpha0);
        }
        std::vector<double> updated(sectors, 0.0);
        double maximum_relative_change = 0.0;
        for (std::size_t sector = 0; sector < sectors; ++sector) {
            double numerator = 0.0;
            for (std::size_t year = 0; year < samples.size(); ++year) {
                numerator += digamma(
                    static_cast<double>(counts[year][sector])
                    + alpha[sector]
                ) - digamma(alpha[sector]);
            }
            updated[sector] = std::max(
                alpha[sector] * numerator / denominator,
                1.0e-6
            );
            maximum_relative_change = std::max(
                maximum_relative_change,
                std::abs(updated[sector] - alpha[sector])
                    / std::max(alpha[sector], 1.0e-12)
            );
        }
        alpha = std::move(updated);
        if (maximum_relative_change < 1.0e-10) break;
    }
    return alpha;
}

std::vector<int> random_layout(
    const int candidates,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual
) {
    std::vector<int> pool(static_cast<std::size_t>(candidates));
    std::iota(pool.begin(), pool.end(), 0);
    for (int index = 0; index < turbine_count_fixed; ++index) {
        const int selected = random.integer(
            index,
            candidates,
            generation,
            10,
            individual,
            static_cast<std::uint64_t>(index)
        );
        std::swap(
            pool[static_cast<std::size_t>(index)],
            pool[static_cast<std::size_t>(selected)]
        );
    }
    pool.resize(turbine_count_fixed);
    std::sort(pool.begin(), pool.end());
    return pool;
}

}  // namespace

struct Problem::Impl {
    std::string case_id;
    std::string semantic_id;
    std::string objective_name;
    int station = -1;
    int workers = 20;
    int observed_precompute_workers = 0;
    double grid_side_m = 0.0;
    double precomputation_seconds = 0.0;
    std::vector<Point> candidates;
    std::vector<double> directions_degrees;
    std::vector<double> wind_mean_values;
    std::vector<double> wind_covariance_values;
    std::vector<std::array<double, 3>> turbine_curve;
    std::array<double, 5> case_a_alpha{};
    std::vector<float> station_year_frequencies;
    std::vector<float> wake_ratios;

    [[nodiscard]] std::size_t wake_index(
        const int direction,
        const int source,
        const int target
    ) const {
        const std::size_t count = candidates.size();
        return (
            static_cast<std::size_t>(direction) * count
            + static_cast<std::size_t>(source)
        ) * count + static_cast<std::size_t>(target);
    }

    void load_proxy(const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open L0499 proxy");
        std::array<char, 7> magic{};
        stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!stream || std::string(magic.data(), magic.size()) != "L0499P1") {
            throw std::runtime_error("invalid L0499 proxy magic");
        }
        const auto station_count = read_binary<std::uint32_t>(stream);
        const auto year_count = read_binary<std::uint32_t>(stream);
        const auto sector_count = read_binary<std::uint32_t>(stream);
        const auto curve_count = read_binary<std::uint32_t>(stream);
        if (
            station_count != 41U || year_count != 20U
            || sector_count != 12U || curve_count < 2U
        ) {
            throw std::runtime_error("L0499 proxy dimensions mismatch");
        }
        for (double& value : case_a_alpha) {
            value = read_binary<double>(stream);
        }
        turbine_curve.resize(curve_count);
        for (auto& point : turbine_curve) {
            point[0] = read_binary<float>(stream);
            point[1] = read_binary<float>(stream);
            point[2] = read_binary<float>(stream);
        }
        station_year_frequencies.resize(41U * 20U * 12U);
        for (float& value : station_year_frequencies) {
            value = read_binary<float>(stream);
        }
        if (stream.peek() != std::ifstream::traits_type::eof()) {
            throw std::runtime_error("unexpected trailing L0499 proxy data");
        }
    }

    void configure_case() {
        std::string objective_suffix;
        if (case_id.starts_with("l0499_case_a_")) {
            semantic_id = "l0499_case_a_dm_cvar_grid_v1";
            objective_suffix = case_id.substr(
                std::string("l0499_case_a_").size()
            );
            station = -1;
            grid_side_m = 2000.0;
            for (int y = 0; y < 12; ++y) {
                for (int x = 0; x < 12; ++x) {
                    candidates.push_back({
                        2000.0 * static_cast<double>(x) / 11.0,
                        2000.0 * static_cast<double>(y) / 11.0
                    });
                }
            }
            directions_degrees = {0.0, 30.0, 60.0, 90.0, 120.0};
            std::vector<double> alpha(
                case_a_alpha.begin(), case_a_alpha.end()
            );
            const double alpha0 = std::accumulate(
                alpha.begin(), alpha.end(), 0.0
            );
            wind_mean_values.resize(alpha.size());
            std::transform(
                alpha.begin(), alpha.end(), wind_mean_values.begin(),
                [alpha0](const double value) { return value / alpha0; }
            );
            wind_covariance_values = dm_covariance(alpha);
        } else if (case_id.starts_with("l0499_case_b_station_")) {
            semantic_id =
                "l0499_case_b_ndawn41_proxy_dm_cvar_grid_v1";
            const std::string suffix = case_id.substr(
                std::string("l0499_case_b_station_").size()
            );
            if (suffix.size() != 5U || suffix[2] != '_') {
                throw std::invalid_argument("invalid L0499 station case ID");
            }
            station = std::stoi(suffix.substr(0, 2)) - 1;
            if (station < 0 || station >= 41) {
                throw std::invalid_argument("L0499 station is out of range");
            }
            objective_suffix = suffix.substr(3);
            grid_side_m = 1550.0;
            for (int y = 0; y < 10; ++y) {
                for (int x = 0; x < 10; ++x) {
                    candidates.push_back({
                        1550.0 * static_cast<double>(x) / 9.0,
                        1550.0 * static_cast<double>(y) / 9.0
                    });
                }
            }
            for (int direction = 0; direction < 12; ++direction) {
                directions_degrees.push_back(
                    30.0 * static_cast<double>(direction)
                );
            }
            std::vector<std::vector<double>> historical(
                10, std::vector<double>(12, 0.0)
            );
            for (int year = 0; year < 10; ++year) {
                double total = 0.0;
                for (int direction = 0; direction < 12; ++direction) {
                    const std::size_t index = (
                        static_cast<std::size_t>(station) * 20U
                        + static_cast<std::size_t>(year)
                    ) * 12U + static_cast<std::size_t>(direction);
                    historical[static_cast<std::size_t>(year)]
                        [static_cast<std::size_t>(direction)] =
                            station_year_frequencies[index];
                    total += station_year_frequencies[index];
                }
                for (double& value : historical[
                    static_cast<std::size_t>(year)
                ]) {
                    value /= total;
                }
            }
            const auto alpha = fit_dirichlet_multinomial(historical);
            const double alpha0 = std::accumulate(
                alpha.begin(), alpha.end(), 0.0
            );
            wind_mean_values.resize(alpha.size());
            std::transform(
                alpha.begin(), alpha.end(), wind_mean_values.begin(),
                [alpha0](const double value) { return value / alpha0; }
            );
            wind_covariance_values = dm_covariance(alpha);
        } else {
            throw std::invalid_argument("unknown L0499 case ID");
        }
        if (objective_suffix == "to") objective_name = "to";
        else if (objective_suffix == "so") objective_name = "so";
        else if (objective_suffix == "ro") objective_name = "ro";
        else throw std::invalid_argument("unknown L0499 objective variant");
    }

    void precompute() {
        const auto start = Clock::now();
        const int count = static_cast<int>(candidates.size());
        const int sector_count = static_cast<int>(
            directions_degrees.size()
        );
        wake_ratios.assign(
            static_cast<std::size_t>(sector_count)
                * static_cast<std::size_t>(count)
                * static_cast<std::size_t>(count),
            0.0F
        );
        fode::PersistentExecutor executor(workers);
        executor.reset_work_receipt();
        executor.parallel_for(0, sector_count * count, [&](const int item) {
            const int direction = item / count;
            const int source = item % count;
            const double radians = directions_degrees[
                static_cast<std::size_t>(direction)
            ] * std::numbers::pi / 180.0;
            const double flow_x = -std::sin(radians);
            const double flow_y = -std::cos(radians);
            const double ct = interpolate_curve(
                turbine_curve, ambient_speed_mps, 2
            );
            const double base_deficit = 1.0 - std::sqrt(
                std::max(0.0, 1.0 - ct)
            );
            for (int target = 0; target < count; ++target) {
                if (source == target) continue;
                const double dx = candidates[
                    static_cast<std::size_t>(target)
                ].x_m - candidates[static_cast<std::size_t>(source)].x_m;
                const double dy = candidates[
                    static_cast<std::size_t>(target)
                ].y_m - candidates[static_cast<std::size_t>(source)].y_m;
                const double downwind = dx * flow_x + dy * flow_y;
                if (downwind <= 0.0) continue;
                const double crosswind = std::abs(
                    -dx * flow_y + dy * flow_x
                );
                const double wake_radius =
                    rotor_radius_m + wake_expansion * downwind;
                const double overlap = circle_overlap(
                    wake_radius, rotor_radius_m, crosswind
                );
                const double expanded = rotor_radius_m / wake_radius;
                const double overlap_fraction = overlap / (
                    std::numbers::pi * rotor_radius_m * rotor_radius_m
                );
                wake_ratios[wake_index(direction, source, target)] =
                    static_cast<float>(
                        base_deficit * expanded * expanded
                        * overlap_fraction
                    );
            }
        });
        const auto receipt = executor.work_receipt();
        observed_precompute_workers = receipt.distinct_participants;
        precomputation_seconds = elapsed_seconds(start);
    }

    [[nodiscard]] bool valid_layout(
        const std::vector<int>& layout
    ) const {
        if (layout.size() != turbine_count_fixed) return false;
        for (std::size_t first = 0; first < layout.size(); ++first) {
            if (
                layout[first] < 0
                || layout[first] >= static_cast<int>(candidates.size())
            ) return false;
            for (std::size_t second = 0; second < first; ++second) {
                if (layout[first] == layout[second]) return false;
                const Point& a = candidates[
                    static_cast<std::size_t>(layout[first])
                ];
                const Point& b = candidates[
                    static_cast<std::size_t>(layout[second])
                ];
                if (
                    std::hypot(a.x_m - b.x_m, a.y_m - b.y_m)
                    + 1.0e-9 < minimum_spacing
                ) return false;
            }
        }
        return true;
    }

    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<int>& layout
    ) const {
        Evaluation result;
        result.feasible = valid_layout(layout);
        if (!result.feasible) {
            result.objective = -std::numeric_limits<double>::infinity();
            return result;
        }
        const int sectors = static_cast<int>(directions_degrees.size());
        result.sector_power_kw.assign(
            static_cast<std::size_t>(sectors), 0.0
        );
        for (int sector = 0; sector < sectors; ++sector) {
            double farm_power = 0.0;
            for (int target = 0; target < turbine_count_fixed; ++target) {
                double sum_squared = 0.0;
                for (int source = 0; source < turbine_count_fixed; ++source) {
                    if (source == target) continue;
                    const double ratio = wake_ratios[wake_index(
                        sector,
                        layout[static_cast<std::size_t>(source)],
                        layout[static_cast<std::size_t>(target)]
                    )];
                    sum_squared += ratio * ratio;
                }
                const double speed = ambient_speed_mps * std::max(
                    0.0, 1.0 - std::sqrt(sum_squared)
                );
                farm_power += interpolate_curve(turbine_curve, speed, 1);
            }
            result.sector_power_kw[static_cast<std::size_t>(sector)] =
                farm_power;
        }
        double mean_power_kw = 0.0;
        for (int sector = 0; sector < sectors; ++sector) {
            mean_power_kw += wind_mean_values[
                static_cast<std::size_t>(sector)
            ] * result.sector_power_kw[static_cast<std::size_t>(sector)];
        }
        result.expected_aep_mwh = 8.76 * mean_power_kw;
        double variance_power = 0.0;
        for (int row = 0; row < sectors; ++row) {
            for (int column = 0; column < sectors; ++column) {
                variance_power +=
                    result.sector_power_kw[static_cast<std::size_t>(row)]
                    * wind_covariance_values[
                        static_cast<std::size_t>(row * sectors + column)
                    ]
                    * result.sector_power_kw[
                        static_cast<std::size_t>(column)
                    ];
            }
        }
        result.aep_standard_deviation_mwh =
            8.76 * std::sqrt(std::max(0.0, variance_power));
        result.cvar_mwh = result.expected_aep_mwh
            - cvar_tail_coefficient_beta_0_8
                * result.aep_standard_deviation_mwh;
        result.minimum_sector_power_kw = *std::min_element(
            result.sector_power_kw.begin(), result.sector_power_kw.end()
        );
        if (objective_name == "to") {
            result.objective = result.expected_aep_mwh;
        } else if (objective_name == "so") {
            result.objective = result.cvar_mwh;
        } else {
            result.objective = result.minimum_sector_power_kw;
        }
        return result;
    }
};

Problem::Problem(
    std::string case_id,
    const std::string& proxy_path,
    const int workers
) : impl_(std::make_unique<Impl>()) {
    if (workers <= 0) throw std::invalid_argument("workers must be positive");
    impl_->case_id = std::move(case_id);
    impl_->workers = workers;
    impl_->load_proxy(proxy_path);
    impl_->configure_case();
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
int Problem::turbine_count() const noexcept {
    return turbine_count_fixed;
}
int Problem::sector_count() const noexcept {
    return static_cast<int>(impl_->directions_degrees.size());
}
int Problem::station_index() const noexcept {
    return impl_->station;
}
const std::string& Problem::objective_variant() const noexcept {
    return impl_->objective_name;
}
double Problem::minimum_spacing_m() const noexcept {
    return minimum_spacing;
}
double Problem::precomputation_seconds() const noexcept {
    return impl_->precomputation_seconds;
}
int Problem::observed_precomputation_workers() const noexcept {
    return impl_->observed_precompute_workers;
}
const std::vector<double>& Problem::wind_mean() const noexcept {
    return impl_->wind_mean_values;
}
const std::vector<double>& Problem::wind_covariance() const noexcept {
    return impl_->wind_covariance_values;
}

Evaluation Problem::evaluate(
    const std::vector<int>& candidate_indices
) const {
    return impl_->evaluate_layout(candidate_indices);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0) {
        throw std::invalid_argument("L0499 optimizer workers must be positive");
    }
    if (
        config.max_physical_fes < static_cast<std::uint64_t>(population_size)
        || (
            config.max_physical_fes
            - static_cast<std::uint64_t>(population_size)
        ) % static_cast<std::uint64_t>(population_size) != 0U
    ) {
        throw std::invalid_argument(
            "L0499 physical FES must be 64 plus complete 64-member generations"
        );
    }
    struct Individual {
        std::vector<int> layout;
        Evaluation evaluation;
    };
    const auto better = [](const Individual& left, const Individual& right) {
        if (left.evaluation.objective != right.evaluation.objective) {
            return left.evaluation.objective > right.evaluation.objective;
        }
        return left.layout < right.layout;
    };
    const fode::CounterRng random(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::vector<Individual> population(population_size);
    for (int index = 0; index < population_size; ++index) {
        population[static_cast<std::size_t>(index)].layout = random_layout(
            static_cast<int>(impl_->candidates.size()),
            random,
            0,
            static_cast<std::uint64_t>(index)
        );
    }
    double evaluator_seconds = 0.0;
    const auto evaluate_population = [&](std::vector<Individual>& values) {
        const auto start = Clock::now();
        executor.parallel_for(0, static_cast<int>(values.size()), [&](int i) {
            values[static_cast<std::size_t>(i)].evaluation =
                impl_->evaluate_layout(
                    values[static_cast<std::size_t>(i)].layout
                );
        });
        evaluator_seconds += elapsed_seconds(start);
    };
    const auto algorithm_start = Clock::now();
    evaluate_population(population);
    std::stable_sort(population.begin(), population.end(), better);
    RunResult result;
    result.case_id = impl_->case_id;
    result.problem_semantic_id = impl_->semantic_id;
    result.method_semantic_id =
        "l0499_fixed_count_binary_ga_completed_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.physical_fes = population_size;
    result.initial_best = population.front().evaluation;
    result.best_objective_history.push_back(
        population.front().evaluation.objective
    );
    const std::uint64_t target_generations = (
        config.max_physical_fes
        - static_cast<std::uint64_t>(population_size)
    ) / static_cast<std::uint64_t>(population_size);
    auto tournament = [&](const std::uint64_t generation, const int child,
                          const int draw) {
        const int first = random.integer(
            0, population_size, generation, 20,
            static_cast<std::uint64_t>(child),
            static_cast<std::uint64_t>(draw), 0
        );
        const int second = random.integer(
            0, population_size, generation, 20,
            static_cast<std::uint64_t>(child),
            static_cast<std::uint64_t>(draw), 1
        );
        return better(
            population[static_cast<std::size_t>(first)],
            population[static_cast<std::size_t>(second)]
        ) ? first : second;
    };
    for (
        std::uint64_t generation = 1;
        generation <= target_generations;
        ++generation
    ) {
        std::vector<Individual> offspring(population_size);
        for (int child = 0; child < population_size; ++child) {
            const int first_parent = tournament(generation, child, 0);
            int second_parent = tournament(generation, child, 1);
            if (second_parent == first_parent) {
                second_parent = (second_parent + 1) % population_size;
            }
            const auto& first = population[
                static_cast<std::size_t>(first_parent)
            ].layout;
            const auto& second = population[
                static_cast<std::size_t>(second_parent)
            ].layout;
            std::vector<int> layout = first;
            if (
                random.uniform(generation, 21, child)
                < crossover_probability
            ) {
                std::vector<int> intersection;
                std::vector<int> symmetric;
                std::set_intersection(
                    first.begin(), first.end(),
                    second.begin(), second.end(),
                    std::back_inserter(intersection)
                );
                std::set_symmetric_difference(
                    first.begin(), first.end(),
                    second.begin(), second.end(),
                    std::back_inserter(symmetric)
                );
                for (std::size_t index = 0; index < symmetric.size(); ++index) {
                    const int selected = random.integer(
                        static_cast<int>(index),
                        static_cast<int>(symmetric.size()),
                        generation,
                        22,
                        static_cast<std::uint64_t>(child),
                        static_cast<std::uint64_t>(index)
                    );
                    std::swap(
                        symmetric[index],
                        symmetric[static_cast<std::size_t>(selected)]
                    );
                }
                layout = std::move(intersection);
                for (
                    const int candidate : symmetric
                ) {
                    if (layout.size() == turbine_count_fixed) break;
                    layout.push_back(candidate);
                }
                std::sort(layout.begin(), layout.end());
            }
            if (
                random.uniform(generation, 23, child)
                < mutation_probability
            ) {
                const int remove_slot = random.integer(
                    0,
                    turbine_count_fixed,
                    generation,
                    24,
                    static_cast<std::uint64_t>(child)
                );
                std::vector<bool> occupied(impl_->candidates.size(), false);
                for (const int candidate : layout) {
                    occupied[static_cast<std::size_t>(candidate)] = true;
                }
                occupied[static_cast<std::size_t>(
                    layout[static_cast<std::size_t>(remove_slot)]
                )] = false;
                int replacement = -1;
                const int start = random.integer(
                    0,
                    static_cast<int>(impl_->candidates.size()),
                    generation,
                    25,
                    static_cast<std::uint64_t>(child)
                );
                for (
                    int offset = 0;
                    offset < static_cast<int>(impl_->candidates.size());
                    ++offset
                ) {
                    const int candidate = (
                        start + offset
                    ) % static_cast<int>(impl_->candidates.size());
                    if (!occupied[static_cast<std::size_t>(candidate)]) {
                        replacement = candidate;
                        break;
                    }
                }
                layout[static_cast<std::size_t>(remove_slot)] = replacement;
                std::sort(layout.begin(), layout.end());
            }
            offspring[static_cast<std::size_t>(child)].layout =
                std::move(layout);
        }
        evaluate_population(offspring);
        result.physical_fes += population_size;
        population.insert(
            population.end(),
            std::make_move_iterator(offspring.begin()),
            std::make_move_iterator(offspring.end())
        );
        std::stable_sort(population.begin(), population.end(), better);
        population.resize(population_size);
        result.best_objective_history.push_back(
            population.front().evaluation.objective
        );
    }
    result.generations = target_generations;
    result.best_candidate_indices = population.front().layout;
    result.best_evaluation = population.front().evaluation;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(algorithm_start);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    result.precomputation_seconds = impl_->precomputation_seconds;
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const int candidate : result.best_candidate_indices) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(candidate));
    }
    for (const double objective : result.best_objective_history) {
        const auto quantized = static_cast<std::int64_t>(
            std::llround(objective * 1.0e6)
        );
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(quantized));
    }
    result.scientific_hash = hash;
    return result;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const char* objective : {"to", "so", "ro"}) {
        result.push_back(std::string("l0499_case_a_") + objective);
    }
    for (int station = 1; station <= 41; ++station) {
        const std::string number = station < 10
            ? "0" + std::to_string(station)
            : std::to_string(station);
        for (const char* objective : {"to", "so", "ro"}) {
            result.push_back(
                "l0499_case_b_station_" + number + "_" + objective
            );
        }
    }
    return result;
}

}  // namespace core99::l0499
