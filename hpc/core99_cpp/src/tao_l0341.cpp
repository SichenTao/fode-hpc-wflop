/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0341 pure-C++ 3-D Gaussian evaluator and MDPSO
Paper: Tao et al., 10.1016/j.renene.2020.06.003.
Public source: no target code/data; direct MDPSO and wake predecessors were
legally recovered and are identified in the controlling contract.
Missing fields: Fig. 10-11 arrays, calibration, initialization/seeds/repeats,
velocity limits, cardinality encoding and exact constraint handling.
Reconstruction: declared power/JPDF/model completions, inactive-slot
nonuniform encoding, nearest-domain projection and Deb comparison.
Semantic IDs: l0341_three_farm_3d_gaussian_v1;
l0341_mdpso_predecessor_completed_v1.
Contract: shared/contracts/core99_l0341_tao_3d_mdpso_2020.json.
Claim boundary: academic declared reproduction, not author source, exact
private figure arrays/curves, variable-cardinality encoding or replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/tao_l0341.hpp"

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

namespace core99::l0341 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double roughness_m = 0.3;
constexpr double wake_expansion = 0.075;
constexpr double gaussian_c = 5.15;
constexpr double axial_induction = 1.0 / 3.0;
constexpr double source_calibrated_deficit_scale = 2.10;
constexpr double inertia = 0.5;
constexpr double local_coefficient = 1.4;
constexpr double global_coefficient = 1.4;
constexpr double diversity_coefficient = 10.0;

struct TurbineSpec {
    double rated_mw;
    double cut_in_mps;
    double rated_mps;
    double cut_out_mps;
    double diameter_m;
    double hub_height_m;
};

constexpr std::array<TurbineSpec, 5> specs{{
    {0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
    {1.0, 3.0, 9.0, 25.0, 90.3, 90.0},
    {3.0, 3.0, 12.0, 22.5, 126.0, 119.0},
    {5.0, 1.5, 13.0, 27.0, 132.0, 140.0},
    {2.0, 3.0, 11.0, 22.0, 100.0, 100.0},
}};

constexpr std::array<double, 9> tail_direction_weights{
    1.549, 1.841, 2.132, 3.395, 4.029, 3.395, 2.132, 1.841, 1.549
};
constexpr std::array<std::array<double, 3>, 9> tail_speed_weights{{
    {0.836, 0.578, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 1.161, 0.135},
    {0.836, 1.128, 1.431},
    {0.836, 1.762, 1.431},
    {0.836, 1.128, 1.431},
    {0.836, 1.161, 0.135},
    {0.836, 0.870, 0.135},
    {0.836, 0.578, 0.135},
}};

struct DirectionState {
    double radians = 0.0;
    std::array<double, 3> speeds_mps{};
    std::array<double, 3> weights{};
    int speed_count = 0;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

double power_mw(const int type, const double speed_mps) {
    const auto& turbine = specs[static_cast<std::size_t>(type)];
    if (
        speed_mps < turbine.cut_in_mps
        || speed_mps >= turbine.cut_out_mps
    ) {
        return 0.0;
    }
    if (speed_mps >= turbine.rated_mps) return turbine.rated_mw;
    const double midpoint = turbine.cut_in_mps
        + 0.6 * (turbine.rated_mps - turbine.cut_in_mps);
    const double slope = 8.0
        / (turbine.rated_mps - turbine.cut_in_mps);
    const double raw = turbine.rated_mw
        / (1.0 + std::exp(-slope * (speed_mps - midpoint)));
    const double at_cut_in = turbine.rated_mw
        / (
            1.0
            + std::exp(-slope * (turbine.cut_in_mps - midpoint))
        );
    return std::max(
        0.0,
        turbine.rated_mw * (raw - at_cut_in)
        / (turbine.rated_mw - at_cut_in)
    );
}

double relative_shear_factor(
    const double height_m,
    const double hub_height_m
) {
    return std::log(std::max(height_m, roughness_m + 1.0e-9) / roughness_m)
        / std::log(hub_height_m / roughness_m);
}

std::vector<DirectionState> wind_states(const std::string& scenario) {
    std::vector<DirectionState> result;
    if (scenario == "a") {
        DirectionState state;
        state.radians = 270.0 * std::numbers::pi / 180.0;
        state.speeds_mps[0] = 15.0;
        state.weights[0] = 1.0;
        state.speed_count = 1;
        result.push_back(state);
        return result;
    }
    if (scenario == "b") {
        for (int direction = 0; direction < 36; ++direction) {
            DirectionState state;
            state.radians =
                direction * 10.0 * std::numbers::pi / 180.0;
            state.speeds_mps[0] = 15.0;
            state.weights[0] = 1.0 / 36.0;
            state.speed_count = 1;
            result.push_back(state);
        }
        return result;
    }
    if (scenario != "c") {
        throw std::invalid_argument("unknown L0341 wind scenario");
    }
    double total = 0.0;
    for (int direction = 0; direction < 27; ++direction) {
        total += 0.836 + 0.292 + 0.135;
    }
    total += std::accumulate(
        tail_direction_weights.begin(), tail_direction_weights.end(), 0.0
    );
    constexpr std::array<double, 3> speeds{8.0, 12.0, 17.0};
    for (int direction = 0; direction < 36; ++direction) {
        DirectionState state;
        state.radians =
            direction * 10.0 * std::numbers::pi / 180.0;
        state.speeds_mps = speeds;
        const auto raw = direction < 27
            ? std::array<double, 3>{0.836, 0.292, 0.135}
            : tail_speed_weights[static_cast<std::size_t>(direction - 27)];
        for (int speed = 0; speed < 3; ++speed) {
            state.weights[static_cast<std::size_t>(speed)] =
                raw[static_cast<std::size_t>(speed)] / total;
        }
        state.speed_count = 3;
        result.push_back(state);
    }
    return result;
}

int nth_prime(const int index) {
    int found = -1;
    for (int candidate = 2;; ++candidate) {
        bool prime = true;
        for (
            int divisor = 2;
            divisor * divisor <= candidate;
            ++divisor
        ) {
            if (candidate % divisor == 0) {
                prime = false;
                break;
            }
        }
        if (prime && ++found == index) return candidate;
    }
}

double radical_inverse(std::uint64_t value, const int base) {
    double result = 0.0;
    double factor = 1.0 / base;
    while (value != 0U) {
        result += factor * static_cast<double>(value % base);
        value /= static_cast<std::uint64_t>(base);
        factor /= base;
    }
    return result;
}

bool feasible_better(
    const Evaluation& left,
    const std::vector<double>& left_position,
    const Evaluation& right,
    const std::vector<double>& right_position
) {
    if (left.feasible != right.feasible) return left.feasible;
    if (!left.feasible) {
        if (left.constraint_violation != right.constraint_violation) {
            return left.constraint_violation < right.constraint_violation;
        }
    } else if (left.expected_power_mw != right.expected_power_mw) {
        return left.expected_power_mw > right.expected_power_mw;
    }
    return left_position < right_position;
}

}  // namespace

struct Problem::Impl {
    std::string case_id;
    bool is_nonuniform = false;
    std::string scenario;
    double farm_width_m = 0.0;
    double farm_height_m = 0.0;
    double target_capacity_mw = 0.0;
    int max_slots = 0;
    int dimensions = 0;
    int max_iterations = 0;
    std::vector<DirectionState> directions;

    [[nodiscard]] std::vector<Turbine> decode(
        const std::vector<double>& position
    ) const {
        std::vector<Turbine> layout(static_cast<std::size_t>(max_slots));
        if (!is_nonuniform) {
            const int type = std::clamp(
                static_cast<int>(std::llround(position[0])), 1, 3
            );
            const int active = std::clamp(
                static_cast<int>(std::llround(
                    target_capacity_mw
                    / specs[static_cast<std::size_t>(type)].rated_mw
                )),
                1,
                max_slots
            );
            for (int slot = 0; slot < max_slots; ++slot) {
                auto& turbine = layout[static_cast<std::size_t>(slot)];
                turbine.type = slot < active ? type : 0;
                turbine.x_m = position[static_cast<std::size_t>(1 + slot)];
                turbine.y_m = position[
                    static_cast<std::size_t>(1 + max_slots + slot)
                ];
            }
            return layout;
        }
        double capacity = 0.0;
        for (int slot = 0; slot < max_slots; ++slot) {
            auto& turbine = layout[static_cast<std::size_t>(slot)];
            turbine.type = std::clamp(
                static_cast<int>(std::llround(
                    position[static_cast<std::size_t>(3 * slot)]
                )),
                0,
                3
            );
            turbine.x_m = position[static_cast<std::size_t>(3 * slot + 1)];
            turbine.y_m = position[static_cast<std::size_t>(3 * slot + 2)];
            capacity += specs[static_cast<std::size_t>(turbine.type)].rated_mw;
        }
        while (capacity > target_capacity_mw + 0.5) {
            bool changed = false;
            const double excess = capacity - target_capacity_mw;
            for (auto& turbine : layout) {
                const int old_type = turbine.type;
                if (old_type == 0) continue;
                int new_type = old_type == 3 ? 2 : old_type == 2 ? 1 : 0;
                const double reduction =
                    specs[static_cast<std::size_t>(old_type)].rated_mw
                    - specs[static_cast<std::size_t>(new_type)].rated_mw;
                if (reduction <= excess + 1.0e-9) {
                    turbine.type = new_type;
                    capacity -= reduction;
                    changed = true;
                    break;
                }
            }
            if (!changed) break;
        }
        while (capacity < target_capacity_mw - 0.5) {
            bool changed = false;
            const double shortage = target_capacity_mw - capacity;
            for (auto& turbine : layout) {
                const int old_type = turbine.type;
                if (old_type == 3) continue;
                int new_type = old_type == 0 ? 1 : old_type == 1 ? 2 : 3;
                const double increment =
                    specs[static_cast<std::size_t>(new_type)].rated_mw
                    - specs[static_cast<std::size_t>(old_type)].rated_mw;
                if (increment <= shortage + 1.0e-9) {
                    turbine.type = new_type;
                    capacity += increment;
                    changed = true;
                    break;
                }
            }
            if (!changed) break;
        }
        return layout;
    }

    void project_discrete(std::vector<double>& position) const {
        if (!is_nonuniform) {
            position[0] = std::clamp(std::round(position[0]), 1.0, 3.0);
            return;
        }
        auto layout = decode(position);
        for (int slot = 0; slot < max_slots; ++slot) {
            position[static_cast<std::size_t>(3 * slot)] =
                layout[static_cast<std::size_t>(slot)].type;
        }
    }

    [[nodiscard]] Evaluation evaluate_layout(
        const std::vector<Turbine>& all_slots
    ) const {
        std::vector<Turbine> layout;
        layout.reserve(all_slots.size());
        Evaluation result;
        for (const auto& turbine : all_slots) {
            if (turbine.type == 0) continue;
            layout.push_back(turbine);
            result.installed_capacity_mw +=
                specs[static_cast<std::size_t>(turbine.type)].rated_mw;
            if (
                turbine.x_m < 0.0 || turbine.x_m > farm_width_m
                || turbine.y_m < 0.0 || turbine.y_m > farm_height_m
            ) {
                result.constraint_violation += 1.0;
            }
        }
        result.active_turbines = static_cast<int>(layout.size());
        result.constraint_violation += std::abs(
            result.installed_capacity_mw - target_capacity_mw
        ) / target_capacity_mw;
        result.minimum_spacing_margin_m =
            std::numeric_limits<double>::infinity();
        for (std::size_t first = 0; first < layout.size(); ++first) {
            for (
                std::size_t second = first + 1;
                second < layout.size();
                ++second
            ) {
                const double distance = std::hypot(
                    layout[first].x_m - layout[second].x_m,
                    layout[first].y_m - layout[second].y_m
                );
                const double required = 5.0 * std::max(
                    specs[static_cast<std::size_t>(layout[first].type)]
                        .diameter_m,
                    specs[static_cast<std::size_t>(layout[second].type)]
                        .diameter_m
                );
                const double margin = distance - required;
                result.minimum_spacing_margin_m = std::min(
                    result.minimum_spacing_margin_m, margin
                );
                if (distance + 1.0e-9 < required) {
                    result.constraint_violation +=
                        (required - distance)
                        / required;
                }
            }
        }
        result.feasible =
            result.constraint_violation <= 1.0e-12 && !layout.empty();
        if (!result.feasible) return result;
        double ideal_power = 0.0;
        std::vector<std::array<double, 3>> point_multipliers(layout.size());
        for (const auto& direction : directions) {
            const double sine = std::sin(direction.radians);
            const double cosine = std::cos(direction.radians);
            for (std::size_t target = 0; target < layout.size(); ++target) {
                const auto& target_spec = specs[
                    static_cast<std::size_t>(layout[target].type)
                ];
                constexpr std::array<double, 3> vertical_fraction{
                    -0.5, 0.0, 0.5
                };
                constexpr std::array<double, 3> rotor_weights{
                    0.25, 0.5, 0.25
                };
                double cube_mean = 0.0;
                for (int point = 0; point < 3; ++point) {
                    const double point_height = target_spec.hub_height_m
                        + vertical_fraction[static_cast<std::size_t>(point)]
                            * 0.5 * target_spec.diameter_m;
                    double deficit_square_sum = 0.0;
                    for (
                        std::size_t source = 0;
                        source < layout.size();
                        ++source
                    ) {
                        if (source == target) continue;
                        const double dx =
                            layout[target].x_m - layout[source].x_m;
                        const double dy =
                            layout[target].y_m - layout[source].y_m;
                        const double downstream = sine * dx + cosine * dy;
                        if (!(downstream > 0.0)) continue;
                        const double crosswind = cosine * dx - sine * dy;
                        const auto& source_spec = specs[
                            static_cast<std::size_t>(layout[source].type)
                        ];
                        const double wake_radius =
                            0.5 * source_spec.diameter_m
                            + wake_expansion * downstream;
                        const double vertical =
                            point_height - source_spec.hub_height_m;
                        const double radial2 =
                            crosswind * crosswind + vertical * vertical;
                        if (radial2 >= wake_radius * wake_radius) continue;
                        const double sigma = wake_radius / gaussian_c;
                        const double boundary = std::exp(
                            -0.5 * gaussian_c * gaussian_c
                        );
                        const double gaussian = std::exp(
                            -radial2 / (2.0 * sigma * sigma)
                        );
                        const double center = 2.0 * axial_induction
                            * std::pow(
                                0.5 * source_spec.diameter_m / wake_radius,
                                2.0
                            );
                        const double deficit = std::min(
                            0.95,
                            source_calibrated_deficit_scale * center
                            * std::max(0.0, gaussian - boundary)
                            / (1.0 - boundary)
                        );
                        deficit_square_sum += deficit * deficit;
                    }
                    const double multiplier = relative_shear_factor(
                        point_height, target_spec.hub_height_m
                    )
                        * std::max(
                            0.05, 1.0 - std::sqrt(deficit_square_sum)
                        );
                    cube_mean += rotor_weights[
                        static_cast<std::size_t>(point)
                    ] * multiplier * multiplier * multiplier;
                }
                point_multipliers[target][0] = std::cbrt(cube_mean);
            }
            for (int speed = 0; speed < direction.speed_count; ++speed) {
                const double probability =
                    direction.weights[static_cast<std::size_t>(speed)];
                const double reference_speed =
                    direction.speeds_mps[static_cast<std::size_t>(speed)];
                for (std::size_t target = 0; target < layout.size(); ++target) {
                    const int type = layout[target].type;
                    const auto& target_spec =
                        specs[static_cast<std::size_t>(type)];
                    if (
                        reference_speed >= target_spec.cut_in_mps
                        && reference_speed < target_spec.cut_out_mps
                    ) {
                        result.expected_power_mw += probability * power_mw(
                            type,
                            reference_speed * point_multipliers[target][0]
                        );
                    }
                    ideal_power += probability * power_mw(
                        type, reference_speed
                    );
                }
            }
        }
        result.capacity_factor_percent = 100.0 * result.expected_power_mw
            / result.installed_capacity_mw;
        result.efficiency_percent = ideal_power > 0.0
            ? 100.0 * result.expected_power_mw / ideal_power : 0.0;
        return result;
    }
};

Problem::Problem(std::string case_id) : impl_(std::make_unique<Impl>()) {
    impl_->case_id = std::move(case_id);
    if (!impl_->case_id.starts_with("l0341_")) {
        throw std::invalid_argument("unknown L0341 case prefix");
    }
    impl_->is_nonuniform =
        impl_->case_id.find("_nonuniform_") != std::string::npos;
    if (
        !impl_->is_nonuniform
        && impl_->case_id.find("_uniform_") == std::string::npos
    ) {
        throw std::invalid_argument("unknown L0341 design class");
    }
    if (impl_->case_id.find("_wfa_") != std::string::npos) {
        impl_->farm_width_m = 4000.0;
        impl_->farm_height_m = 4000.0;
        impl_->target_capacity_mw = 25.0;
    } else if (impl_->case_id.find("_wfb_") != std::string::npos) {
        impl_->farm_width_m = 6000.0;
        impl_->farm_height_m = 6000.0;
        impl_->target_capacity_mw = 45.0;
    } else if (impl_->case_id.find("_wfc_") != std::string::npos) {
        impl_->farm_width_m = 4000.0;
        impl_->farm_height_m = 10000.0;
        impl_->target_capacity_mw = 60.0;
    } else {
        throw std::invalid_argument("unknown L0341 farm");
    }
    const std::size_t separator = impl_->case_id.rfind('_');
    impl_->scenario = impl_->case_id.substr(separator + 1);
    if (
        impl_->scenario != "a"
        && impl_->scenario != "b"
        && impl_->scenario != "c"
    ) {
        throw std::invalid_argument("unknown L0341 scenario suffix");
    }
    impl_->max_slots = static_cast<int>(impl_->target_capacity_mw);
    impl_->dimensions = impl_->is_nonuniform
        ? 3 * impl_->max_slots : 2 * impl_->max_slots + 1;
    impl_->max_iterations = impl_->is_nonuniform ? 20000 : 15000;
    impl_->directions = wind_states(impl_->scenario);
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
const std::string& Problem::case_id() const noexcept {
    return impl_->case_id;
}
const std::string& Problem::semantic_id() const noexcept {
    static const std::string value = "l0341_three_farm_3d_gaussian_v1";
    return value;
}
bool Problem::nonuniform() const noexcept {
    return impl_->is_nonuniform;
}
const std::string& Problem::wind_scenario() const noexcept {
    return impl_->scenario;
}
int Problem::maximum_slots() const noexcept {
    return impl_->max_slots;
}
int Problem::decision_dimension() const noexcept {
    return impl_->dimensions;
}
int Problem::paper_population() const noexcept {
    return 20 * impl_->dimensions;
}
int Problem::paper_generations() const noexcept {
    return impl_->max_iterations;
}
double Problem::width_m() const noexcept {
    return impl_->farm_width_m;
}
double Problem::height_m() const noexcept {
    return impl_->farm_height_m;
}
double Problem::capacity_mw() const noexcept {
    return impl_->target_capacity_mw;
}
Evaluation Problem::evaluate(const std::vector<Turbine>& layout) const {
    return impl_->evaluate_layout(layout);
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers <= 0) {
        throw std::invalid_argument("L0341 workers must be positive");
    }
    const int generations = config.generations >= 0
        ? config.generations : paper_generations();
    const int population_count = config.population_override > 0
        ? config.population_override : paper_population();
    if (generations < 0 || population_count < 2) {
        throw std::invalid_argument("invalid L0341 optimization budget");
    }
    struct Particle {
        std::vector<double> position;
        std::vector<double> velocity;
        std::vector<double> personal_best;
        Evaluation evaluation;
        Evaluation personal_best_evaluation;
    };
    const fode::CounterRng random(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::vector<int> primes(static_cast<std::size_t>(impl_->dimensions));
    for (int dimension = 0; dimension < impl_->dimensions; ++dimension) {
        primes[static_cast<std::size_t>(dimension)] = nth_prime(dimension);
    }
    auto bounds = [&](const int dimension) {
        if (!impl_->is_nonuniform) {
            if (dimension == 0) return std::pair{1.0, 3.0};
            if (dimension <= impl_->max_slots) {
                return std::pair{0.0, impl_->farm_width_m};
            }
            return std::pair{0.0, impl_->farm_height_m};
        }
        const int component = dimension % 3;
        if (component == 0) return std::pair{0.0, 3.0};
        if (component == 1) return std::pair{0.0, impl_->farm_width_m};
        return std::pair{0.0, impl_->farm_height_m};
    };
    std::vector<Particle> particles(static_cast<std::size_t>(population_count));
    for (int particle = 0; particle < population_count; ++particle) {
        auto& value = particles[static_cast<std::size_t>(particle)];
        value.position.resize(static_cast<std::size_t>(impl_->dimensions));
        value.velocity.resize(static_cast<std::size_t>(impl_->dimensions));
        for (int dimension = 0; dimension < impl_->dimensions; ++dimension) {
            const auto [lower, upper] = bounds(dimension);
            double unit = radical_inverse(
                static_cast<std::uint64_t>(particle + 1),
                primes[static_cast<std::size_t>(dimension)]
            );
            unit = std::fmod(
                unit + random.uniform(0, 300, dimension), 1.0
            );
            value.position[static_cast<std::size_t>(dimension)] =
                lower + unit * (upper - lower);
            value.velocity[static_cast<std::size_t>(dimension)] =
                0.05 * (upper - lower) * random.normal(
                    0, 301, particle, dimension
                );
        }
        impl_->project_discrete(value.position);
    }
    double evaluator_seconds = 0.0;
    auto evaluate_all = [&]() {
        const auto started = Clock::now();
        executor.parallel_for(0, population_count, [&](const int index) {
            auto& particle = particles[static_cast<std::size_t>(index)];
            particle.evaluation = impl_->evaluate_layout(
                impl_->decode(particle.position)
            );
        });
        evaluator_seconds += elapsed_seconds(started);
    };
    const auto started = Clock::now();
    evaluate_all();
    int global_index = 0;
    for (int particle = 0; particle < population_count; ++particle) {
        auto& value = particles[static_cast<std::size_t>(particle)];
        value.personal_best = value.position;
        value.personal_best_evaluation = value.evaluation;
        if (feasible_better(
            value.personal_best_evaluation,
            value.personal_best,
            particles[static_cast<std::size_t>(global_index)]
                .personal_best_evaluation,
            particles[static_cast<std::size_t>(global_index)].personal_best
        )) {
            global_index = particle;
        }
    }
    std::vector<double> global_position =
        particles[static_cast<std::size_t>(global_index)].personal_best;
    Evaluation global_evaluation =
        particles[static_cast<std::size_t>(global_index)]
            .personal_best_evaluation;
    RunResult result;
    result.case_id = impl_->case_id;
    result.problem_semantic_id = semantic_id();
    result.method_semantic_id =
        "l0341_mdpso_predecessor_completed_v1";
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.population = population_count;
    result.generations = generations;
    result.physical_fes = population_count;
    result.initial_best = global_evaluation;
    result.best_power_history_mw.push_back(
        global_evaluation.expected_power_mw
    );
    for (int generation = 1; generation <= generations; ++generation) {
        const std::vector<double> generation_global = global_position;
        executor.parallel_for(0, population_count, [&](const int index) {
            auto& particle = particles[static_cast<std::size_t>(index)];
            for (int dimension = 0; dimension < impl_->dimensions; ++dimension) {
                const std::size_t slot = static_cast<std::size_t>(dimension);
                const auto [lower, upper] = bounds(dimension);
                double velocity =
                    inertia * particle.velocity[slot]
                    + local_coefficient * random.uniform(
                        generation, 310, index, dimension, 0
                    ) * (
                        particle.personal_best[slot]
                        - particle.position[slot]
                    )
                    + global_coefficient * random.uniform(
                        generation, 311, index, dimension, 0
                    ) * (
                        generation_global[slot] - particle.position[slot]
                    )
                    + diversity_coefficient * random.uniform(
                        generation, 312, index, dimension, 0
                    ) * (
                        particle.position[slot] - generation_global[slot]
                    );
                const double maximum_velocity = 0.5 * (upper - lower);
                velocity = std::clamp(
                    velocity, -maximum_velocity, maximum_velocity
                );
                double position = particle.position[slot] + velocity;
                if (position < lower || position > upper) {
                    position = std::clamp(position, lower, upper);
                    velocity *= -0.5;
                }
                particle.velocity[slot] = velocity;
                particle.position[slot] = position;
            }
            impl_->project_discrete(particle.position);
        });
        evaluate_all();
        result.physical_fes += static_cast<std::uint64_t>(population_count);
        for (int index = 0; index < population_count; ++index) {
            auto& particle = particles[static_cast<std::size_t>(index)];
            if (feasible_better(
                particle.evaluation,
                particle.position,
                particle.personal_best_evaluation,
                particle.personal_best
            )) {
                particle.personal_best = particle.position;
                particle.personal_best_evaluation = particle.evaluation;
            }
            if (feasible_better(
                particle.personal_best_evaluation,
                particle.personal_best,
                global_evaluation,
                global_position
            )) {
                global_position = particle.personal_best;
                global_evaluation = particle.personal_best_evaluation;
            }
        }
        result.best_power_history_mw.push_back(
            global_evaluation.expected_power_mw
        );
    }
    result.best_layout = impl_->decode(global_position);
    result.best_evaluation = global_evaluation;
    result.evaluator_seconds = evaluator_seconds;
    result.end_to_end_seconds = elapsed_seconds(started);
    result.algorithm_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    result.observed_workers = executor.work_receipt().distinct_participants;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const double power : result.best_power_history_mw) {
        const auto quantized = static_cast<std::int64_t>(
            std::llround(power * 1.0e8)
        );
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(quantized));
    }
    for (const auto& turbine : result.best_layout) {
        hash = mix_hash(hash, static_cast<std::uint64_t>(turbine.type));
        hash = mix_hash(
            hash,
            std::bit_cast<std::uint64_t>(
                static_cast<std::int64_t>(std::llround(turbine.x_m * 1.0e6))
            )
        );
        hash = mix_hash(
            hash,
            std::bit_cast<std::uint64_t>(
                static_cast<std::int64_t>(std::llround(turbine.y_m * 1.0e6))
            )
        );
    }
    result.scientific_hash = hash;
    return result;
}

std::vector<std::string> paper_case_ids() {
    return {
        "l0341_uniform_wfa_a",
        "l0341_uniform_wfa_b",
        "l0341_uniform_wfa_c",
        "l0341_uniform_wfb_c",
        "l0341_uniform_wfc_c",
        "l0341_nonuniform_wfa_a",
        "l0341_nonuniform_wfa_b",
        "l0341_nonuniform_wfa_c",
        "l0341_nonuniform_wfb_c",
        "l0341_nonuniform_wfc_c",
    };
}

std::vector<Turbine> diagnostic_4x4_layout() {
    std::vector<Turbine> result;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.push_back({
                4,
                static_cast<double>(column) * 500.0,
                static_cast<double>(row) * 500.0,
            });
        }
    }
    return result;
}

Evaluation evaluate_diagnostic_4x4(
    const double reference_speed_mps,
    const double direction_degrees
) {
    Problem::Impl model;
    model.case_id = "l0341_diagnostic_4x4";
    model.is_nonuniform = false;
    model.scenario = "diagnostic";
    model.farm_width_m = 1500.0;
    model.farm_height_m = 1500.0;
    model.target_capacity_mw = 32.0;
    model.max_slots = 16;
    DirectionState state;
    state.radians = direction_degrees * std::numbers::pi / 180.0;
    state.speeds_mps[0] = reference_speed_mps;
    state.weights[0] = 1.0;
    state.speed_count = 1;
    model.directions.push_back(state);
    return model.evaluate_layout(diagnostic_4x4_layout());
}

}  // namespace core99::l0341
