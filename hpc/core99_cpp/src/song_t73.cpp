/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T73 pure-C++ GA, pattern search, Jensen evaluator,
k-means and opportunistic condition-based maintenance simulation
Paper/DOI: Song et al.; 10.1016/j.cie.2018.04.051.
Public assets, missing fields, paper conflicts, declared completions, semantic
IDs, HPC design and claim boundary: include/core99/song_t73.hpp.
Semantic IDs and controlling contract:
shared/contracts/core99_t73_song_2018.json.
HPC realization: one persistent full-core executor evaluates complete binary-GA
candidates, fixed-index pattern-poll candidates and fixed-index maintenance
replications. Scenario trigonometry and the irregular site mask are immutable;
counter-keyed draws and ordered reductions make results scheduling-independent.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/song_t73.hpp"

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
#include <tuple>
#include <utility>
#include <vector>

namespace core99::t73 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int wind_samples = 40;
constexpr double farm_width_m = 3000.0;
constexpr double farm_height_m = 1500.0;
constexpr double minimum_spacing_m = 100.0;
constexpr double hub_height_m = 60.0;
constexpr double roughness_m = 0.3;
constexpr double rotor_radius_m = 20.0;
constexpr double thrust_coefficient = 0.88;
constexpr double wake_decay = 0.0944;
constexpr double axial_induction = 0.3268;
constexpr double cut_in_mps = 3.0;
constexpr double cut_out_mps = 22.0;
constexpr double energy_price_usd_mwh = 60.0;
constexpr double capacity_revenue_usd_turbine_year = 45000.0;
constexpr double annualized_unit_capital_usd = 160000.0;
constexpr int contract_days = 25 * 365;
constexpr double inspection_cost_usd = 25000.0;
constexpr double downtime_cost_usd_day = 20000.0;
constexpr double opportunistic_threshold = 0.65;
constexpr double condition_threshold = 0.85;
constexpr double failure_threshold = 1.0;

constexpr std::array<double, 4> opportunistic_cost{{1000, 750, 1500, 900}};
constexpr std::array<double, 4> condition_cost{{2000, 1500, 3000, 1800}};
constexpr std::array<double, 4> corrective_cost{{4000, 3250, 5500, 3700}};
constexpr std::array<double, 4> mean_life_years{{18.0, 14.0, 11.0, 13.0}};

struct WindSample {
    double speed_mps = 0.0;
    double downwind_x = 0.0;
    double downwind_y = 0.0;
    double weight = 0.0;
};

struct Individual {
    std::vector<std::uint8_t> bits;
    LayoutEvaluation evaluation;
};

struct MaintenanceReceipt {
    MaintenanceEvaluation evaluation;
    std::uint64_t life_events = 0;
};

struct ReplicationReceipt {
    std::array<double, 6> cost{};
    std::uint64_t life_events = 0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t hash_double(const double value) {
    return std::bit_cast<std::uint64_t>(value);
}

double weibull_sample(
    const fode::CounterRng& rng,
    const double shape,
    const double scale,
    const std::uint64_t sample
) {
    const double u = std::clamp(
        rng.uniform(0, 10, sample), 1.0e-15, 1.0 - 1.0e-15
    );
    return scale * std::pow(-std::log1p(-u), 1.0 / shape);
}

std::vector<Point> build_candidate_mask() {
    struct RankedPoint {
        Point point;
        double penalty = 0.0;
    };
    std::vector<RankedPoint> ranked;
    ranked.reserve(31U * 16U);
    for (int row = 0; row <= 15; ++row) {
        for (int column = 0; column <= 30; ++column) {
            const double x = 100.0 * static_cast<double>(column);
            const double y = 100.0 * static_cast<double>(row);
            const double xmin = 1600.0 * (1.0 - y / farm_height_m);
            const double xmax = farm_width_m
                - 150.0 * std::abs(y - 750.0) / 750.0;
            const double penalty = std::max(0.0, xmin - x)
                + std::max(0.0, x - xmax);
            ranked.push_back({{x, y}, penalty});
        }
    }
    std::stable_sort(
        ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
            return std::tie(left.penalty, left.point.y_m, left.point.x_m)
                < std::tie(right.penalty, right.point.y_m, right.point.x_m);
        }
    );
    ranked.resize(342);
    std::vector<Point> result;
    result.reserve(ranked.size());
    for (const auto& item : ranked) result.push_back(item.point);
    std::sort(result.begin(), result.end(), [](const Point& left, const Point& right) {
        return std::tie(left.y_m, left.x_m) < std::tie(right.y_m, right.x_m);
    });
    return result;
}

std::vector<WindSample> build_wind_samples() {
    fode::CounterRng rng(732014);
    std::vector<WindSample> result;
    result.reserve(wind_samples);
    for (int index = 0; index < wind_samples; ++index) {
        const bool first = index < 20;
        const double shape = first ? 3.0523 : 2.5042;
        const double scale = first ? 10.5787 : 9.6388;
        const double speed = weibull_sample(rng, shape, scale, index);
        double theta_deg = std::exp(
            5.3566 + 0.1184
                * rng.normal(0, 11, static_cast<std::uint64_t>(index))
        );
        theta_deg = std::fmod(theta_deg, 360.0);
        const double theta = theta_deg * std::numbers::pi / 180.0;
        result.push_back({
            speed,
            std::cos(theta),
            std::sin(theta),
            (first ? 14.0 : 10.0) / (20.0 * 24.0),
        });
    }
    return result;
}

double distance(const Point& left, const Point& right) {
    return std::hypot(left.x_m - right.x_m, left.y_m - right.y_m);
}

bool inside_site(const Point& point) {
    return point.x_m >= 0.0 && point.x_m <= farm_width_m
        && point.y_m >= 0.0 && point.y_m <= farm_height_m;
}

std::vector<Point> decode(
    const std::vector<std::uint8_t>& bits,
    const std::vector<Point>& candidates
) {
    std::vector<Point> layout;
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (bits[index] != 0U) layout.push_back(candidates[index]);
    }
    return layout;
}

double power_kw(const double speed) {
    if (speed < cut_in_mps || speed > cut_out_mps) return 0.0;
    return 0.3 * speed * speed * speed;
}

LayoutEvaluation evaluate_layout_impl(
    const std::vector<Point>& layout,
    const std::vector<WindSample>& scenarios
) {
    LayoutEvaluation result;
    result.turbine_count = static_cast<int>(layout.size());
    if (layout.empty()) return result;
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t left = 0; left < layout.size(); ++left) {
        if (!inside_site(layout[left])) return result;
        for (std::size_t right = left + 1; right < layout.size(); ++right) {
            minimum = std::min(minimum, distance(layout[left], layout[right]));
        }
    }
    result.minimum_spacing_m = layout.size() == 1U ? farm_width_m : minimum;
    if (minimum + 1.0e-9 < minimum_spacing_m) return result;

    double mean_power_kw = 0.0;
    for (const auto& scenario : scenarios) {
        for (std::size_t downstream = 0; downstream < layout.size(); ++downstream) {
            double squared = 0.0;
            for (std::size_t upstream = 0; upstream < layout.size(); ++upstream) {
                if (upstream == downstream) continue;
                const double dx = layout[downstream].x_m - layout[upstream].x_m;
                const double dy = layout[downstream].y_m - layout[upstream].y_m;
                const double along = dx * scenario.downwind_x
                    + dy * scenario.downwind_y;
                if (along <= 0.0) continue;
                const double cross = std::abs(-dx * scenario.downwind_y
                                              + dy * scenario.downwind_x);
                if (cross > rotor_radius_m + wake_decay * along) continue;
                const double pair_deficit = 2.0 * axial_induction
                    / std::pow(1.0 + wake_decay * along / rotor_radius_m, 2.0);
                squared += pair_deficit * pair_deficit;
            }
            const double coast_factor = 0.8
                + 0.2 * layout[downstream].x_m / farm_width_m;
            const double effective_speed = scenario.speed_mps * coast_factor
                * std::max(0.0, 1.0 - std::sqrt(squared));
            mean_power_kw += scenario.weight * power_kw(effective_speed);
        }
    }
    result.annual_energy_mwh = mean_power_kw * 8760.0 / 1000.0;
    result.energy_revenue_usd = result.annual_energy_mwh * energy_price_usd_mwh;
    result.capacity_revenue_usd = capacity_revenue_usd_turbine_year
        * static_cast<double>(result.turbine_count);
    const double count = static_cast<double>(result.turbine_count);
    result.annualized_capital_usd = annualized_unit_capital_usd * count
        * (2.0 / 3.0 + std::exp(-0.00174 * count * count) / 3.0);
    result.pre_maintenance_profit_usd = result.energy_revenue_usd
        + result.capacity_revenue_usd - result.annualized_capital_usd;
    result.feasible = true;
    return result;
}

double objective(const LayoutEvaluation& value) {
    return value.feasible ? value.pre_maintenance_profit_usd
        : -std::numeric_limits<double>::infinity();
}

std::vector<int> kmeans(const std::vector<Point>& points, const int cluster_count) {
    if (points.empty()) throw std::invalid_argument("T73 cannot cluster empty layout");
    std::vector<int> order(points.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](const int left, const int right) {
        return std::tie(points[left].x_m, points[left].y_m)
            < std::tie(points[right].x_m, points[right].y_m);
    });
    std::vector<Point> centroids;
    for (int cluster = 0; cluster < cluster_count; ++cluster) {
        const std::size_t slot = (static_cast<std::size_t>(2 * cluster + 1)
            * points.size()) / static_cast<std::size_t>(2 * cluster_count);
        centroids.push_back(points[order[std::min(slot, points.size() - 1U)]]);
    }
    std::vector<int> assignment(points.size(), 0);
    for (int iteration = 0; iteration < 30; ++iteration) {
        for (std::size_t index = 0; index < points.size(); ++index) {
            int best = 0;
            double best_distance = distance(points[index], centroids[0]);
            for (int cluster = 1; cluster < cluster_count; ++cluster) {
                const double candidate = distance(points[index], centroids[cluster]);
                if (candidate < best_distance) {
                    best_distance = candidate;
                    best = cluster;
                }
            }
            assignment[index] = best;
        }
        std::vector<Point> next(static_cast<std::size_t>(cluster_count));
        std::vector<int> counts(static_cast<std::size_t>(cluster_count), 0);
        for (std::size_t index = 0; index < points.size(); ++index) {
            next[assignment[index]].x_m += points[index].x_m;
            next[assignment[index]].y_m += points[index].y_m;
            ++counts[assignment[index]];
        }
        for (int cluster = 0; cluster < cluster_count; ++cluster) {
            if (counts[cluster] > 0) {
                next[cluster].x_m /= static_cast<double>(counts[cluster]);
                next[cluster].y_m /= static_cast<double>(counts[cluster]);
            } else {
                next[cluster] = centroids[cluster];
            }
        }
        centroids = std::move(next);
    }
    return assignment;
}

ReplicationReceipt simulate_replication(
    const std::vector<int>& clusters,
    const int cluster_count,
    const int interval,
    const std::uint64_t seed,
    const int replication
) {
    const std::size_t component_count = clusters.size() * 4U;
    std::vector<double> degradation(component_count, 0.0);
    std::vector<double> rate(component_count, 0.0);
    std::vector<std::uint64_t> cycles(component_count, 0);
    fode::CounterRng rng(seed);
    auto refresh_rate = [&](const std::size_t component) {
        const int type = static_cast<int>(component % 4U);
        const double mean_daily = 1.0 / (365.0 * mean_life_years[type]);
        const double draw = rng.normal(
            static_cast<std::uint64_t>(replication), 40,
            static_cast<std::uint64_t>(component), cycles[component]
        );
        rate[component] = mean_daily * std::exp(0.30 * draw - 0.045);
        ++cycles[component];
    };
    for (std::size_t component = 0; component < component_count; ++component) {
        refresh_rate(component);
    }

    ReplicationReceipt receipt;
    int previous = 0;
    for (int scheduled = interval; scheduled <= contract_days; scheduled += interval) {
        const std::uint64_t weather_event
            = static_cast<std::uint64_t>(replication) * 100000ULL
                + static_cast<std::uint64_t>(scheduled);
        const double inspection_wind = weibull_sample(
            rng, 2.75, 10.1, weather_event
        );
        const int weather_delay = inspection_wind > 15.0
            ? std::min(5, 1 + static_cast<int>(inspection_wind - 15.0)) : 0;
        const int actual_inspection = std::min(contract_days, scheduled + weather_delay);
        const int span = actual_inspection - previous;
        previous = actual_inspection;
        for (std::size_t component = 0; component < component_count; ++component) {
            degradation[component] += rate[component] * static_cast<double>(span);
        }
        receipt.cost[0] += inspection_cost_usd;
        std::vector<std::uint8_t> cluster_trigger(
            static_cast<std::size_t>(cluster_count), 0U
        );
        for (std::size_t component = 0; component < component_count; ++component) {
            if (degradation[component] >= condition_threshold) {
                cluster_trigger[clusters[component / 4U]] = 1U;
            }
        }
        for (std::size_t component = 0; component < component_count; ++component) {
            const int type = static_cast<int>(component % 4U);
            const double value = degradation[component];
            bool maintained = false;
            if (value >= failure_threshold) {
                receipt.cost[4] += corrective_cost[type];
                const double before = value
                    - rate[component] * static_cast<double>(span);
                const double failure_offset = std::clamp(
                    (failure_threshold - before) / rate[component],
                    0.0, static_cast<double>(span)
                );
                const double downtime = std::clamp(
                    static_cast<double>(span) - failure_offset, 0.0,
                    static_cast<double>(span)
                );
                receipt.cost[5] += downtime;
                maintained = true;
            } else if (value >= condition_threshold) {
                receipt.cost[3] += condition_cost[type];
                maintained = true;
            } else if (value >= opportunistic_threshold
                       && cluster_trigger[clusters[component / 4U]] != 0U) {
                receipt.cost[2] += opportunistic_cost[type];
                maintained = true;
            }
            if (maintained) {
                degradation[component] = 0.0;
                refresh_rate(component);
                ++receipt.life_events;
            }
        }
    }
    receipt.cost[1] = receipt.cost[0] + receipt.cost[2] + receipt.cost[3]
        + receipt.cost[4] + downtime_cost_usd_day * receipt.cost[5];
    return receipt;
}

MaintenanceReceipt evaluate_maintenance_impl(
    const std::vector<int>& clusters,
    const int interval,
    const int replications,
    const std::uint64_t seed,
    fode::PersistentExecutor& executor
) {
    if (interval <= 0 || replications <= 0 || clusters.empty()) {
        throw std::invalid_argument("T73 invalid maintenance configuration");
    }
    const int cluster_count = *std::max_element(clusters.begin(), clusters.end()) + 1;
    std::vector<ReplicationReceipt> receipts(static_cast<std::size_t>(replications));
    executor.parallel_for(0, replications, [&](const int replication) {
        receipts[replication] = simulate_replication(
            clusters, cluster_count, interval, seed, replication
        );
    });
    MaintenanceReceipt result;
    result.evaluation.inspection_interval_days = interval;
    for (const auto& receipt : receipts) {
        result.evaluation.inspection_cost_usd += receipt.cost[0];
        result.evaluation.mean_cost_usd += receipt.cost[1];
        result.evaluation.opportunistic_cost_usd += receipt.cost[2];
        result.evaluation.condition_cost_usd += receipt.cost[3];
        result.evaluation.corrective_cost_usd += receipt.cost[4];
        result.evaluation.mean_downtime_days += receipt.cost[5];
        result.life_events += receipt.life_events;
    }
    const double divisor = static_cast<double>(replications);
    result.evaluation.inspection_cost_usd /= divisor;
    result.evaluation.mean_cost_usd /= divisor;
    result.evaluation.opportunistic_cost_usd /= divisor;
    result.evaluation.condition_cost_usd /= divisor;
    result.evaluation.corrective_cost_usd /= divisor;
    result.evaluation.mean_downtime_days /= divisor;
    result.evaluation.downtime_cost_usd = downtime_cost_usd_day
        * result.evaluation.mean_downtime_days;
    return result;
}

std::uint64_t hash_result(const RunResult& result) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_hash(hash, result.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(result.cluster_count));
    for (const auto& point : result.discrete_layout) {
        hash = mix_hash(hash, hash_double(point.x_m));
        hash = mix_hash(hash, hash_double(point.y_m));
    }
    for (const auto& point : result.continuous_layout) {
        hash = mix_hash(hash, hash_double(point.x_m));
        hash = mix_hash(hash, hash_double(point.y_m));
    }
    for (const int cluster : result.cluster_assignment) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(cluster));
    }
    for (const auto& role : result.roles) {
        for (const char value : role.role) {
            hash = mix_hash(hash, static_cast<unsigned char>(value));
        }
        hash = mix_hash(hash, hash_double(role.layout.pre_maintenance_profit_usd));
        hash = mix_hash(hash, static_cast<std::uint64_t>(
            role.maintenance.inspection_interval_days
        ));
        hash = mix_hash(hash, hash_double(role.maintenance.mean_cost_usd));
        hash = mix_hash(hash, hash_double(role.integrated_profit_usd));
    }
    return hash;
}

}  // namespace

struct Problem::Impl {
    std::vector<Point> candidates = build_candidate_mask();
    std::vector<WindSample> scenarios = build_wind_samples();
};

Problem::Problem() : impl_(std::make_unique<Impl>()) {
    static_assert(hub_height_m == 60.0);
    static_assert(roughness_m == 0.3);
    static_assert(thrust_coefficient == 0.88);
    if (impl_->candidates.size() != 342U || impl_->scenarios.size() != 40U) {
        throw std::runtime_error("T73 paper dimensions not constructed");
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

int Problem::candidate_count() const noexcept {
    return static_cast<int>(impl_->candidates.size());
}

const std::vector<Point>& Problem::candidate_points() const noexcept {
    return impl_->candidates;
}

LayoutEvaluation Problem::evaluate_layout(const std::vector<Point>& layout) const {
    return evaluate_layout_impl(layout, impl_->scenarios);
}

MaintenanceEvaluation Problem::evaluate_maintenance(
    const std::vector<Point>& layout,
    const std::vector<int>& clusters,
    const int inspection_interval_days,
    const int replications,
    const std::uint64_t seed,
    const int workers
) const {
    if (layout.size() != clusters.size()) {
        throw std::invalid_argument("T73 cluster vector size mismatch");
    }
    fode::PersistentExecutor executor(workers);
    return evaluate_maintenance_impl(
        clusters, inspection_interval_days, replications, seed, executor
    ).evaluation;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0 || config.ga_population < 4
        || config.ga_generations < 0 || config.pattern_iterations < 0
        || config.maintenance_replications <= 0) {
        throw std::invalid_argument("T73 invalid run configuration");
    }
    const auto total_start = Clock::now();
    RunResult result;
    result.case_id = "nj342-" + to_string(config.cluster_profile);
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.ga_population = config.ga_population;
    result.ga_generations = config.ga_generations;
    result.pattern_iterations = config.pattern_iterations;
    result.maintenance_replications = config.maintenance_replications;
    result.cluster_count = config.cluster_profile == ClusterProfile::equation_text_four
        ? 4 : 2;

    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    fode::CounterRng rng(config.seed);
    const auto algorithm_start = Clock::now();
    const auto& candidates = problem.impl_->candidates;
    std::vector<Individual> population(static_cast<std::size_t>(config.ga_population));
    for (int individual = 0; individual < config.ga_population; ++individual) {
        auto& bits = population[individual].bits;
        bits.resize(candidates.size());
        const double occupancy = 0.25 + 0.60
            * rng.uniform(0, 1, static_cast<std::uint64_t>(individual));
        for (std::size_t gene = 0; gene < bits.size(); ++gene) {
            bits[gene] = rng.uniform(
                0, 2, static_cast<std::uint64_t>(individual), gene
            ) < occupancy ? 1U : 0U;
        }
    }
    std::vector<int> east_order(static_cast<int>(candidates.size()));
    std::iota(east_order.begin(), east_order.end(), 0);
    std::sort(east_order.begin(), east_order.end(), [&](const int left, const int right) {
        if (candidates[left].x_m != candidates[right].x_m) {
            return candidates[left].x_m > candidates[right].x_m;
        }
        return candidates[left].y_m < candidates[right].y_m;
    });
    std::fill(population[0].bits.begin(), population[0].bits.end(), 0U);
    for (int index = 0; index < 232; ++index) {
        population[0].bits[east_order[index]] = 1U;
    }

    auto evaluate_population = [&](std::vector<Individual>& items) {
        std::vector<double> timings(items.size(), 0.0);
        executor.parallel_for(0, static_cast<int>(items.size()), [&](const int index) {
            const auto start = Clock::now();
            items[index].evaluation = evaluate_layout_impl(
                decode(items[index].bits, candidates), problem.impl_->scenarios
            );
            timings[index] = elapsed_seconds(start);
        });
        result.layout_evaluations += items.size();
        for (const auto& item : items) {
            result.wind_scenario_turbine_evaluations
                += static_cast<std::uint64_t>(wind_samples)
                    * static_cast<std::uint64_t>(item.evaluation.turbine_count);
        }
        result.layout_evaluator_seconds += std::accumulate(
            timings.begin(), timings.end(), 0.0
        );
    };
    evaluate_population(population);
    for (int generation = 0; generation < config.ga_generations; ++generation) {
        std::stable_sort(
            population.begin(), population.end(), [](const auto& left, const auto& right) {
                return objective(left.evaluation) > objective(right.evaluation);
            }
        );
        std::vector<Individual> children(static_cast<std::size_t>(config.ga_population));
        children[0].bits = population[0].bits;
        children[1].bits = population[1].bits;
        for (int child = 2; child < config.ga_population; ++child) {
            auto tournament = [&](const std::uint64_t draw) {
                const int left = rng.integer(
                    0, config.ga_population, generation + 1, 3, child, draw
                );
                const int right = rng.integer(
                    0, config.ga_population, generation + 1, 4, child, draw
                );
                return objective(population[left].evaluation)
                    >= objective(population[right].evaluation) ? left : right;
            };
            const int parent_a = tournament(0);
            const int parent_b = tournament(1);
            children[child].bits.resize(candidates.size());
            for (std::size_t gene = 0; gene < candidates.size(); ++gene) {
                std::uint8_t value = rng.uniform(
                    generation + 1, 5, child, gene
                ) < 0.5 ? population[parent_a].bits[gene]
                        : population[parent_b].bits[gene];
                if (rng.uniform(generation + 1, 6, child, gene)
                    < 1.0 / static_cast<double>(candidates.size())) {
                    value = value == 0U ? 1U : 0U;
                }
                children[child].bits[gene] = value;
            }
        }
        evaluate_population(children);
        population = std::move(children);
    }
    std::stable_sort(
        population.begin(), population.end(), [](const auto& left, const auto& right) {
            return objective(left.evaluation) > objective(right.evaluation);
        }
    );
    result.discrete_layout = decode(population.front().bits, candidates);
    const LayoutEvaluation discrete_evaluation = population.front().evaluation;

    result.continuous_layout = result.discrete_layout;
    LayoutEvaluation continuous_evaluation = discrete_evaluation;
    double mesh = 50.0;
    for (int iteration = 0; iteration < config.pattern_iterations; ++iteration) {
        const int block = std::min(
            5, static_cast<int>(result.continuous_layout.size())
        );
        std::vector<std::vector<Point>> polls(static_cast<std::size_t>(4 * block));
        for (int slot = 0; slot < block; ++slot) {
            const int turbine = (5 * iteration + slot)
                % static_cast<int>(result.continuous_layout.size());
            for (int direction = 0; direction < 4; ++direction) {
                auto& candidate = polls[4 * slot + direction];
                candidate = result.continuous_layout;
                if (direction == 0) candidate[turbine].x_m += mesh;
                if (direction == 1) candidate[turbine].x_m -= mesh;
                if (direction == 2) candidate[turbine].y_m += mesh;
                if (direction == 3) candidate[turbine].y_m -= mesh;
            }
        }
        std::vector<LayoutEvaluation> evaluations(polls.size());
        std::vector<double> timings(polls.size(), 0.0);
        executor.parallel_for(0, static_cast<int>(polls.size()), [&](const int index) {
            const auto start = Clock::now();
            evaluations[index] = evaluate_layout_impl(
                polls[index], problem.impl_->scenarios
            );
            timings[index] = elapsed_seconds(start);
        });
        result.layout_evaluations += polls.size();
        for (std::size_t index = 0; index < polls.size(); ++index) {
            result.wind_scenario_turbine_evaluations
                += static_cast<std::uint64_t>(wind_samples)
                    * static_cast<std::uint64_t>(evaluations[index].turbine_count);
            result.layout_evaluator_seconds += timings[index];
        }
        int best = -1;
        for (int index = 0; index < static_cast<int>(evaluations.size()); ++index) {
            if (objective(evaluations[index]) > objective(continuous_evaluation)
                && (best < 0 || objective(evaluations[index])
                    > objective(evaluations[best]))) {
                best = index;
            }
        }
        if (best >= 0) {
            result.continuous_layout = std::move(polls[best]);
            continuous_evaluation = evaluations[best];
            mesh = std::min(50.0, mesh * 1.05);
        } else {
            mesh = std::max(1.0, mesh * 0.93);
        }
    }
    result.algorithm_seconds = elapsed_seconds(algorithm_start);
    result.cluster_assignment = kmeans(
        result.continuous_layout, result.cluster_count
    );

    RoleResult discrete_role;
    discrete_role.role = "table3_discrete_premaintenance";
    discrete_role.layout = discrete_evaluation;
    discrete_role.integrated_profit_usd = discrete_evaluation.pre_maintenance_profit_usd;
    result.roles.push_back(discrete_role);
    RoleResult continuous_role;
    continuous_role.role = "table3_continuous_premaintenance";
    continuous_role.layout = continuous_evaluation;
    continuous_role.integrated_profit_usd = continuous_evaluation.pre_maintenance_profit_usd;
    result.roles.push_back(continuous_role);

    const auto maintenance_start = Clock::now();
    const auto intervals = paper_inspection_intervals();
    const auto discrete_clusters = kmeans(result.discrete_layout, result.cluster_count);
    for (const auto& stage : {std::string("discrete"), std::string("continuous")}) {
        const auto& clusters = stage == "discrete" ? discrete_clusters
                                                    : result.cluster_assignment;
        const auto& evaluation = stage == "discrete" ? discrete_evaluation
                                                      : continuous_evaluation;
        for (const int interval : intervals) {
            const auto maintenance = evaluate_maintenance_impl(
                clusters, interval, config.maintenance_replications,
                mix_hash(config.seed, static_cast<std::uint64_t>(interval)), executor
            );
            result.component_life_events += maintenance.life_events;
            RoleResult role;
            role.role = "table5_" + stage + "_V" + std::to_string(interval);
            role.layout = evaluation;
            role.maintenance = maintenance.evaluation;
            role.integrated_profit_usd = evaluation.pre_maintenance_profit_usd
                - maintenance.evaluation.mean_cost_usd;
            result.roles.push_back(role);
        }
    }
    result.maintenance_simulation_seconds = elapsed_seconds(maintenance_start);
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.parallel_regions = receipt.parallel_regions;
    result.scientific_hash = hash_result(result);
    result.end_to_end_seconds = elapsed_seconds(total_start);
    return result;
}

std::string to_string(const ClusterProfile value) {
    return value == ClusterProfile::equation_text_four
        ? "equation_text_four" : "figure_caption_two";
}

std::vector<int> paper_inspection_intervals() {
    return {200, 250, 332, 350, 400};
}

}  // namespace core99::t73
