/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0259 pure-C++ SUGGA, LIBSVM and precomputed grid wake
Paper/DOI: public source, conflicts, missing facts, reconstruction
completion, semantic IDs, production backend and claim boundary:
hpc/core99_cpp/include/core99/ju_l0259.hpp.
Public source: pinned MIT WFLOP_SUGGA_Python revision declared in the header.
SVR source: pinned official BSD-3-Clause LIBSVM revision declared in header
and third_party/libsvm-l0259/PROVENANCE.md.
Controlling contract: shared/contracts/core99_l0259_sugga_2019.json.
Independent validator: scripts/validate_core99_l0259.py.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/ju_l0259.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"
#include "svm.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
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

namespace core99::l0259 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kGridSide = 12;
constexpr int kCandidateCount = kGridSide * kGridSide;
constexpr int kPaperPopulation = 120;
constexpr int kPaperGenerations = 200;
constexpr int kPaperMonteCarloLayouts = 10000;
constexpr int kPaperRepeats = 100;
constexpr double kRotorRadiusM = 38.5;
constexpr double kSurfaceRoughnessM = 0.00025;
constexpr double kEliteRate = 0.2;
constexpr double kSelectionRate = 0.5;
constexpr double kMutationRate = 0.1;
constexpr double kInformedRate = 0.5;
constexpr int kSvrCandidateLocations = 5;

struct WindState {
    double from_radians = 0.0;
    double speed_mps = 13.0;
    double probability = 1.0;
};

struct Surface {
    std::vector<double> training_targets_kw;
    std::vector<double> predictions_kw;
};

struct CachedSurrogate {
    Surface surface;
    double monte_carlo_truth_seconds = 0.0;
    double training_seconds = 0.0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<WindState> wind_states(const int profile) {
    if (profile == 1) return {{0.0, 13.0, 1.0}};
    if (profile == 2) {
        return {
            {0.0, 13.0, 0.25},
            {std::numbers::pi / 2.0, 13.0, 0.25},
            {std::numbers::pi, 13.0, 0.25},
            {3.0 * std::numbers::pi / 2.0, 13.0, 0.25},
        };
    }
    if (profile == 3) {
        return {
            {0.0, 13.0, 0.20},
            {std::numbers::pi / 3.0, 13.0, 0.30},
            {2.0 * std::numbers::pi / 3.0, 13.0, 0.20},
            {std::numbers::pi, 13.0, 0.10},
            {4.0 * std::numbers::pi / 3.0, 13.0, 0.10},
            {5.0 * std::numbers::pi / 3.0, 13.0, 0.10},
        };
    }
    throw std::invalid_argument("invalid L0259 wind profile");
}

double turbine_power_kw(const double speed_mps) {
    if (speed_mps < 2.0 || speed_mps >= 18.0) return 0.0;
    if (speed_mps < 12.8) {
        return 0.3 * speed_mps * speed_mps * speed_mps;
    }
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
    const double distance = std::max(center_distance, 1.0e-12);
    const double wake_argument = std::clamp(
        (
            distance * distance + wake_radius * wake_radius
            - rotor_radius * rotor_radius
        ) / (2.0 * distance * wake_radius),
        -1.0,
        1.0
    );
    const double rotor_argument = std::clamp(
        (
            distance * distance + rotor_radius * rotor_radius
            - wake_radius * wake_radius
        ) / (2.0 * distance * rotor_radius),
        -1.0,
        1.0
    );
    const double radicand = std::max(
        0.0,
        (-distance + wake_radius + rotor_radius)
            * (distance + wake_radius - rotor_radius)
            * (distance - wake_radius + rotor_radius)
            * (distance + wake_radius + rotor_radius)
    );
    return wake_radius * wake_radius * std::acos(wake_argument)
        + rotor_radius * rotor_radius * std::acos(rotor_argument)
        - 0.5 * std::sqrt(radicand);
}

std::vector<int> unavailable_one_based(const int landscape) {
    std::vector<int> result;
    const auto sequence = [&](const int start, const int stop, const int step) {
        for (int value = start; value < stop; value += step) {
            result.push_back(value);
        }
    };
    switch (landscape) {
    case 0:
        break;
    case 1:
        sequence(121, 145, 1);
        break;
    case 2:
        sequence(61, 85, 1);
        break;
    case 3:
        sequence(11, 144, 12);
        sequence(12, 145, 12);
        break;
    case 4:
        sequence(6, 144, 12);
        sequence(7, 145, 12);
        break;
    case 5:
        sequence(41, 105, 12);
        sequence(42, 105, 12);
        sequence(43, 105, 12);
        sequence(44, 105, 12);
        break;
    case 6:
        sequence(1, 28, 12);
        sequence(2, 28, 12);
        sequence(12, 37, 12);
        sequence(11, 37, 12);
        sequence(109, 145, 12);
        sequence(119, 145, 12);
        sequence(110, 145, 12);
        sequence(120, 145, 12);
        break;
    case 7:
        sequence(133, 145, 1);
        break;
    case 8:
        sequence(61, 73, 1);
        break;
    case 9:
        sequence(12, 145, 12);
        break;
    case 10:
        sequence(6, 145, 12);
        break;
    case 11:
        sequence(42, 105, 12);
        sequence(43, 105, 12);
        break;
    case 12:
        result = {1,2,11,12,13,24,121,132,133,134,143,144};
        break;
    default:
        throw std::invalid_argument("invalid L0259 landscape");
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
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
    throw std::invalid_argument("unknown L0259 variant " + variant);
}

void quiet_libsvm(const char*) {}

Surface fit_svr(const std::vector<double>& target) {
    if (target.size() != static_cast<std::size_t>(kCandidateCount)) {
        throw std::invalid_argument("L0259 SVR target cardinality");
    }
    svm_set_print_string_function(&quiet_libsvm);
    std::vector<std::array<svm_node, 3>> nodes(kCandidateCount);
    std::vector<svm_node*> node_pointers(kCandidateCount);
    std::vector<double> response = target;
    for (int node = 0; node < kCandidateCount; ++node) {
        auto& row = nodes[static_cast<std::size_t>(node)];
        row[0] = {1, static_cast<double>(node % kGridSide)};
        row[1] = {2, static_cast<double>(node / kGridSide)};
        row[2] = {-1, 0.0};
        node_pointers[static_cast<std::size_t>(node)] = row.data();
    }
    svm_problem problem{};
    problem.l = kCandidateCount;
    problem.y = response.data();
    problem.x = node_pointers.data();
    svm_parameter parameter{};
    parameter.svm_type = EPSILON_SVR;
    parameter.kernel_type = RBF;
    parameter.degree = 3;
    parameter.gamma = 0.3;
    parameter.coef0 = 0.0;
    parameter.cache_size = 100.0;
    parameter.eps = 1.0e-3;
    parameter.C = 2000.0;
    parameter.nr_weight = 0;
    parameter.weight_label = nullptr;
    parameter.weight = nullptr;
    parameter.nu = 0.5;
    parameter.p = 0.1;
    parameter.shrinking = 1;
    parameter.probability = 0;
    const char* error = svm_check_parameter(&problem, &parameter);
    if (error != nullptr) {
        throw std::runtime_error(std::string("L0259 LIBSVM: ") + error);
    }
    svm_model* raw_model = svm_train(&problem, &parameter);
    if (raw_model == nullptr) {
        throw std::runtime_error("L0259 LIBSVM training failed");
    }
    const auto destroy = [](svm_model* model) {
        svm_free_and_destroy_model(&model);
    };
    std::unique_ptr<svm_model, decltype(destroy)> model(raw_model, destroy);
    Surface surface;
    surface.training_targets_kw = target;
    surface.predictions_kw.resize(kCandidateCount);
    for (int node = 0; node < kCandidateCount; ++node) {
        surface.predictions_kw[static_cast<std::size_t>(node)] =
            svm_predict(model.get(), nodes[static_cast<std::size_t>(node)].data());
    }
    return surface;
}

}  // namespace

struct Problem::State {
    std::vector<WindState> winds;
    std::array<bool, kCandidateCount> available{};
    std::vector<int> available_nodes;
    std::vector<double> squared_deficit;
    double ideal_power_per_turbine_kw = 0.0;
};

namespace {

bool feasible_layout(
    const Layout& layout,
    const Problem::State& state,
    const int turbine_count
) {
    if (layout.size() != static_cast<std::size_t>(turbine_count)) {
        return false;
    }
    if (!std::is_sorted(layout.begin(), layout.end())) return false;
    if (layout.empty() || layout.front() < 0
        || layout.back() >= kCandidateCount) {
        return false;
    }
    if (std::adjacent_find(layout.begin(), layout.end()) != layout.end()) {
        return false;
    }
    return std::all_of(layout.begin(), layout.end(), [&](const int node) {
        return state.available[static_cast<std::size_t>(node)];
    });
}

Layout random_layout(
    const Problem::State& state,
    const int turbine_count,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t phase
) {
    std::vector<std::pair<double, int>> keys;
    keys.reserve(state.available_nodes.size());
    for (const int node : state.available_nodes) {
        keys.emplace_back(
            random.uniform(generation, phase, individual, node),
            node
        );
    }
    std::nth_element(
        keys.begin(),
        keys.begin() + turbine_count,
        keys.end()
    );
    Layout layout;
    layout.reserve(static_cast<std::size_t>(turbine_count));
    for (int index = 0; index < turbine_count; ++index) {
        layout.push_back(keys[static_cast<std::size_t>(index)].second);
    }
    std::sort(layout.begin(), layout.end());
    return layout;
}

Evaluation evaluate_layout(
    const Layout& layout,
    const Problem::State& state,
    const int turbine_count
) {
    Evaluation result;
    result.turbine_power_kw.assign(
        static_cast<std::size_t>(turbine_count),
        0.0
    );
    if (!feasible_layout(layout, state, turbine_count)) return result;
    for (std::size_t wind = 0; wind < state.winds.size(); ++wind) {
        const auto& wind_state = state.winds[wind];
        const std::size_t offset =
            wind * kCandidateCount * kCandidateCount;
        for (int downstream_index = 0;
             downstream_index < turbine_count;
             ++downstream_index) {
            const int downstream =
                layout[static_cast<std::size_t>(downstream_index)];
            double sum_squared_deficit = 0.0;
            const std::size_t row =
                offset
                + static_cast<std::size_t>(downstream) * kCandidateCount;
            for (const int upstream : layout) {
                sum_squared_deficit += state.squared_deficit[
                    row + static_cast<std::size_t>(upstream)
                ];
            }
            const double speed = wind_state.speed_mps
                * (1.0 - std::sqrt(sum_squared_deficit));
            result.turbine_power_kw[
                static_cast<std::size_t>(downstream_index)
            ] += wind_state.probability * turbine_power_kw(speed);
        }
    }
    result.expected_power_kw = std::accumulate(
        result.turbine_power_kw.begin(),
        result.turbine_power_kw.end(),
        0.0
    );
    result.efficiency_percent =
        100.0 * result.expected_power_kw
        / (
            static_cast<double>(turbine_count)
            * state.ideal_power_per_turbine_kw
        );
    result.feasible = std::isfinite(result.expected_power_kw);
    return result;
}

std::shared_ptr<const CachedSurrogate> train_cached(
    const Problem::State& state,
    const int turbine_count,
    const int monte_carlo_layouts,
    const std::uint64_t seed,
    fode::PersistentExecutor& executor
) {
    auto result = std::make_shared<CachedSurrogate>();
    const fode::CounterRng random(seed);
    std::vector<Layout> layouts(
        static_cast<std::size_t>(monte_carlo_layouts)
    );
    std::vector<Evaluation> evaluations(
        static_cast<std::size_t>(monte_carlo_layouts)
    );
    const auto truth_start = Clock::now();
    executor.parallel_for(0, monte_carlo_layouts, [&](const int sample) {
        auto layout = random_layout(
            state,
            turbine_count,
            random,
            0,
            static_cast<std::uint64_t>(sample),
            10
        );
        evaluations[static_cast<std::size_t>(sample)] =
            evaluate_layout(layout, state, turbine_count);
        layouts[static_cast<std::size_t>(sample)] = std::move(layout);
    });
    result->monte_carlo_truth_seconds = elapsed_seconds(truth_start);
    std::array<double, kCandidateCount> sums{};
    std::array<int, kCandidateCount> counts{};
    for (int sample = 0; sample < monte_carlo_layouts; ++sample) {
        const auto& layout = layouts[static_cast<std::size_t>(sample)];
        const auto& powers =
            evaluations[static_cast<std::size_t>(sample)].turbine_power_kw;
        for (int turbine = 0; turbine < turbine_count; ++turbine) {
            const int node = layout[static_cast<std::size_t>(turbine)];
            sums[static_cast<std::size_t>(node)] +=
                powers[static_cast<std::size_t>(turbine)];
            ++counts[static_cast<std::size_t>(node)];
        }
    }
    std::vector<double> targets(kCandidateCount, 0.0);
    for (int node = 0; node < kCandidateCount; ++node) {
        const int count = counts[static_cast<std::size_t>(node)];
        if (count > 0) {
            targets[static_cast<std::size_t>(node)] =
                sums[static_cast<std::size_t>(node)]
                / static_cast<double>(count);
        }
    }
    const auto training_start = Clock::now();
    result->surface = fit_svr(targets);
    result->training_seconds = elapsed_seconds(training_start);
    return result;
}

int random_empty_node(
    const Layout& layout,
    const Problem::State& state,
    const fode::CounterRng& random,
    const std::uint64_t generation,
    const std::uint64_t phase,
    const std::uint64_t individual,
    const std::uint64_t draw
) {
    const int count = static_cast<int>(state.available_nodes.size());
    for (int attempt = 0; attempt < kCandidateCount * 2; ++attempt) {
        const int available_index = random.integer(
            0,
            count,
            generation,
            phase,
            individual,
            static_cast<std::uint64_t>(attempt),
            draw
        );
        const int candidate =
            state.available_nodes[static_cast<std::size_t>(available_index)];
        if (!std::binary_search(layout.begin(), layout.end(), candidate)) {
            return candidate;
        }
    }
    for (const int candidate : state.available_nodes) {
        if (!std::binary_search(layout.begin(), layout.end(), candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("L0259 layout has no empty available node");
}

Layout replace_node(Layout layout, const int old_node, const int new_node) {
    const auto iterator = std::find(layout.begin(), layout.end(), old_node);
    if (iterator == layout.end()) {
        throw std::runtime_error("L0259 replacement node absent");
    }
    *iterator = new_node;
    std::sort(layout.begin(), layout.end());
    return layout;
}

Layout crossover_layout(
    const Layout& first,
    const Layout& second,
    const Problem::State& state,
    const int turbine_count,
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
        child.insert(child.end(), first.begin(), first.begin() + cut);
        child.insert(child.end(), second.begin() + cut, second.end());
        if (feasible_layout(child, state, turbine_count)) return child;
    }
    return first;
}

std::uint64_t result_hash(
    const Layout& layout,
    const std::vector<double>& history,
    const Surface& surface
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const int node : layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(node));
    }
    for (const double value : history) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    for (const double value : surface.predictions_kw) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hash;
}

std::uint64_t training_seed(
    const int wind_profile,
    const int landscape,
    const int turbine_count,
    const std::string& variant
) {
    return 259000000ULL
        + static_cast<std::uint64_t>(wind_profile) * 100000ULL
        + static_cast<std::uint64_t>(landscape) * 1000ULL
        + static_cast<std::uint64_t>(turbine_count) * 10ULL
        + (variant == "source_normal_threshold" ? 1ULL : 0ULL);
}

}  // namespace

Problem::Problem(std::string case_id, std::string variant)
    : case_id_(std::move(case_id)), variant_(std::move(variant)) {
    if (
        variant_ != "paper_probability"
        && variant_ != "source_normal_threshold"
    ) {
        throw std::invalid_argument("invalid L0259 variant " + variant_);
    }
    int consumed = 0;
    if (
        std::sscanf(
            case_id_.c_str(),
            "l0259_d%d_l%d_n%d%n",
            &wind_profile_,
            &landscape_,
            &turbine_count_,
            &consumed
        ) != 3
        || consumed != static_cast<int>(case_id_.size())
        || wind_profile_ < 1 || wind_profile_ > 3
        || landscape_ < 0 || landscape_ > 12
        || (
            turbine_count_ != 15
            && turbine_count_ != 20
            && turbine_count_ != 25
        )
    ) {
        throw std::invalid_argument("invalid L0259 case id " + case_id_);
    }
    if (variant_ == "source_normal_threshold") {
        cell_width_m_ = 231.0;
        hub_height_m_ = 80.0;
    }
    semantic_id_ = "l0259_landowner12x12_d"
        + std::to_string(wind_profile_) + "_l"
        + std::to_string(landscape_) + "_n"
        + std::to_string(turbine_count_) + "_v1";
    state_ = std::make_unique<State>();
    state_->winds = wind_states(wind_profile_);
    state_->available.fill(true);
    for (const int one_based : unavailable_one_based(landscape_)) {
        state_->available[static_cast<std::size_t>(one_based - 1)] = false;
    }
    for (int node = 0; node < kCandidateCount; ++node) {
        if (state_->available[static_cast<std::size_t>(node)]) {
            state_->available_nodes.push_back(node);
        }
    }
    if (
        state_->available_nodes.size()
        < static_cast<std::size_t>(turbine_count_)
    ) {
        throw std::invalid_argument("L0259 problem has too few available cells");
    }
    for (const auto& wind : state_->winds) {
        state_->ideal_power_per_turbine_kw +=
            wind.probability * turbine_power_kw(wind.speed_mps);
    }
    const double entrainment =
        0.5 / std::log(hub_height_m_ / kSurfaceRoughnessM);
    state_->squared_deficit.assign(
        state_->winds.size() * kCandidateCount * kCandidateCount,
        0.0
    );
    for (std::size_t wind = 0; wind < state_->winds.size(); ++wind) {
        const double cosine = std::cos(state_->winds[wind].from_radians);
        const double sine = std::sin(state_->winds[wind].from_radians);
        std::array<double, kCandidateCount> rotated_x{};
        std::array<double, kCandidateCount> rotated_y{};
        for (int node = 0; node < kCandidateCount; ++node) {
            const double x =
                (static_cast<double>(node % kGridSide) + 0.5)
                * cell_width_m_;
            const double y =
                (static_cast<double>(node / kGridSide) + 0.5)
                * cell_width_m_;
            rotated_x[static_cast<std::size_t>(node)] =
                cosine * x - sine * y;
            rotated_y[static_cast<std::size_t>(node)] =
                sine * x + cosine * y;
        }
        const std::size_t offset =
            wind * kCandidateCount * kCandidateCount;
        for (int downstream = 0; downstream < kCandidateCount; ++downstream) {
            for (int upstream = 0; upstream < kCandidateCount; ++upstream) {
                const double dy =
                    rotated_y[static_cast<std::size_t>(upstream)]
                    - rotated_y[static_cast<std::size_t>(downstream)];
                if (dy <= 0.0) continue;
                const double dx = std::abs(
                    rotated_x[static_cast<std::size_t>(downstream)]
                    - rotated_x[static_cast<std::size_t>(upstream)]
                );
                const double wake_radius = kRotorRadiusM + entrainment * dy;
                const double overlap =
                    circle_overlap(dx, wake_radius, kRotorRadiusM);
                const double deficit =
                    (2.0 / 3.0)
                    * (kRotorRadiusM * kRotorRadiusM)
                    / (wake_radius * wake_radius)
                    * overlap
                    / (std::numbers::pi * kRotorRadiusM * kRotorRadiusM);
                state_->squared_deficit[
                    offset
                    + static_cast<std::size_t>(downstream) * kCandidateCount
                    + static_cast<std::size_t>(upstream)
                ] = deficit * deficit;
            }
        }
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const std::string& Problem::case_id() const noexcept { return case_id_; }
const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}
int Problem::wind_profile() const noexcept { return wind_profile_; }
int Problem::landscape() const noexcept { return landscape_; }
int Problem::turbine_count() const noexcept { return turbine_count_; }
int Problem::available_count() const noexcept {
    return static_cast<int>(state_->available_nodes.size());
}
double Problem::cell_width_m() const noexcept { return cell_width_m_; }
double Problem::hub_height_m() const noexcept { return hub_height_m_; }
int Problem::wind_state_count() const noexcept {
    return static_cast<int>(state_->winds.size());
}
int Problem::paper_population() const noexcept { return kPaperPopulation; }
int Problem::paper_generations() const noexcept { return kPaperGenerations; }
int Problem::paper_monte_carlo_layouts() const noexcept {
    return kPaperMonteCarloLayouts;
}
int Problem::paper_repeats() const noexcept { return kPaperRepeats; }

Evaluation Problem::evaluate(const Layout& layout) const {
    return evaluate_layout(layout, *state_, turbine_count_);
}

SurrogateSnapshot Problem::train_surrogate(
    const int monte_carlo_layouts,
    const std::uint64_t seed,
    const int workers
) const {
    if (monte_carlo_layouts < 1 || workers < 1) {
        throw std::invalid_argument("invalid L0259 surrogate work");
    }
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const auto cached = train_cached(
        *state_,
        turbine_count_,
        monte_carlo_layouts,
        seed,
        executor
    );
    const auto receipt = executor.work_receipt();
    SurrogateSnapshot result;
    result.training_targets_kw = cached->surface.training_targets_kw;
    result.predictions_kw = cached->surface.predictions_kw;
    result.observed_workers = receipt.distinct_participants;
    result.monte_carlo_truth_seconds = cached->monte_carlo_truth_seconds;
    result.training_seconds = cached->training_seconds;
    return result;
}

RunResult Problem::optimize(const RunConfig& config) const {
    const int monte_carlo_layouts = config.monte_carlo_layouts < 0
        ? kPaperMonteCarloLayouts : config.monte_carlo_layouts;
    const int population_size = config.population < 0
        ? kPaperPopulation : config.population;
    const int generations = config.generations < 0
        ? kPaperGenerations : config.generations;
    if (
        config.workers < 1 || monte_carlo_layouts < 1
        || population_size < 2 || generations < 1
        || config.variant != variant_
    ) {
        throw std::invalid_argument("invalid L0259 run configuration");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    static std::mutex cache_mutex;
    static std::map<
        std::string,
        std::shared_ptr<const CachedSurrogate>
    > cache;
    const std::string cache_key =
        case_id_ + ":" + variant_ + ":" + std::to_string(monte_carlo_layouts);
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
        cached = train_cached(
            *state_,
            turbine_count_,
            monte_carlo_layouts,
            training_seed(
                wind_profile_,
                landscape_,
                turbine_count_,
                variant_
            ),
            executor
        );
        if (config.reuse_surrogate) {
            const std::lock_guard lock(cache_mutex);
            cache[cache_key] = cached;
        }
    }
    const auto& surface = cached->surface;
    const fode::CounterRng random(config.seed);
    std::vector<Layout> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](const int individual) {
        population[static_cast<std::size_t>(individual)] = random_layout(
            *state_,
            turbine_count_,
            random,
            0,
            static_cast<std::uint64_t>(individual),
            20
        );
    });
    std::vector<Evaluation> evaluations(
        static_cast<std::size_t>(population_size)
    );
    std::vector<double> history;
    history.reserve(static_cast<std::size_t>(generations));
    Evaluation initial_best;
    Evaluation best;
    Layout best_layout;
    double population_truth_seconds = 0.0;
    const auto algorithm_start = Clock::now();
    for (int generation = 0; generation < generations; ++generation) {
        const auto truth_start = Clock::now();
        executor.parallel_for(0, population_size, [&](const int individual) {
            evaluations[static_cast<std::size_t>(individual)] =
                evaluate_layout(
                    population[static_cast<std::size_t>(individual)],
                    *state_,
                    turbine_count_
                );
        });
        population_truth_seconds += elapsed_seconds(truth_start);
        std::vector<int> order(static_cast<std::size_t>(population_size));
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
        std::vector<Layout> sorted_population(
            static_cast<std::size_t>(population_size)
        );
        std::vector<Evaluation> sorted_evaluations(
            static_cast<std::size_t>(population_size)
        );
        for (int index = 0; index < population_size; ++index) {
            const int source = order[static_cast<std::size_t>(index)];
            sorted_population[static_cast<std::size_t>(index)] =
                population[static_cast<std::size_t>(source)];
            sorted_evaluations[static_cast<std::size_t>(index)] =
                evaluations[static_cast<std::size_t>(source)];
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
                *state_,
                random,
                generation,
                30,
                individual,
                0
            );
            const bool choose_guided = !probability_event(
                random,
                variant_,
                kInformedRate,
                generation,
                31,
                individual
            );
            if (choose_guided) {
                double best_prediction =
                    -std::numeric_limits<double>::infinity();
                for (int candidate_index = 0;
                     candidate_index < kSvrCandidateLocations;
                     ++candidate_index) {
                    const int candidate = random_empty_node(
                        layout,
                        *state_,
                        random,
                        generation,
                        32,
                        individual,
                        candidate_index
                    );
                    const double prediction = surface.predictions_kw[
                        static_cast<std::size_t>(candidate)
                    ];
                    if (prediction > best_prediction) {
                        best_prediction = prediction;
                        destination = candidate;
                    }
                }
            }
            layout = replace_node(layout, worst_node, destination);
        });

        std::vector<int> parents;
        const int elite_count = std::max(
            2,
            static_cast<int>(population_size * kEliteRate)
        );
        for (int index = 0; index < elite_count; ++index) {
            parents.push_back(index);
        }
        for (int index = elite_count; index < population_size; ++index) {
            if (probability_event(
                    random,
                    variant_,
                    kSelectionRate,
                    generation,
                    50,
                    index
                )) {
                parents.push_back(index);
            }
        }
        std::vector<Layout> next_population(
            static_cast<std::size_t>(population_size)
        );
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
            if (second == first) {
                second = (second + 1) % static_cast<int>(parents.size());
            }
            Layout child = crossover_layout(
                population[static_cast<std::size_t>(
                    parents[static_cast<std::size_t>(first)]
                )],
                population[static_cast<std::size_t>(
                    parents[static_cast<std::size_t>(second)]
                )],
                *state_,
                turbine_count_,
                random,
                generation,
                individual
            );
            if (probability_event(
                    random,
                    variant_,
                    kMutationRate,
                    generation,
                    70,
                    individual
                )) {
                const int removed_index = random.integer(
                    0,
                    turbine_count_,
                    generation,
                    71,
                    individual
                );
                const int removed =
                    child[static_cast<std::size_t>(removed_index)];
                const int inserted = random_empty_node(
                    child,
                    *state_,
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
    const double algorithm_elapsed = elapsed_seconds(algorithm_start);
    const auto receipt = executor.work_receipt();
    RunResult result;
    result.case_id = case_id_;
    result.problem_semantic_id = semantic_id_;
    result.method_semantic_id = variant_ == "paper_probability"
        ? "l0259_sugga_paper_probability_v1"
        : "l0259_sugga_source_normal_threshold_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.monte_carlo_layouts = monte_carlo_layouts;
    result.population = population_size;
    result.generations = generations;
    result.surrogate_reused = surrogate_reused;
    result.physical_fes =
        static_cast<std::uint64_t>(
            surrogate_reused ? 0 : monte_carlo_layouts
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
        surrogate_reused ? 0.0 : cached->training_seconds;
    result.population_truth_seconds = population_truth_seconds;
    result.algorithm_seconds = std::max(
        0.0,
        algorithm_elapsed - population_truth_seconds
    );
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
    result.reserve(117);
    for (int wind = 1; wind <= 3; ++wind) {
        for (int landscape = 0; landscape <= 12; ++landscape) {
            for (const int turbines : {15, 20, 25}) {
                result.push_back(
                    "l0259_d" + std::to_string(wind)
                    + "_l" + std::to_string(landscape)
                    + "_n" + std::to_string(turbines)
                );
            }
        }
    }
    return result;
}

Layout regular_reference_layout(const Problem& problem) {
    const auto unavailable = unavailable_one_based(problem.landscape());
    std::array<bool, kCandidateCount> allowed{};
    allowed.fill(true);
    for (const int one_based : unavailable) {
        allowed[static_cast<std::size_t>(one_based - 1)] = false;
    }
    Layout result;
    for (int node = 0; node < kCandidateCount; ++node) {
        if (allowed[static_cast<std::size_t>(node)]) {
            result.push_back(node);
            if (
                result.size()
                == static_cast<std::size_t>(problem.turbine_count())
            ) {
                return result;
            }
        }
    }
    throw std::runtime_error("L0259 reference layout construction failed");
}

}  // namespace core99::l0259
