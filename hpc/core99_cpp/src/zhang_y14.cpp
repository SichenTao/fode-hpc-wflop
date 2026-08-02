/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y14 pure-C++ evaluator, p-SDRDE and CPU-HPC path
Paper/DOI: Zhang et al.; 10.1109/TSTE.2026.3661110
Public asset, missing information, paper conflict, reconstruction, semantic
IDs, controlling contract and claim boundary:
include/core99/zhang_y14.hpp
Semantic IDs: y14_psdrde_declared_reconstruction_v1;
y14_energy_noise_threefarm_declared_proxy_v1.
HPC analysis: evidence/development/Y14_H0_H4_mathematical_hpc_analysis_20260801.md
Claim boundary: flexible academic reconstruction, not author code, private
Gansu arrays, exact ISO site replay, random trajectory or numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/zhang_y14.hpp"

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
#include <utility>
#include <vector>

namespace core99::y14 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kRotorRadiusM = 56.5;
constexpr double kHubHeightM = 90.0;
constexpr double kReceiverHeightM = 1.5;
constexpr double kThrust = 0.74;
constexpr double kWakeRecovery = 0.075;
constexpr double kRatedPowerKw = 3000.0;
constexpr double kPowerA = 2959.0;
constexpr double kPowerB = 12.85;
constexpr double kPowerC = 4.934;
constexpr double kWeibullScale = 8.3;
constexpr double kWeibullShape = 2.0;
constexpr int kPowerTableSize = 8193;
constexpr double kSourceLevelDb = 105.0;
constexpr std::array<double, 8> kAWeight{
    -26.2, -16.1, -8.6, -3.2, 0.0, 1.2, 1.0, -1.1,
};
constexpr std::array<double, 8> kAtmosphericDbPerKm{
    0.1, 0.3, 1.0, 2.0, 4.0, 9.0, 20.0, 50.0,
};

struct Individual {
    std::vector<Point> layout;
    Evaluation evaluation;
    int rank = 0;
    double crowding = 0.0;
    double preference_distance = 0.0;
};

struct Trial {
    std::vector<Point> layout;
    Evaluation evaluation;
    int replacement_index = 0;
    bool feasible_before_evaluation = false;
    bool better = false;
};

double elapsed_seconds(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double squared_distance(const Point& left, const Point& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
}

double violation(const Evaluation& value) {
    return value.spacing_violation_m + value.boundary_violation_m;
}

bool pareto_dominates(const Evaluation& left, const Evaluation& right) {
    if (left.feasible != right.feasible) {
        return left.feasible;
    }
    if (!left.feasible) {
        return violation(left) < violation(right);
    }
    const bool no_worse =
        left.negative_aep_gwh <= right.negative_aep_gwh
        && left.spl_db <= right.spl_db;
    const bool strict =
        left.negative_aep_gwh < right.negative_aep_gwh
        || left.spl_db < right.spl_db;
    return no_worse && strict;
}

bool mutually_nondominated(const Evaluation& left, const Evaluation& right) {
    return !pareto_dominates(left, right) && !pareto_dominates(right, left);
}

void assign_crowding(
    std::vector<Individual>& population,
    const std::vector<int>& front
) {
    for (const int index : front) {
        population[static_cast<std::size_t>(index)].crowding = 0.0;
    }
    if (front.size() <= 2U) {
        for (const int index : front) {
            population[static_cast<std::size_t>(index)].crowding =
                std::numeric_limits<double>::infinity();
        }
        return;
    }
    for (int objective = 0; objective < 2; ++objective) {
        std::vector<int> order = front;
        const auto value = [&](const int index) {
            const auto& evaluation =
                population[static_cast<std::size_t>(index)].evaluation;
            return objective == 0
                ? evaluation.negative_aep_gwh : evaluation.spl_db;
        };
        std::stable_sort(
            order.begin(), order.end(), [&](const int left, const int right) {
                if (value(left) != value(right)) {
                    return value(left) < value(right);
                }
                return left < right;
            }
        );
        population[static_cast<std::size_t>(order.front())].crowding =
            std::numeric_limits<double>::infinity();
        population[static_cast<std::size_t>(order.back())].crowding =
            std::numeric_limits<double>::infinity();
        const double range = value(order.back()) - value(order.front());
        if (!(range > 0.0)) {
            continue;
        }
        for (std::size_t position = 1; position + 1 < order.size(); ++position) {
            auto& item = population[static_cast<std::size_t>(order[position])];
            if (std::isfinite(item.crowding)) {
                item.crowding +=
                    (value(order[position + 1])
                     - value(order[position - 1])) / range;
            }
        }
    }
}

void rank_and_crowding(
    std::vector<Individual>& population,
    const Scenario& scenario,
    double noise_weight,
    double delta,
    fode::PersistentExecutor& executor
) {
    const int count = static_cast<int>(population.size());
    double minimum_aep = std::numeric_limits<double>::infinity();
    double maximum_aep = -std::numeric_limits<double>::infinity();
    double minimum_spl = std::numeric_limits<double>::infinity();
    double maximum_spl = -std::numeric_limits<double>::infinity();
    for (const auto& item : population) {
        minimum_aep = std::min(minimum_aep, item.evaluation.negative_aep_gwh);
        maximum_aep = std::max(maximum_aep, item.evaluation.negative_aep_gwh);
        minimum_spl = std::min(minimum_spl, item.evaluation.spl_db);
        maximum_spl = std::max(maximum_spl, item.evaluation.spl_db);
    }
    const double range_aep = std::max(1.0e-12, maximum_aep - minimum_aep);
    const double range_spl = std::max(1.0e-12, maximum_spl - minimum_spl);
    const double energy_weight = 1.0 - noise_weight;
    double minimum_distance = std::numeric_limits<double>::infinity();
    double maximum_distance = 0.0;
    for (auto& item : population) {
        const double da =
            (item.evaluation.negative_aep_gwh
             - scenario.reference_negative_aep_gwh) / range_aep;
        const double ds =
            (item.evaluation.spl_db - scenario.reference_spl_db) / range_spl;
        item.preference_distance = std::sqrt(
            energy_weight * da * da + noise_weight * ds * ds
        );
        minimum_distance = std::min(minimum_distance, item.preference_distance);
        maximum_distance = std::max(maximum_distance, item.preference_distance);
    }
    const double distance_range =
        std::max(1.0e-12, maximum_distance - minimum_distance);
    std::vector<std::vector<int>> outgoing(
        static_cast<std::size_t>(count)
    );
    std::vector<int> incoming(static_cast<std::size_t>(count), 0);
    executor.parallel_for(0, count, [&](const int left) {
        auto& row = outgoing[static_cast<std::size_t>(left)];
        int degree = 0;
        for (int right = 0; right < count; ++right) {
            if (left == right) {
                continue;
            }
            const auto& l = population[static_cast<std::size_t>(left)];
            const auto& r = population[static_cast<std::size_t>(right)];
            const bool left_r_dominates =
                pareto_dominates(l.evaluation, r.evaluation)
                || (
                    mutually_nondominated(l.evaluation, r.evaluation)
                    && (l.preference_distance - r.preference_distance)
                        / distance_range <= -delta
                );
            const bool right_r_dominates =
                pareto_dominates(r.evaluation, l.evaluation)
                || (
                    mutually_nondominated(l.evaluation, r.evaluation)
                    && (r.preference_distance - l.preference_distance)
                        / distance_range <= -delta
                );
            if (left_r_dominates && !right_r_dominates) {
                row.push_back(right);
            } else if (right_r_dominates && !left_r_dominates) {
                ++degree;
            }
        }
        incoming[static_cast<std::size_t>(left)] = degree;
    });
    std::vector<bool> assigned(static_cast<std::size_t>(count), false);
    int assigned_count = 0;
    int rank = 1;
    while (assigned_count < count) {
        std::vector<int> front;
        for (int index = 0; index < count; ++index) {
            if (!assigned[static_cast<std::size_t>(index)]
                && incoming[static_cast<std::size_t>(index)] == 0) {
                front.push_back(index);
            }
        }
        if (front.empty()) {
            int minimum_incoming = std::numeric_limits<int>::max();
            for (int index = 0; index < count; ++index) {
                if (!assigned[static_cast<std::size_t>(index)]) {
                    minimum_incoming = std::min(
                        minimum_incoming,
                        incoming[static_cast<std::size_t>(index)]
                    );
                }
            }
            for (int index = 0; index < count; ++index) {
                if (!assigned[static_cast<std::size_t>(index)]
                    && incoming[static_cast<std::size_t>(index)]
                        == minimum_incoming) {
                    front.push_back(index);
                }
            }
        }
        std::sort(front.begin(), front.end());
        for (const int index : front) {
            assigned[static_cast<std::size_t>(index)] = true;
            ++assigned_count;
            population[static_cast<std::size_t>(index)].rank = rank;
        }
        assign_crowding(population, front);
        for (const int source : front) {
            for (const int target : outgoing[static_cast<std::size_t>(source)]) {
                if (!assigned[static_cast<std::size_t>(target)]) {
                    --incoming[static_cast<std::size_t>(target)];
                }
            }
        }
        ++rank;
    }
}

bool preferred(
    const Individual& left,
    const Individual& right,
    int left_index,
    int right_index
) {
    if (left.rank != right.rank) {
        return left.rank < right.rank;
    }
    if (left.crowding != right.crowding) {
        return left.crowding > right.crowding;
    }
    if (left.preference_distance != right.preference_distance) {
        return left.preference_distance < right.preference_distance;
    }
    return left_index < right_index;
}

Point clamp_point(const Point point, const Scenario& scenario) {
    return {
        std::clamp(point.x_m, kRotorRadiusM, scenario.length_m-kRotorRadiusM),
        std::clamp(point.y_m, kRotorRadiusM, scenario.width_m-kRotorRadiusM),
    };
}

bool layout_feasible(const std::vector<Point>& layout, const Scenario& scenario) {
    const double minimum_squared = 8.0 * kRotorRadiusM * kRotorRadiusM;
    for (const auto& point : layout) {
        if (point.x_m < kRotorRadiusM
            || point.x_m > scenario.length_m-kRotorRadiusM
            || point.y_m < kRotorRadiusM
            || point.y_m > scenario.width_m-kRotorRadiusM) {
            return false;
        }
    }
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1; right < layout.size(); ++right) {
            if (squared_distance(layout[left], layout[right]) < minimum_squared) {
                return false;
            }
        }
    }
    return true;
}

std::pair<int, int> first_spacing_conflict(const std::vector<Point>& layout) {
    const double minimum_squared = 8.0 * kRotorRadiusM * kRotorRadiusM;
    for (int left = 0; left < static_cast<int>(layout.size()); ++left) {
        for (int right = left + 1; right < static_cast<int>(layout.size()); ++right) {
            if (squared_distance(
                    layout[static_cast<std::size_t>(left)],
                    layout[static_cast<std::size_t>(right)]
                ) < minimum_squared) {
                return {left, right};
            }
        }
    }
    return {-1, -1};
}

std::vector<Point> initial_layout(
    const Problem& problem,
    const fode::CounterRng& rng,
    int individual
) {
    const auto& scenario = problem.scenario();
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(scenario.turbine_count));
    const double minimum_squared = problem.minimum_spacing_m()
        * problem.minimum_spacing_m();
    for (int turbine = 0; turbine < scenario.turbine_count; ++turbine) {
        bool accepted = false;
        for (int attempt = 0; attempt < 20000; ++attempt) {
            Point candidate{
                kRotorRadiusM
                    + (scenario.length_m - 2.0*kRotorRadiusM)
                        * rng.uniform(0, 10, individual, turbine, 2*attempt),
                kRotorRadiusM
                    + (scenario.width_m - 2.0*kRotorRadiusM)
                        * rng.uniform(0, 10, individual, turbine, 2*attempt+1),
            };
            accepted = std::all_of(
                layout.begin(), layout.end(), [&](const Point& previous) {
                    return squared_distance(candidate, previous)
                        >= minimum_squared;
                }
            );
            if (accepted) {
                layout.push_back(candidate);
                break;
            }
        }
        if (!accepted) {
            return problem.reference_layout();
        }
    }
    return layout;
}

int draw_distinct(
    const fode::CounterRng& rng,
    int low,
    int high,
    const std::vector<int>& excluded,
    std::uint64_t generation,
    std::uint64_t phase,
    std::uint64_t individual,
    std::uint64_t coordinate
) {
    for (std::uint64_t draw = 0; draw < 1000; ++draw) {
        const int value = rng.integer(
            low, high, generation, phase, individual, coordinate, draw
        );
        if (std::find(excluded.begin(), excluded.end(), value)
            == excluded.end()) {
            return value;
        }
    }
    for (int value = low; value < high; ++value) {
        if (std::find(excluded.begin(), excluded.end(), value)
            == excluded.end()) {
            return value;
        }
    }
    throw std::runtime_error("Y14 could not draw a distinct index");
}

double scaling_factor(
    const fode::CounterRng& rng,
    std::uint64_t generation,
    std::uint64_t individual,
    std::uint64_t draw
) {
    double value = 0.0;
    if (rng.uniform(generation, 30, individual, draw, 0) <= 0.5) {
        value = 0.5 + 0.5 * rng.normal(generation, 31, individual, draw, 0);
    } else {
        const double uniform = std::clamp(
            rng.uniform(generation, 32, individual, draw, 0),
            1.0e-12, 1.0-1.0e-12
        );
        value = std::tan(std::numbers::pi * (uniform - 0.5));
    }
    return std::clamp(value, 0.05, 1.5);
}

int select_replacement_index(
    const fode::CounterRng& rng,
    std::uint64_t generation,
    int child,
    const std::vector<int>& successes,
    const std::vector<int>& failures,
    int learning_period
) {
    const int count = static_cast<int>(successes.size());
    if (static_cast<int>(generation) < learning_period) {
        return rng.integer(0, count, generation, 40, child, 0, 0);
    }
    std::vector<double> weights(static_cast<std::size_t>(count), 0.0);
    double total = 0.0;
    for (int index = 0; index < count; ++index) {
        weights[static_cast<std::size_t>(index)] =
            (static_cast<double>(successes[static_cast<std::size_t>(index)])+1.0)
            / (static_cast<double>(
                successes[static_cast<std::size_t>(index)]
                + failures[static_cast<std::size_t>(index)]) + 2.0);
        total += weights[static_cast<std::size_t>(index)];
    }
    const double target = rng.uniform(generation, 41, child, 0, 0) * total;
    double cumulative = 0.0;
    for (int index = 0; index < count; ++index) {
        cumulative += weights[static_cast<std::size_t>(index)];
        if (target <= cumulative) {
            return index;
        }
    }
    return count - 1;
}

std::uint64_t mix_hash(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

}  // namespace

std::vector<Scenario> paper_scenarios() {
    return {
        {"Y14_n16_original",16,2500.0,4000.0,-138.229,45.0,false,0.5},
        {"Y14_n24_original",24,2500.0,4000.0,-207.350,50.0,false,0.5},
        {"Y14_n48_original",48,5000.0,4000.0,-390.158,50.0,false,0.5},
        {"Y14_n16_adjusted",16,2500.0,4000.0,-144.726,43.380,true,0.7},
        {"Y14_n24_adjusted",24,2500.0,4000.0,-212.741,47.500,true,0.7},
        {"Y14_n48_adjusted",48,5000.0,4000.0,-403.033,47.700,true,0.7},
    };
}

Problem::Problem(Scenario scenario) : scenario_(std::move(scenario)) {
    const auto roles = paper_scenarios();
    const bool known = std::any_of(
        roles.begin(), roles.end(), [&](const Scenario& role) {
            return role.case_id == scenario_.case_id;
        }
    );
    if (!known) {
        throw std::invalid_argument("unknown Y14 paper scenario");
    }
    wind_probabilities_ = {
        0.020,0.025,0.030,0.050,0.100,0.070,0.040,0.030,
        0.020,0.020,0.030,0.040,0.160,0.190,0.110,0.060,
    };
    const double total = std::accumulate(
        wind_probabilities_.begin(), wind_probabilities_.end(), 0.0
    );
    for (double& probability : wind_probabilities_) {
        probability /= total;
    }
    build_receivers();
    build_tables();
}

const Scenario& Problem::scenario() const noexcept { return scenario_; }
int Problem::receiver_count() const noexcept {
    return static_cast<int>(receivers_.size());
}
double Problem::rotor_radius_m() const noexcept { return kRotorRadiusM; }
double Problem::minimum_spacing_m() const noexcept {
    return std::sqrt(8.0) * kRotorRadiusM;
}
const std::vector<double>& Problem::wind_probabilities() const noexcept {
    return wind_probabilities_;
}

void Problem::build_receivers() {
    const int horizontal_segments =
        static_cast<int>(std::ceil(scenario_.length_m / 200.0));
    const int vertical_segments =
        static_cast<int>(std::ceil(scenario_.width_m / 200.0));
    for (int index = 0; index <= horizontal_segments; ++index) {
        const double x = scenario_.length_m
            * static_cast<double>(index) / horizontal_segments;
        receivers_.push_back({x, 0.0});
        receivers_.push_back({x, scenario_.width_m});
    }
    for (int index = 1; index < vertical_segments; ++index) {
        const double y = scenario_.width_m
            * static_cast<double>(index) / vertical_segments;
        receivers_.push_back({0.0, y});
        receivers_.push_back({scenario_.length_m, y});
    }
    receivers_.push_back({0.5*scenario_.length_m,0.5*scenario_.width_m});
}

void Problem::build_tables() {
    expected_power_table_.resize(kPowerTableSize);
    constexpr double step = 0.025;
    constexpr int bins = 1400;
    for (int index = 0; index < kPowerTableSize; ++index) {
        const double deficit = 0.999
            * static_cast<double>(index) / (kPowerTableSize - 1);
        const double scale = kWeibullScale * (1.0-deficit);
        double expectation = 0.0;
        for (int bin = 0; bin < bins; ++bin) {
            const double speed = (static_cast<double>(bin)+0.5)*step;
            const double ratio = speed / scale;
            const double density = kWeibullShape / scale
                * std::pow(ratio,kWeibullShape-1.0)
                * std::exp(-std::pow(ratio,kWeibullShape));
            double power = 0.0;
            if (speed >= 3.0 && speed < 11.5) {
                power = kPowerA * std::exp(
                    -std::pow((speed-kPowerB)/kPowerC,2.0)
                );
            } else if (speed >= 11.5 && speed < 25.0) {
                power = kRatedPowerKw;
            }
            expectation += power * density * step;
        }
        expected_power_table_[static_cast<std::size_t>(index)] = expectation;
    }
    const int maximum_distance = static_cast<int>(
        std::ceil(std::hypot(scenario_.length_m,scenario_.width_m)+200.0)
    );
    source_noise_table_.resize(static_cast<std::size_t>(maximum_distance+2));
    for (int index = 0; index <= maximum_distance+1; ++index) {
        const double distance = std::max(1.0,static_cast<double>(index));
        const double divergence = 20.0*std::log10(distance)+11.0;
        double intensity = 0.0;
        for (std::size_t band = 0; band < kAWeight.size(); ++band) {
            const double atmospheric =
                kAtmosphericDbPerKm[band]*distance/1000.0;
            const double level = kSourceLevelDb-divergence-atmospheric
                + kAWeight[band];
            intensity += std::pow(10.0,0.1*level);
        }
        source_noise_table_[static_cast<std::size_t>(index)] = intensity;
    }
}

double Problem::expected_power_kw(double deficit) const {
    const double position = std::clamp(deficit,0.0,0.999)
        / 0.999 * static_cast<double>(kPowerTableSize-1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower+1,expected_power_table_.size()-1);
    const double fraction = position-static_cast<double>(lower);
    return expected_power_table_[lower]
        + fraction*(expected_power_table_[upper]-expected_power_table_[lower]);
}

double Problem::source_noise_intensity(double distance_m) const {
    const double position = std::clamp(
        distance_m,1.0,static_cast<double>(source_noise_table_.size()-1)
    );
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min(lower+1,source_noise_table_.size()-1);
    const double fraction = position-static_cast<double>(lower);
    return source_noise_table_[lower]
        + fraction*(source_noise_table_[upper]-source_noise_table_[lower]);
}

std::vector<Point> Problem::reference_layout() const {
    const int columns = static_cast<int>(std::ceil(std::sqrt(
        static_cast<double>(scenario_.turbine_count)
        * scenario_.length_m / scenario_.width_m
    )));
    const int rows = static_cast<int>(std::ceil(
        static_cast<double>(scenario_.turbine_count) / columns
    ));
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(scenario_.turbine_count));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (static_cast<int>(layout.size()) == scenario_.turbine_count) {
                return layout;
            }
            const double usable_x = scenario_.length_m-2.0*kRotorRadiusM;
            const double usable_y = scenario_.width_m-2.0*kRotorRadiusM;
            double x = kRotorRadiusM
                + usable_x*(static_cast<double>(column)+0.5)/columns;
            if (row%2 != 0) {
                x += 0.2*usable_x/columns;
            }
            layout.push_back(clamp_point({
                x,
                kRotorRadiusM
                    + usable_y*(static_cast<double>(row)+0.5)/rows,
            },scenario_));
        }
    }
    return layout;
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (static_cast<int>(layout.size()) != scenario_.turbine_count) {
        throw std::invalid_argument("Y14 layout turbine count mismatch");
    }
    Evaluation result;
    for (const auto& point : layout) {
        result.boundary_violation_m +=
            std::max(0.0,kRotorRadiusM-point.x_m)
            + std::max(0.0,point.x_m-(scenario_.length_m-kRotorRadiusM))
            + std::max(0.0,kRotorRadiusM-point.y_m)
            + std::max(0.0,point.y_m-(scenario_.width_m-kRotorRadiusM));
    }
    const double minimum_spacing = minimum_spacing_m();
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left+1; right < layout.size(); ++right) {
            result.spacing_violation_m += std::max(
                0.0,
                minimum_spacing-std::sqrt(squared_distance(layout[left],layout[right]))
            );
        }
    }
    result.feasible = result.boundary_violation_m <= 1.0e-10
        && result.spacing_violation_m <= 1.0e-10;
    double expected_farm_power_kw = 0.0;
    std::vector<double> deficit_square(layout.size(),0.0);
    for (std::size_t direction = 0; direction < wind_probabilities_.size(); ++direction) {
        std::fill(deficit_square.begin(),deficit_square.end(),0.0);
        const double angle = 2.0*std::numbers::pi
            * static_cast<double>(direction)/wind_probabilities_.size();
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        for (std::size_t target = 0; target < layout.size(); ++target) {
            for (std::size_t source = 0; source < layout.size(); ++source) {
                if (source == target) continue;
                const double dx = layout[target].x_m-layout[source].x_m;
                const double dy = layout[target].y_m-layout[source].y_m;
                const double downstream = dx*cosine+dy*sine;
                if (!(downstream > 0.0)) continue;
                const double crosswind = std::abs(-dx*sine+dy*cosine);
                const double wake_radius = kRotorRadiusM+kWakeRecovery*downstream;
                if (crosswind > wake_radius) continue;
                const double deficit = (1.0-std::sqrt(1.0-kThrust))
                    / std::pow(1.0+kWakeRecovery*downstream/kRotorRadiusM,2.0);
                deficit_square[target] += deficit*deficit;
            }
        }
        double direction_power = 0.0;
        for (const double square : deficit_square) {
            direction_power += expected_power_kw(std::sqrt(square));
        }
        expected_farm_power_kw += wind_probabilities_[direction]*direction_power;
    }
    result.negative_aep_gwh = -8760.0*expected_farm_power_kw/1.0e6;
    double maximum_intensity = 0.0;
    const double vertical = kHubHeightM-kReceiverHeightM;
    for (const Point& receiver : receivers_) {
        double intensity = 0.0;
        for (const Point& turbine : layout) {
            const double horizontal = std::sqrt(squared_distance(receiver,turbine));
            intensity += source_noise_intensity(std::hypot(horizontal,vertical));
        }
        maximum_intensity = std::max(maximum_intensity,intensity);
    }
    result.spl_db = 10.0*std::log10(std::max(maximum_intensity,1.0e-300));
    return result;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<Point>>& layouts,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(layouts.size());
    executor.parallel_for(0,static_cast<int>(layouts.size()),[&](const int index) {
        result[static_cast<std::size_t>(index)] =
            evaluate(layouts[static_cast<std::size_t>(index)]);
    });
    return result;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0 || config.population != 50
        || config.subpopulation != 10
        || config.maximum_evaluation_slots
            < static_cast<std::uint64_t>(config.population)
        || config.maximum_evaluation_slots
            % static_cast<std::uint64_t>(config.population) != 0
        || !(config.crossover_rate > 0.0 && config.crossover_rate <= 1.0)
        || config.learning_period <= 0) {
        throw std::invalid_argument("invalid Y14 p-SDRDE configuration");
    }
    const auto started = Clock::now();
    const auto& scenario = problem.scenario();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    std::vector<Individual> population(static_cast<std::size_t>(config.population));
    executor.parallel_for(0,config.population,[&](const int index) {
        population[static_cast<std::size_t>(index)].layout =
            initial_layout(problem,rng,index);
    });
    std::vector<std::vector<Point>> initial;
    initial.reserve(population.size());
    for (const auto& individual : population) initial.push_back(individual.layout);
    auto eval_started = Clock::now();
    const auto initial_evaluations = problem.evaluate_population(initial,executor);
    double evaluator_seconds = elapsed_seconds(eval_started);
    for (std::size_t index = 0; index < population.size(); ++index) {
        population[index].evaluation = initial_evaluations[index];
    }
    std::uint64_t physical_fes = population.size();
    std::uint64_t slots = population.size();
    int generation = 0;
    const int turbines = scenario.turbine_count;
    std::vector<int> successes(static_cast<std::size_t>(turbines),0);
    std::vector<int> failures(static_cast<std::size_t>(turbines),0);
    std::vector<std::vector<int>> success_history;
    std::vector<std::vector<int>> failure_history;
    while (slots < config.maximum_evaluation_slots) {
        const double progress = static_cast<double>(slots)
            / static_cast<double>(config.maximum_evaluation_slots);
        const double delta = 1.0-0.9*progress;
        const double noise_weight = scenario.adjusted_preference
            ? 0.5+0.2*progress : 0.5;
        rank_and_crowding(
            population,scenario,noise_weight,delta,executor
        );
        std::vector<int> permutation(static_cast<std::size_t>(config.population));
        std::iota(permutation.begin(),permutation.end(),0);
        std::stable_sort(
            permutation.begin(),permutation.end(),[&](const int left,const int right) {
                const double lk = rng.uniform(generation,20,left,0,0);
                const double rk = rng.uniform(generation,20,right,0,0);
                return lk != rk ? lk < rk : left < right;
            }
        );
        std::vector<int> position(static_cast<std::size_t>(config.population));
        for (int index = 0; index < config.population; ++index) {
            position[static_cast<std::size_t>(
                permutation[static_cast<std::size_t>(index)]
            )] = index;
        }
        std::vector<Trial> trials(static_cast<std::size_t>(config.population));
        executor.parallel_for(0,config.population,[&](const int child) {
            Trial trial;
            const int group = position[static_cast<std::size_t>(child)]
                / config.subpopulation;
            const int group_begin = group*config.subpopulation;
            const int group_end = group_begin+config.subpopulation;
            int best = permutation[static_cast<std::size_t>(group_begin)];
            for (int local = group_begin+1; local < group_end; ++local) {
                const int candidate = permutation[static_cast<std::size_t>(local)];
                if (preferred(
                    population[static_cast<std::size_t>(candidate)],
                    population[static_cast<std::size_t>(best)],candidate,best
                )) best = candidate;
            }
            const int r1_local = draw_distinct(
                rng,group_begin,group_end,{},generation,21,child,0
            );
            const int r2_local = draw_distinct(
                rng,group_begin,group_end,{r1_local},generation,21,child,1
            );
            const int r1 = permutation[static_cast<std::size_t>(r1_local)];
            const int r2 = permutation[static_cast<std::size_t>(r2_local)];
            const double scale = scaling_factor(rng,generation,child,0);
            std::vector<Point> mutant(static_cast<std::size_t>(turbines));
            for (int turbine = 0; turbine < turbines; ++turbine) {
                const Point& base = population[static_cast<std::size_t>(best)]
                    .layout[static_cast<std::size_t>(turbine)];
                const Point& one = population[static_cast<std::size_t>(r1)]
                    .layout[static_cast<std::size_t>(turbine)];
                const Point& two = population[static_cast<std::size_t>(r2)]
                    .layout[static_cast<std::size_t>(turbine)];
                mutant[static_cast<std::size_t>(turbine)] = clamp_point({
                    base.x_m+scale*(one.x_m-two.x_m),
                    base.y_m+scale*(one.y_m-two.y_m),
                },scenario);
            }
            std::vector<Point> candidates(static_cast<std::size_t>(turbines));
            for (int turbine = 0; turbine < turbines; ++turbine) {
                const int a = draw_distinct(
                    rng,0,turbines,{turbine},generation,22,child,4*turbine
                );
                const int b = draw_distinct(
                    rng,0,turbines,{turbine,a},generation,22,child,4*turbine+1
                );
                const int c = draw_distinct(
                    rng,0,turbines,{turbine,a,b},generation,22,child,4*turbine+2
                );
                const double local_scale = scaling_factor(
                    rng,generation,child,static_cast<std::uint64_t>(turbine+1)
                );
                Point value{
                    mutant[static_cast<std::size_t>(a)].x_m
                        + local_scale*(
                            mutant[static_cast<std::size_t>(b)].x_m
                            - mutant[static_cast<std::size_t>(c)].x_m),
                    mutant[static_cast<std::size_t>(a)].y_m
                        + local_scale*(
                            mutant[static_cast<std::size_t>(b)].y_m
                            - mutant[static_cast<std::size_t>(c)].y_m),
                };
                const int forced = rng.integer(
                    0,2,generation,23,child,turbine,0
                );
                if (forced != 0 && rng.uniform(generation,23,child,turbine,1)
                    > config.crossover_rate) {
                    value.x_m = mutant[static_cast<std::size_t>(turbine)].x_m;
                }
                if (forced != 1 && rng.uniform(generation,23,child,turbine,2)
                    > config.crossover_rate) {
                    value.y_m = mutant[static_cast<std::size_t>(turbine)].y_m;
                }
                candidates[static_cast<std::size_t>(turbine)] =
                    clamp_point(value,scenario);
            }
            trial.layout = population[static_cast<std::size_t>(child)].layout;
            trial.replacement_index = select_replacement_index(
                rng,generation,child,successes,failures,config.learning_period
            );
            int replaced = trial.replacement_index;
            trial.layout[static_cast<std::size_t>(replaced)] = candidates.front();
            const int replacement_limit = turbines/2;
            for (int replacement = 1; replacement < replacement_limit; ++replacement) {
                const auto conflict = first_spacing_conflict(trial.layout);
                if (conflict.first < 0) break;
                replaced = conflict.first == replaced
                    ? conflict.second : conflict.first;
                trial.layout[static_cast<std::size_t>(replaced)] =
                    candidates[static_cast<std::size_t>(replacement%turbines)];
            }
            trial.feasible_before_evaluation =
                layout_feasible(trial.layout,scenario);
            if (!trial.feasible_before_evaluation) {
                trial.layout = population[static_cast<std::size_t>(child)].layout;
                trial.evaluation =
                    population[static_cast<std::size_t>(child)].evaluation;
            }
            trials[static_cast<std::size_t>(child)] = std::move(trial);
        });
        eval_started = Clock::now();
        executor.parallel_for(0,config.population,[&](const int child) {
            auto& trial = trials[static_cast<std::size_t>(child)];
            if (trial.feasible_before_evaluation) {
                trial.evaluation = problem.evaluate(trial.layout);
            }
        });
        evaluator_seconds += elapsed_seconds(eval_started);
        std::vector<int> generation_success(static_cast<std::size_t>(turbines),0);
        std::vector<int> generation_failure(static_cast<std::size_t>(turbines),0);
        std::vector<Individual> merged = population;
        merged.reserve(static_cast<std::size_t>(2*config.population));
        for (int child = 0; child < config.population; ++child) {
            auto& trial = trials[static_cast<std::size_t>(child)];
            if (trial.feasible_before_evaluation) {
                ++physical_fes;
                const auto& parent = population[static_cast<std::size_t>(child)];
                trial.better = pareto_dominates(trial.evaluation,parent.evaluation)
                    || mutually_nondominated(trial.evaluation,parent.evaluation);
            }
            auto& counter = trial.feasible_before_evaluation && trial.better
                ? generation_success : generation_failure;
            ++counter[static_cast<std::size_t>(trial.replacement_index)];
            merged.push_back({trial.layout,trial.evaluation,0,0.0,0.0});
        }
        success_history.push_back(generation_success);
        failure_history.push_back(generation_failure);
        for (int turbine = 0; turbine < turbines; ++turbine) {
            successes[static_cast<std::size_t>(turbine)] +=
                generation_success[static_cast<std::size_t>(turbine)];
            failures[static_cast<std::size_t>(turbine)] +=
                generation_failure[static_cast<std::size_t>(turbine)];
        }
        if (static_cast<int>(success_history.size()) > config.learning_period) {
            for (int turbine = 0; turbine < turbines; ++turbine) {
                successes[static_cast<std::size_t>(turbine)] -=
                    success_history.front()[static_cast<std::size_t>(turbine)];
                failures[static_cast<std::size_t>(turbine)] -=
                    failure_history.front()[static_cast<std::size_t>(turbine)];
            }
            success_history.erase(success_history.begin());
            failure_history.erase(failure_history.begin());
        }
        rank_and_crowding(merged,scenario,noise_weight,delta,executor);
        std::vector<int> order(merged.size());
        std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),[&](const int left,const int right) {
            return preferred(
                merged[static_cast<std::size_t>(left)],
                merged[static_cast<std::size_t>(right)],left,right
            );
        });
        for (int index = 0; index < config.population; ++index) {
            population[static_cast<std::size_t>(index)] =
                std::move(merged[static_cast<std::size_t>(
                    order[static_cast<std::size_t>(index)]
                )]);
        }
        slots += static_cast<std::uint64_t>(config.population);
        ++generation;
    }
    const double final_progress = 1.0;
    const double final_noise_weight = scenario.adjusted_preference
        ? scenario.final_noise_weight : 0.5;
    rank_and_crowding(
        population,scenario,final_noise_weight,1.0-0.9*final_progress,executor
    );
    std::vector<FrontPoint> front;
    for (const auto& individual : population) {
        if (individual.rank == 1) {
            front.push_back({individual.evaluation,individual.layout});
        }
    }
    std::stable_sort(front.begin(),front.end(),[](const auto& left,const auto& right) {
        if (left.evaluation.negative_aep_gwh
            != right.evaluation.negative_aep_gwh) {
            return left.evaluation.negative_aep_gwh
                < right.evaluation.negative_aep_gwh;
        }
        return left.evaluation.spl_db < right.evaluation.spl_db;
    });
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto& point : front) {
        hash = mix_hash(hash,std::bit_cast<std::uint64_t>(
            point.evaluation.negative_aep_gwh
        ));
        hash = mix_hash(hash,std::bit_cast<std::uint64_t>(point.evaluation.spl_db));
        for (const auto& coordinate : point.layout) {
            hash = mix_hash(hash,std::bit_cast<std::uint64_t>(coordinate.x_m));
            hash = mix_hash(hash,std::bit_cast<std::uint64_t>(coordinate.y_m));
        }
    }
    const auto receipt = executor.work_receipt();
    RunResult result;
    result.case_id = scenario.case_id;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.population = config.population;
    result.generations = generation;
    result.nominal_evaluation_slots = slots;
    result.physical_fes = physical_fes;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(started);
    result.algorithm_seconds = std::max(0.0,result.end_to_end_seconds-evaluator_seconds);
    result.scientific_hash = hash;
    result.front = std::move(front);
    return result;
}

}  // namespace core99::y14
