/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T87 pure-C++ proxy evaluator, IGA-PSO transitions,
unique-layout evaluation, full-core persistent execution and receipts
Paper/DOI/source/missing/conflict/reconstruction/semantic IDs/backend/claim:
hpc/core99_cpp/include/core99/hu_t87.hpp
Controlling contract: shared/contracts/core99_t87_hu_iga_pso_2024.json
Independent validator: scripts/validate_core99_t87.py
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/hu_t87.hpp"

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
#include <unordered_map>
#include <utility>
#include <vector>

namespace core99::t87 {
namespace {

using Clock = std::chrono::steady_clock;
using Genome = std::vector<std::uint64_t>;

constexpr double rotor_diameter_m = 156.0;
constexpr double minimum_spacing_d = 4.0;
constexpr double hub_height_m = 100.0;
constexpr double roughness_length_m = 0.037;
constexpr double wake_growth =
    0.5 / std::log(hub_height_m / roughness_length_m);
constexpr double rated_power_mw = 3.3;
constexpr double crossover_probability = 0.9;
constexpr double initial_mutation_probability = 0.1;
constexpr double final_mutation_probability = 0.2;
constexpr double initial_inertia = 0.4;
constexpr double final_inertia = 0.9;
constexpr double pso_cognitive = 2.0;
constexpr double pso_social = 2.0;
constexpr double pso_neighborhood_d = 1.0;
constexpr double dgwm_rmin_d = 0.275;
constexpr double nav_net_value_rmb_per_mwh = 380.0;
constexpr double nav_annualized_cost_rmb_per_turbine = 2.0e6;

std::size_t deficit_index(
    const int state,
    const int candidate_count,
    const int source,
    const int target
) {
    return static_cast<std::size_t>(
        (state * candidate_count + source) * candidate_count + target
    );
}

template <class T>
T read_binary(std::ifstream& stream) {
    T value{};
    stream.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!stream) throw std::runtime_error("truncated T87 proxy fixture");
    return value;
}

double circle_overlap_area(
    const double first_radius,
    const double second_radius,
    const double distance
) {
    if (distance >= first_radius + second_radius) return 0.0;
    if (distance <= std::abs(first_radius - second_radius)) {
        const double radius = std::min(first_radius, second_radius);
        return std::numbers::pi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (
            distance * distance + first_radius * first_radius
            - second_radius * second_radius
        ) / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (
            distance * distance + second_radius * second_radius
            - first_radius * first_radius
        ) / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double radical = std::max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius)
    );
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle
        - 0.5 * std::sqrt(radical);
}

bool better_evaluation(
    const Evaluation& left,
    const Evaluation& right
) {
    if (left.feasible != right.feasible) return left.feasible;
    if (!left.feasible) {
        if (
            left.total_normalized_constraint_violation
            != right.total_normalized_constraint_violation
        ) {
            return left.total_normalized_constraint_violation
                < right.total_normalized_constraint_violation;
        }
    }
    if (left.fitness != right.fitness) return left.fitness > right.fitness;
    if (left.turbine_count != right.turbine_count) {
        return left.turbine_count < right.turbine_count;
    }
    return false;
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

struct GenomeHash {
    std::size_t operator()(const Genome& genome) const noexcept {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (const auto word : genome) hash = mix_hash(hash, word);
        return static_cast<std::size_t>(hash);
    }
};

std::vector<int> decode_genome(
    const Genome& genome,
    const int candidate_count
) {
    std::vector<int> indices;
    for (int index = 0; index < candidate_count; ++index) {
        if (
            (genome[static_cast<std::size_t>(index / 64)]
                >> static_cast<unsigned>(index % 64)) & 1ULL
        ) {
            indices.push_back(index);
        }
    }
    return indices;
}

void mask_unused_bits(Genome& genome, const int candidate_count) {
    const int used = candidate_count % 64;
    if (used != 0) {
        genome.back() &= (1ULL << static_cast<unsigned>(used)) - 1ULL;
    }
}

struct BatchResult {
    std::vector<Evaluation> evaluations;
    std::uint64_t unique_count = 0;
    double seconds = 0.0;
};

BatchResult evaluate_unique_genomes(
    const Problem& problem,
    const std::vector<Genome>& population,
    fode::PersistentExecutor& executor
) {
    const auto begin = Clock::now();
    std::unordered_map<Genome, std::size_t, GenomeHash> lookup;
    lookup.reserve(population.size() * 2U);
    std::vector<Genome> unique;
    std::vector<std::size_t> mapping(population.size());
    for (std::size_t index = 0; index < population.size(); ++index) {
        const auto [iterator, inserted] = lookup.emplace(
            population[index], unique.size()
        );
        if (inserted) unique.push_back(population[index]);
        mapping[index] = iterator->second;
    }
    std::vector<Evaluation> unique_evaluations(unique.size());
    executor.parallel_for(0, static_cast<int>(unique.size()), [&](const int i) {
        unique_evaluations[static_cast<std::size_t>(i)] =
            problem.evaluate_candidate_indices(
                decode_genome(
                    unique[static_cast<std::size_t>(i)],
                    static_cast<int>(problem.candidates().size())
                )
            );
    });
    BatchResult result;
    result.evaluations.resize(population.size());
    for (std::size_t index = 0; index < population.size(); ++index) {
        result.evaluations[index] = unique_evaluations[mapping[index]];
    }
    result.unique_count = unique.size();
    result.seconds = std::chrono::duration<double>(
        Clock::now() - begin
    ).count();
    return result;
}

std::size_t best_index(
    const std::vector<Evaluation>& evaluations
) {
    std::size_t best = 0;
    for (std::size_t index = 1; index < evaluations.size(); ++index) {
        if (better_evaluation(evaluations[index], evaluations[best])) {
            best = index;
        }
    }
    return best;
}

std::vector<std::size_t> fitness_rank(
    const std::vector<Evaluation>& evaluations
) {
    std::vector<std::size_t> order(evaluations.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(
        order.begin(),
        order.end(),
        [&](const std::size_t left, const std::size_t right) {
            if (better_evaluation(evaluations[left], evaluations[right])) {
                return true;
            }
            if (better_evaluation(evaluations[right], evaluations[left])) {
                return false;
            }
            return left < right;
        }
    );
    return order;
}

std::size_t roulette_rank_select(
    const std::vector<std::size_t>& order,
    const double draw
) {
    const double count = static_cast<double>(order.size());
    const double total = count * (count + 1.0) / 2.0;
    const double target = draw * total;
    double cumulative = 0.0;
    for (std::size_t rank = 0; rank < order.size(); ++rank) {
        cumulative += static_cast<double>(order.size() - rank);
        if (target <= cumulative) return order[rank];
    }
    return order.back();
}

std::vector<double> coordinates_from_candidates(
    const Problem& problem,
    const std::vector<int>& indices
) {
    std::vector<double> coordinates;
    coordinates.reserve(2U * indices.size());
    for (const int index : indices) {
        const Candidate& candidate =
            problem.candidates()[static_cast<std::size_t>(index)];
        coordinates.push_back(candidate.x_d);
        coordinates.push_back(candidate.y_d);
    }
    return coordinates;
}

std::vector<Evaluation> evaluate_particles(
    const Problem& problem,
    const std::vector<std::vector<double>>& particles,
    fode::PersistentExecutor& executor,
    double& seconds
) {
    const auto begin = Clock::now();
    std::vector<Evaluation> evaluations(particles.size());
    executor.parallel_for(
        0, static_cast<int>(particles.size()), [&](const int index) {
            evaluations[static_cast<std::size_t>(index)] =
                problem.evaluate_coordinates_d(
                    particles[static_cast<std::size_t>(index)]
                );
        }
    );
    seconds += std::chrono::duration<double>(Clock::now() - begin).count();
    return evaluations;
}

}  // namespace

std::string wake_model_name(const WakeModel model) {
    if (model == WakeModel::jensen) return "jensen";
    if (model == WakeModel::gaussian) return "gaussian";
    return "double_gaussian";
}

std::string objective_model_name(const ObjectiveModel model) {
    return model == ObjectiveModel::aep ? "aep" : "nav";
}

Problem::Problem(
    std::string case_id,
    const std::string& proxy_path,
    const int precomputation_workers
) : case_id_(std::move(case_id)),
    precomputation_workers_(std::max(1, precomputation_workers)) {
    const auto begin = Clock::now();
    configure_case();
    load_proxy(proxy_path);
    calibrate_candidate_speed_multipliers();
    precompute_candidate_deficits();
    precomputation_seconds_ = std::chrono::duration<double>(
        Clock::now() - begin
    ).count();
}

const std::string& Problem::case_id() const noexcept {
    return case_id_;
}

const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

WakeModel Problem::wake_model() const noexcept {
    return wake_model_;
}

ObjectiveModel Problem::objective_model() const noexcept {
    return objective_model_;
}

const std::vector<Candidate>& Problem::candidates() const noexcept {
    return candidates_;
}

const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}

int Problem::turbine_curve_point_count() const noexcept {
    return static_cast<int>(turbine_curve_.size());
}

double Problem::wind_probability_sum() const noexcept {
    return std::accumulate(
        wind_states_.begin(),
        wind_states_.end(),
        0.0,
        [](const double sum, const WindState& state) {
            return sum + state.probability;
        }
    );
}

double Problem::precomputation_seconds() const noexcept {
    return precomputation_seconds_;
}

void Problem::configure_case() {
    if (case_id_ == "t87_case1_jensen_aep") {
        wake_model_ = WakeModel::jensen;
        objective_model_ = ObjectiveModel::aep;
    } else if (case_id_ == "t87_case2_gwm_aep") {
        wake_model_ = WakeModel::gaussian;
        objective_model_ = ObjectiveModel::aep;
    } else if (case_id_ == "t87_case3_dgwm_aep") {
        wake_model_ = WakeModel::double_gaussian;
        objective_model_ = ObjectiveModel::aep;
    } else if (case_id_ == "t87_case4_jensen_nav") {
        wake_model_ = WakeModel::jensen;
        objective_model_ = ObjectiveModel::nav;
    } else {
        throw std::invalid_argument("unknown T87 case ID: " + case_id_);
    }
}

void Problem::load_proxy(const std::string& proxy_path) {
    std::ifstream stream(proxy_path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open T87 proxy fixture");
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    const std::array<char, 8> expected{
        'T', '8', '7', 'P', 'X', 'Y', '2', '\0'
    };
    if (magic != expected) {
        throw std::runtime_error("T87 proxy fixture magic mismatch");
    }
    const auto candidate_count = read_binary<std::uint32_t>(stream);
    const auto state_count = read_binary<std::uint32_t>(stream);
    const auto curve_count = read_binary<std::uint32_t>(stream);
    if (
        candidate_count != 522U || state_count != 49U
        || curve_count != 38U
    ) {
        throw std::runtime_error("T87 proxy fixture dimension mismatch");
    }
    candidates_.reserve(candidate_count);
    for (std::uint32_t index = 0; index < candidate_count; ++index) {
        Candidate candidate;
        candidate.x_d = read_binary<float>(stream);
        candidate.y_d = read_binary<float>(stream);
        candidate.aeh_h = read_binary<float>(stream);
        candidates_.push_back(candidate);
    }
    wind_states_.reserve(state_count);
    for (std::uint32_t index = 0; index < state_count; ++index) {
        WindState state;
        state.direction_degrees = read_binary<float>(stream);
        state.speed_mps = read_binary<float>(stream);
        state.probability = read_binary<float>(stream);
        wind_states_.push_back(state);
    }
    turbine_curve_.reserve(curve_count);
    for (std::uint32_t index = 0; index < curve_count; ++index) {
        TurbineCurvePoint point;
        point.speed_mps = read_binary<float>(stream);
        point.normalized_power = read_binary<float>(stream);
        point.thrust_coefficient = read_binary<float>(stream);
        turbine_curve_.push_back(point);
    }
    char trailing = '\0';
    stream.read(&trailing, 1);
    if (stream.gcount() != 0) {
        throw std::runtime_error("T87 proxy fixture has trailing bytes");
    }
    if (std::abs(wind_probability_sum() - 1.0) > 1.0e-6) {
        throw std::runtime_error("T87 wind probabilities do not sum to one");
    }
}

double Problem::normalized_power(const double speed_mps) const {
    if (speed_mps < 3.0 || speed_mps > 20.0) return 0.0;
    const auto upper = std::upper_bound(
        turbine_curve_.begin(),
        turbine_curve_.end(),
        speed_mps,
        [](const double value, const TurbineCurvePoint& point) {
            return value < point.speed_mps;
        }
    );
    if (upper == turbine_curve_.begin()) return upper->normalized_power;
    if (upper == turbine_curve_.end()) {
        return turbine_curve_.back().normalized_power;
    }
    const auto lower = std::prev(upper);
    const double fraction = (
        speed_mps - lower->speed_mps
    ) / (upper->speed_mps - lower->speed_mps);
    return std::lerp(
        lower->normalized_power, upper->normalized_power, fraction
    );
}

double Problem::thrust_coefficient(const double speed_mps) const {
    if (speed_mps < 2.5 || speed_mps > 20.0) return 0.0;
    const auto upper = std::upper_bound(
        turbine_curve_.begin(),
        turbine_curve_.end(),
        speed_mps,
        [](const double value, const TurbineCurvePoint& point) {
            return value < point.speed_mps;
        }
    );
    double value = 0.0;
    if (upper == turbine_curve_.begin()) {
        value = upper->thrust_coefficient;
    } else if (upper == turbine_curve_.end()) {
        value = turbine_curve_.back().thrust_coefficient;
    } else {
        const auto lower = std::prev(upper);
        const double fraction = (
            speed_mps - lower->speed_mps
        ) / (upper->speed_mps - lower->speed_mps);
        value = std::lerp(
            lower->thrust_coefficient,
            upper->thrust_coefficient,
            fraction
        );
    }
    return std::clamp(value, 0.0, 0.999);
}

void Problem::calibrate_candidate_speed_multipliers() {
    for (Candidate& candidate : candidates_) {
        double low = 0.1;
        double high = 4.0;
        for (int iteration = 0; iteration < 80; ++iteration) {
            const double middle = 0.5 * (low + high);
            double aeh = 0.0;
            for (const WindState& state : wind_states_) {
                aeh += 8760.0 * state.probability
                    * normalized_power(state.speed_mps * middle);
            }
            if (aeh < candidate.aeh_h) low = middle;
            else high = middle;
        }
        candidate.speed_multiplier = 0.5 * (low + high);
    }
}

double Problem::wake_deficit_ratio(
    const double downstream_d,
    const double crosswind_d,
    const double ct
) const {
    if (downstream_d <= 0.0 || ct <= 0.0) return 0.0;
    if (wake_model_ == WakeModel::jensen) {
        const double wake_radius_d = 0.5 + wake_growth * downstream_d;
        const double overlap = circle_overlap_area(
            wake_radius_d, 0.5, std::abs(crosswind_d)
        ) / (std::numbers::pi * 0.25);
        const double centre = (
            1.0 - std::sqrt(std::max(0.0, 1.0 - ct))
        ) / std::pow(1.0 + 2.0 * wake_growth * downstream_d, 2.0);
        return centre * overlap;
    }
    const double root = std::sqrt(std::max(1.0e-12, 1.0 - ct));
    const double epsilon = 0.2 * std::sqrt(0.5 + 0.5 / root);
    const double sigma_d = wake_growth * downstream_d + epsilon;
    const double radicand = std::max(
        0.0, 1.0 - ct / (8.0 * sigma_d * sigma_d)
    );
    const double amplitude = 1.0 - std::sqrt(radicand);
    const double ring_radius_d = std::sqrt(2.0 / 3.0) * 0.5;
    double average = 0.25 * (
        wake_model_ == WakeModel::gaussian
            ? std::exp(
                -0.5 * crosswind_d * crosswind_d
                / (sigma_d * sigma_d)
            )
            : 0.5 * (
                std::exp(
                    -0.5 * std::pow(
                        std::abs(crosswind_d) + dgwm_rmin_d, 2.0
                    ) / (sigma_d * sigma_d)
                )
                + std::exp(
                    -0.5 * std::pow(
                        std::abs(crosswind_d) - dgwm_rmin_d, 2.0
                    ) / (sigma_d * sigma_d)
                )
            )
    );
    for (int sample = 0; sample < 6; ++sample) {
        const double angle = 2.0 * std::numbers::pi
            * static_cast<double>(sample) / 6.0;
        const double radial = std::hypot(
            crosswind_d + ring_radius_d * std::cos(angle),
            ring_radius_d * std::sin(angle)
        );
        if (wake_model_ == WakeModel::gaussian) {
            average += 0.125 * std::exp(
                -0.5 * radial * radial / (sigma_d * sigma_d)
            );
        } else {
            average += 0.0625 * (
                std::exp(
                    -0.5 * std::pow(radial + dgwm_rmin_d, 2.0)
                    / (sigma_d * sigma_d)
                )
                + std::exp(
                    -0.5 * std::pow(radial - dgwm_rmin_d, 2.0)
                    / (sigma_d * sigma_d)
                )
            );
        }
    }
    return amplitude * average;
}

void Problem::precompute_candidate_deficits() {
    const int states = static_cast<int>(wind_states_.size());
    const int count = static_cast<int>(candidates_.size());
    candidate_deficit_ratio_.assign(
        static_cast<std::size_t>(states)
            * static_cast<std::size_t>(count)
            * static_cast<std::size_t>(count),
        0.0F
    );
    fode::PersistentExecutor executor(precomputation_workers_);
    executor.parallel_for(0, states * count, [&](const int item) {
        const int state_index = item / count;
        const int source = item % count;
        const WindState& state =
            wind_states_[static_cast<std::size_t>(state_index)];
        const Candidate& source_candidate =
            candidates_[static_cast<std::size_t>(source)];
        const double angle = state.direction_degrees
            * std::numbers::pi / 180.0;
        const double flow_x = std::sin(angle);
        const double flow_y = std::cos(angle);
        const double cross_x = std::cos(angle);
        const double cross_y = -std::sin(angle);
        const double ct = thrust_coefficient(
            state.speed_mps * source_candidate.speed_multiplier
        );
        for (int target = 0; target < count; ++target) {
            if (source == target) continue;
            const Candidate& target_candidate =
                candidates_[static_cast<std::size_t>(target)];
            const double dx = target_candidate.x_d - source_candidate.x_d;
            const double dy = target_candidate.y_d - source_candidate.y_d;
            const double downstream_d = dx * flow_x + dy * flow_y;
            const double crosswind_d = dx * cross_x + dy * cross_y;
            candidate_deficit_ratio_[deficit_index(
                state_index, count, source, target
            )] = static_cast<float>(
                wake_deficit_ratio(downstream_d, crosswind_d, ct)
            );
        }
    });
}

double Problem::spatial_speed_multiplier(
    const double x_d,
    const double y_d
) const {
    std::array<double, 4> best_distance{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<int, 4> best_index{-1, -1, -1, -1};
    for (std::size_t index = 0; index < candidates_.size(); ++index) {
        const double distance = std::hypot(
            x_d - candidates_[index].x_d,
            y_d - candidates_[index].y_d
        );
        if (distance < 1.0e-12) {
            return candidates_[index].speed_multiplier;
        }
        for (int slot = 0; slot < 4; ++slot) {
            if (distance < best_distance[static_cast<std::size_t>(slot)]) {
                for (int move = 3; move > slot; --move) {
                    best_distance[static_cast<std::size_t>(move)] =
                        best_distance[static_cast<std::size_t>(move - 1)];
                    best_index[static_cast<std::size_t>(move)] =
                        best_index[static_cast<std::size_t>(move - 1)];
                }
                best_distance[static_cast<std::size_t>(slot)] = distance;
                best_index[static_cast<std::size_t>(slot)] =
                    static_cast<int>(index);
                break;
            }
        }
    }
    double weighted = 0.0;
    double weight_sum = 0.0;
    for (int slot = 0; slot < 4; ++slot) {
        const double weight = 1.0 / std::max(
            1.0e-12,
            best_distance[static_cast<std::size_t>(slot)]
                * best_distance[static_cast<std::size_t>(slot)]
        );
        weighted += weight * candidates_[static_cast<std::size_t>(
            best_index[static_cast<std::size_t>(slot)]
        )].speed_multiplier;
        weight_sum += weight;
    }
    return weighted / weight_sum;
}

Evaluation Problem::finish_evaluation(
    const std::vector<double>& coordinates_d,
    const std::vector<double>& local_speed_multipliers,
    const bool use_candidate_precomputation,
    const std::vector<int>& candidate_indices
) const {
    Evaluation result;
    if (
        coordinates_d.size() % 2U != 0U
        || local_speed_multipliers.size() * 2U != coordinates_d.size()
    ) {
        result.total_normalized_constraint_violation = 1.0;
        return result;
    }
    const int turbines = static_cast<int>(local_speed_multipliers.size());
    result.turbine_count = turbines;
    for (int left = 0; left < turbines; ++left) {
        const double left_x =
            coordinates_d[static_cast<std::size_t>(2 * left)];
        const double left_y =
            coordinates_d[static_cast<std::size_t>(2 * left + 1)];
        if (left_x < -10.0 || left_x > 10.0) {
            result.total_normalized_constraint_violation +=
                std::abs(left_x - std::clamp(left_x, -10.0, 10.0));
        }
        if (left_y < -46.0 || left_y > 46.0) {
            result.total_normalized_constraint_violation +=
                std::abs(left_y - std::clamp(left_y, -46.0, 46.0));
        }
        for (int right = left + 1; right < turbines; ++right) {
            const double distance_d = std::hypot(
                left_x - coordinates_d[static_cast<std::size_t>(2 * right)],
                left_y
                    - coordinates_d[static_cast<std::size_t>(2 * right + 1)]
            );
            result.total_normalized_constraint_violation +=
                std::max(0.0, (minimum_spacing_d - distance_d)
                    / minimum_spacing_d);
        }
    }
    double no_wake_aep_mwh = 0.0;
    std::vector<double> deficit_squares(
        static_cast<std::size_t>(turbines), 0.0
    );
    const int candidate_count = static_cast<int>(candidates_.size());
    for (
        std::size_t state_index = 0;
        state_index < wind_states_.size();
        ++state_index
    ) {
        const WindState& state = wind_states_[state_index];
        std::fill(deficit_squares.begin(), deficit_squares.end(), 0.0);
        const double angle = state.direction_degrees
            * std::numbers::pi / 180.0;
        const double flow_x = std::sin(angle);
        const double flow_y = std::cos(angle);
        const double cross_x = std::cos(angle);
        const double cross_y = -std::sin(angle);
        for (int source = 0; source < turbines; ++source) {
            const double source_speed = state.speed_mps
                * local_speed_multipliers[static_cast<std::size_t>(source)];
            const double ct = thrust_coefficient(source_speed);
            for (int target = 0; target < turbines; ++target) {
                if (source == target) continue;
                double ratio = 0.0;
                if (use_candidate_precomputation) {
                    ratio = candidate_deficit_ratio_[deficit_index(
                        static_cast<int>(state_index),
                        candidate_count,
                        candidate_indices[static_cast<std::size_t>(source)],
                        candidate_indices[static_cast<std::size_t>(target)]
                    )];
                } else {
                    const double dx =
                        coordinates_d[static_cast<std::size_t>(2 * target)]
                        - coordinates_d[
                            static_cast<std::size_t>(2 * source)
                        ];
                    const double dy =
                        coordinates_d[
                            static_cast<std::size_t>(2 * target + 1)
                        ] - coordinates_d[
                            static_cast<std::size_t>(2 * source + 1)
                        ];
                    ratio = wake_deficit_ratio(
                        dx * flow_x + dy * flow_y,
                        dx * cross_x + dy * cross_y,
                        ct
                    );
                }
                const double absolute_deficit = source_speed * ratio;
                deficit_squares[static_cast<std::size_t>(target)] +=
                    absolute_deficit * absolute_deficit;
            }
        }
        for (int target = 0; target < turbines; ++target) {
            const double ambient = state.speed_mps
                * local_speed_multipliers[static_cast<std::size_t>(target)];
            const double waked_speed = std::max(
                0.0,
                ambient
                    - std::sqrt(deficit_squares[
                        static_cast<std::size_t>(target)
                    ])
            );
            const double factor = state.probability * 8760.0
                * rated_power_mw;
            no_wake_aep_mwh += factor * normalized_power(ambient);
            result.aep_mwh += factor * normalized_power(waked_speed);
        }
    }
    result.wake_efficiency = no_wake_aep_mwh > 0.0
        ? result.aep_mwh / no_wake_aep_mwh
        : 1.0;
    result.nav_rmb_per_year =
        nav_net_value_rmb_per_mwh * result.aep_mwh
        - nav_annualized_cost_rmb_per_turbine
            * static_cast<double>(turbines);
    result.fitness = objective_model_ == ObjectiveModel::aep
        ? result.aep_mwh
        : result.nav_rmb_per_year;
    result.feasible =
        result.total_normalized_constraint_violation <= 1.0e-12;
    return result;
}

Evaluation Problem::evaluate_candidate_indices(
    const std::vector<int>& candidate_indices
) const {
    std::vector<double> coordinates;
    std::vector<double> multipliers;
    coordinates.reserve(2U * candidate_indices.size());
    multipliers.reserve(candidate_indices.size());
    bool invalid = false;
    for (const int index : candidate_indices) {
        if (index < 0 || index >= static_cast<int>(candidates_.size())) {
            invalid = true;
            continue;
        }
        const Candidate& candidate =
            candidates_[static_cast<std::size_t>(index)];
        coordinates.push_back(candidate.x_d);
        coordinates.push_back(candidate.y_d);
        multipliers.push_back(candidate.speed_multiplier);
    }
    if (invalid || multipliers.size() != candidate_indices.size()) {
        Evaluation result;
        result.total_normalized_constraint_violation = 1.0;
        return result;
    }
    Evaluation result = finish_evaluation(
        coordinates, multipliers, true, candidate_indices
    );
    auto ordered_indices = candidate_indices;
    std::sort(ordered_indices.begin(), ordered_indices.end());
    if (std::adjacent_find(
        ordered_indices.begin(), ordered_indices.end()
    ) != ordered_indices.end()) {
        result.total_normalized_constraint_violation += 1.0;
        result.feasible = false;
    }
    return result;
}

Evaluation Problem::evaluate_coordinates_d(
    const std::vector<double>& coordinates_d
) const {
    if (coordinates_d.size() % 2U != 0U) {
        Evaluation result;
        result.total_normalized_constraint_violation = 1.0;
        return result;
    }
    std::vector<double> multipliers(coordinates_d.size() / 2U);
    for (std::size_t turbine = 0; turbine < multipliers.size(); ++turbine) {
        multipliers[turbine] = spatial_speed_multiplier(
            coordinates_d[2U * turbine],
            coordinates_d[2U * turbine + 1U]
        );
    }
    return finish_evaluation(
        coordinates_d, multipliers, false, {}
    );
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers <= 0 || config.iga_population < 2
        || config.iga_generations < 0 || config.pso_population < 2
        || config.pso_iterations < 0
    ) {
        throw std::invalid_argument("invalid T87 run configuration");
    }
    const auto run_begin = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int candidate_count =
        static_cast<int>(problem.candidates().size());
    const int word_count = (candidate_count + 63) / 64;

    std::vector<Genome> population(
        static_cast<std::size_t>(config.iga_population),
        Genome(static_cast<std::size_t>(word_count), 0ULL)
    );
    for (int individual = 0; individual < config.iga_population; ++individual) {
        const double assignment_probability = rng.uniform(
            0, 100, static_cast<std::uint64_t>(individual)
        );
        for (int candidate = 0; candidate < candidate_count; ++candidate) {
            if (
                rng.uniform(
                    0,
                    101,
                    static_cast<std::uint64_t>(individual),
                    static_cast<std::uint64_t>(candidate)
                ) < assignment_probability
            ) {
                population[static_cast<std::size_t>(individual)]
                    [static_cast<std::size_t>(candidate / 64)]
                    |= 1ULL << static_cast<unsigned>(candidate % 64);
            }
        }
        mask_unused_bits(
            population[static_cast<std::size_t>(individual)],
            candidate_count
        );
    }

    RunResult result;
    result.case_id = problem.case_id();
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id =
        "t87_iga_pso_predecessor_completed_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.iga_population = config.iga_population;
    result.iga_generations = config.iga_generations;
    result.pso_population = config.pso_population;
    result.pso_iterations = config.pso_iterations;
    result.precomputation_seconds = problem.precomputation_seconds();

    BatchResult batch = evaluate_unique_genomes(
        problem, population, executor
    );
    std::vector<Evaluation> evaluations = std::move(batch.evaluations);
    result.proposed_fes += population.size();
    result.physical_unique_fes += batch.unique_count;
    result.evaluator_seconds += batch.seconds;
    std::size_t best = best_index(evaluations);
    Genome global_best = population[best];
    Evaluation global_best_evaluation = evaluations[best];
    result.best_fitness_history.push_back(global_best_evaluation.fitness);

    for (
        int generation = 1;
        generation <= config.iga_generations;
        ++generation
    ) {
        const auto order = fitness_rank(evaluations);
        std::vector<Genome> offspring(
            population.size(),
            Genome(static_cast<std::size_t>(word_count), 0ULL)
        );
        for (int individual = 0; individual < config.iga_population; ++individual) {
            const std::size_t parent = roulette_rank_select(
                order,
                rng.uniform(
                    static_cast<std::uint64_t>(generation),
                    110,
                    static_cast<std::uint64_t>(individual)
                )
            );
            offspring[static_cast<std::size_t>(individual)] =
                population[parent];
        }
        for (
            int pair = 0;
            pair + 1 < config.iga_population;
            pair += 2
        ) {
            if (
                rng.uniform(
                    static_cast<std::uint64_t>(generation),
                    111,
                    static_cast<std::uint64_t>(pair)
                ) >= crossover_probability
            ) {
                continue;
            }
            const int cut = rng.integer(
                1,
                candidate_count,
                static_cast<std::uint64_t>(generation),
                112,
                static_cast<std::uint64_t>(pair)
            );
            const int cut_word = cut / 64;
            const int cut_bit = cut % 64;
            Genome& first = offspring[static_cast<std::size_t>(pair)];
            Genome& second = offspring[static_cast<std::size_t>(pair + 1)];
            if (cut_bit != 0) {
                const std::uint64_t high_mask =
                    ~((1ULL << static_cast<unsigned>(cut_bit)) - 1ULL);
                const std::uint64_t changed =
                    (first[static_cast<std::size_t>(cut_word)]
                        ^ second[static_cast<std::size_t>(cut_word)])
                    & high_mask;
                first[static_cast<std::size_t>(cut_word)] ^= changed;
                second[static_cast<std::size_t>(cut_word)] ^= changed;
            }
            for (int word = cut_word + (cut_bit != 0 ? 1 : 0);
                 word < word_count; ++word) {
                std::swap(
                    first[static_cast<std::size_t>(word)],
                    second[static_cast<std::size_t>(word)]
                );
            }
        }
        const double mutation_probability = std::min(
            final_mutation_probability,
            initial_mutation_probability
                + static_cast<double>(generation)
                    / std::max(
                        1.0,
                        static_cast<double>(config.iga_generations) / 2.0
                    )
                    * (
                        final_mutation_probability
                        - initial_mutation_probability
                    )
        );
        for (int individual = 0; individual < config.iga_population; ++individual) {
            if (
                rng.uniform(
                    static_cast<std::uint64_t>(generation),
                    113,
                    static_cast<std::uint64_t>(individual)
                ) < mutation_probability
            ) {
                const int candidate = rng.integer(
                    0,
                    candidate_count,
                    static_cast<std::uint64_t>(generation),
                    114,
                    static_cast<std::uint64_t>(individual)
                );
                offspring[static_cast<std::size_t>(individual)]
                    [static_cast<std::size_t>(candidate / 64)]
                    ^= 1ULL << static_cast<unsigned>(candidate % 64);
            }
            mask_unused_bits(
                offspring[static_cast<std::size_t>(individual)],
                candidate_count
            );
        }
        // The target says the optimal layout is preserved. One elite is the
        // minimal deterministic completion of that statement.
        offspring.back() = global_best;
        batch = evaluate_unique_genomes(problem, offspring, executor);
        result.proposed_fes += offspring.size();
        result.physical_unique_fes += batch.unique_count;
        result.evaluator_seconds += batch.seconds;
        population.swap(offspring);
        evaluations = std::move(batch.evaluations);
        best = best_index(evaluations);
        if (better_evaluation(
            evaluations[best], global_best_evaluation
        )) {
            global_best = population[best];
            global_best_evaluation = evaluations[best];
        }
        result.best_fitness_history.push_back(
            global_best_evaluation.fitness
        );
    }

    result.best_grid_candidate_indices = decode_genome(
        global_best, candidate_count
    );
    if (result.best_grid_candidate_indices.empty()) {
        const auto highest = std::max_element(
            problem.candidates().begin(),
            problem.candidates().end(),
            [](const Candidate& left, const Candidate& right) {
                return left.aeh_h < right.aeh_h;
            }
        );
        result.best_grid_candidate_indices.push_back(
            static_cast<int>(
                std::distance(problem.candidates().begin(), highest)
            )
        );
        global_best_evaluation = problem.evaluate_candidate_indices(
            result.best_grid_candidate_indices
        );
        ++result.physical_unique_fes;
    }
    result.best_grid_evaluation = global_best_evaluation;

    const std::vector<double> base = coordinates_from_candidates(
        problem, result.best_grid_candidate_indices
    );
    const int variables = static_cast<int>(base.size());
    std::vector<std::vector<double>> particles(
        static_cast<std::size_t>(config.pso_population), base
    );
    std::vector<std::vector<double>> velocities(
        static_cast<std::size_t>(config.pso_population),
        std::vector<double>(static_cast<std::size_t>(variables), 0.0)
    );
    for (int particle = 1; particle < config.pso_population; ++particle) {
        for (int variable = 0; variable < variables; ++variable) {
            const double perturbation = pso_neighborhood_d * (
                2.0 * rng.uniform(
                    0,
                    120,
                    static_cast<std::uint64_t>(particle),
                    static_cast<std::uint64_t>(variable)
                ) - 1.0
            );
            const bool x_coordinate = variable % 2 == 0;
            const double site_low = x_coordinate ? -10.0 : -46.0;
            const double site_high = x_coordinate ? 10.0 : 46.0;
            particles[static_cast<std::size_t>(particle)]
                [static_cast<std::size_t>(variable)] = std::clamp(
                    base[static_cast<std::size_t>(variable)] + perturbation,
                    std::max(
                        site_low,
                        base[static_cast<std::size_t>(variable)]
                            - pso_neighborhood_d
                    ),
                    std::min(
                        site_high,
                        base[static_cast<std::size_t>(variable)]
                            + pso_neighborhood_d
                    )
                );
            velocities[static_cast<std::size_t>(particle)]
                [static_cast<std::size_t>(variable)] =
                    0.2 * perturbation;
        }
    }
    std::vector<Evaluation> particle_evaluations = evaluate_particles(
        problem, particles, executor, result.evaluator_seconds
    );
    result.proposed_fes += particles.size();
    result.physical_unique_fes += particles.size();
    std::vector<std::vector<double>> personal_best = particles;
    std::vector<Evaluation> personal_best_evaluation = particle_evaluations;
    std::size_t particle_best = best_index(particle_evaluations);
    std::vector<double> continuous_best = particles[particle_best];
    Evaluation continuous_best_evaluation =
        particle_evaluations[particle_best];
    result.best_fitness_history.push_back(
        continuous_best_evaluation.fitness
    );

    for (
        int iteration = 1;
        iteration <= config.pso_iterations;
        ++iteration
    ) {
        const double inertia = initial_inertia
            + (final_inertia - initial_inertia)
                * static_cast<double>(iteration)
                / std::max(1, config.pso_iterations);
        executor.parallel_for(
            0, config.pso_population, [&](const int particle) {
                for (int variable = 0; variable < variables; ++variable) {
                    const double current =
                        particles[static_cast<std::size_t>(particle)]
                            [static_cast<std::size_t>(variable)];
                    const double cognitive_draw = rng.uniform(
                        static_cast<std::uint64_t>(iteration),
                        130,
                        static_cast<std::uint64_t>(particle),
                        static_cast<std::uint64_t>(variable)
                    );
                    const double social_draw = rng.uniform(
                        static_cast<std::uint64_t>(iteration),
                        131,
                        static_cast<std::uint64_t>(particle),
                        static_cast<std::uint64_t>(variable)
                    );
                    double velocity = inertia
                        * velocities[static_cast<std::size_t>(particle)]
                            [static_cast<std::size_t>(variable)]
                        + pso_cognitive * cognitive_draw * (
                            personal_best[static_cast<std::size_t>(particle)]
                                [static_cast<std::size_t>(variable)]
                            - current
                        )
                        + pso_social * social_draw * (
                            continuous_best[
                                static_cast<std::size_t>(variable)
                            ] - current
                        );
                    velocity = std::clamp(
                        velocity,
                        -pso_neighborhood_d,
                        pso_neighborhood_d
                    );
                    const bool x_coordinate = variable % 2 == 0;
                    const double site_low = x_coordinate ? -10.0 : -46.0;
                    const double site_high = x_coordinate ? 10.0 : 46.0;
                    const double low = std::max(
                        site_low,
                        base[static_cast<std::size_t>(variable)]
                            - pso_neighborhood_d
                    );
                    const double high = std::min(
                        site_high,
                        base[static_cast<std::size_t>(variable)]
                            + pso_neighborhood_d
                    );
                    const double updated = std::clamp(
                        current + velocity, low, high
                    );
                    velocities[static_cast<std::size_t>(particle)]
                        [static_cast<std::size_t>(variable)] =
                            updated == current + velocity ? velocity : 0.0;
                    particles[static_cast<std::size_t>(particle)]
                        [static_cast<std::size_t>(variable)] = updated;
                }
            }
        );
        particle_evaluations = evaluate_particles(
            problem, particles, executor, result.evaluator_seconds
        );
        result.proposed_fes += particles.size();
        result.physical_unique_fes += particles.size();
        for (int particle = 0; particle < config.pso_population; ++particle) {
            if (better_evaluation(
                particle_evaluations[static_cast<std::size_t>(particle)],
                personal_best_evaluation[
                    static_cast<std::size_t>(particle)
                ]
            )) {
                personal_best[static_cast<std::size_t>(particle)] =
                    particles[static_cast<std::size_t>(particle)];
                personal_best_evaluation[
                    static_cast<std::size_t>(particle)
                ] = particle_evaluations[
                    static_cast<std::size_t>(particle)
                ];
            }
            if (better_evaluation(
                personal_best_evaluation[
                    static_cast<std::size_t>(particle)
                ],
                continuous_best_evaluation
            )) {
                continuous_best = personal_best[
                    static_cast<std::size_t>(particle)
                ];
                continuous_best_evaluation = personal_best_evaluation[
                    static_cast<std::size_t>(particle)
                ];
            }
        }
        result.best_fitness_history.push_back(
            continuous_best_evaluation.fitness
        );
    }

    result.best_continuous_coordinates_d = std::move(continuous_best);
    result.best_continuous_evaluation = continuous_best_evaluation;
    const auto receipt = executor.work_receipt();
    result.observed_workers = receipt.distinct_participants;
    result.end_to_end_seconds = std::chrono::duration<double>(
        Clock::now() - run_begin
    ).count();
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mix_hash(hash, static_cast<std::uint64_t>(
        result.best_grid_candidate_indices.size()
    ));
    for (const int index : result.best_grid_candidate_indices) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(index));
    }
    for (const double coordinate : result.best_continuous_coordinates_d) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(coordinate));
    }
    hash = mix_hash(
        hash,
        std::bit_cast<std::uint64_t>(
            result.best_continuous_evaluation.fitness
        )
    );
    result.scientific_hash = hash;
    return result;
}

}  // namespace core99::t87
