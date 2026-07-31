/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T76 pure-C++ directional restriction, heterogeneous
turbine physics, and all-core MPGA
Paper DOI: 10.1016/j.energy.2018.11.073
Public source: no target source or native problem arrays were located.
Cited MPGA lineage: 10.1016/j.jweia.2015.01.018 and the audited T62
implementation/contract in this repository.
Missing information, conflicts, reconstruction, semantic IDs, production
backend, controlling contract, and claim boundary:
include/core99/sun_t76.hpp
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sun_t76.hpp"

#include "fode/executor.hpp"
#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace core99::t76 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kFarmSideM = 4000.0;
constexpr double kAirDensity = 1.225;
constexpr double kWakeDecay = 0.075;
constexpr std::uint32_t kGeneMask = (1U << 20U) - 1U;

struct Individual {
    std::vector<std::uint32_t> genes;
    Evaluation evaluation;
};

double elapsed(const Clock::time_point& start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double circle_overlap(
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
        (distance * distance + first_radius * first_radius
            - second_radius * second_radius)
        / (2.0 * distance * first_radius),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (distance * distance + second_radius * second_radius
            - first_radius * first_radius)
        / (2.0 * distance * second_radius),
        -1.0,
        1.0
    ));
    const double triangle = 0.5 * std::sqrt(std::max(
        0.0,
        (-distance + first_radius + second_radius)
        * (distance + first_radius - second_radius)
        * (distance - first_radius + second_radius)
        * (distance + first_radius + second_radius)
    ));
    return first_radius * first_radius * first_angle
        + second_radius * second_radius * second_angle - triangle;
}

double axial_induction(const TurbineSpec& turbine) {
    const double area = std::numbers::pi * 0.25
        * turbine.diameter_m * turbine.diameter_m;
    const double cp = std::clamp(
        turbine.anchor_power_kw * 1000.0
        / (
            0.5 * kAirDensity * area
            * turbine.anchor_speed_mps * turbine.anchor_speed_mps
            * turbine.anchor_speed_mps
        ),
        0.02,
        0.58
    );
    double low = 0.0;
    double high = 1.0 / 3.0;
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double middle = 0.5 * (low + high);
        const double modeled =
            4.0 * middle * (1.0 - middle) * (1.0 - middle);
        if (modeled < cp) low = middle;
        else high = middle;
    }
    return 0.5 * (low + high);
}

double wrapped_distance_degrees(const double left, const double right) {
    double distance = std::abs(left - right);
    if (distance > 180.0) distance = 360.0 - distance;
    return distance;
}

std::vector<WindState> reconstructed_sha_chau_states() {
    const std::vector<double> speed_weights{
        0.032, 0.028, 0.061, 0.088, 0.107, 0.118, 0.121,
        0.109, 0.092, 0.071, 0.055, 0.040, 0.028, 0.018,
        0.010, 0.005, 0.003, 0.002, 0.0015, 0.0010, 0.0007,
        0.0005, 0.00035, 0.00025, 0.00015, 0.00010, 0.00005,
    };
    std::vector<double> direction_weights;
    direction_weights.reserve(36);
    for (int direction = 0; direction < 36; ++direction) {
        const double degrees = static_cast<double>(10 * direction);
        const auto mode = [&](const double centre, const double width) {
            const double z = wrapped_distance_degrees(degrees, centre)
                / width;
            return std::exp(-0.5 * z * z);
        };
        direction_weights.push_back(
            0.05 + 1.00 * mode(120.0, 20.0)
            + 0.70 * mode(250.0, 18.0)
            + 0.60 * mode(70.0, 16.0)
        );
    }
    const double speed_total = std::accumulate(
        speed_weights.begin(), speed_weights.end(), 0.0
    );
    const double direction_total = std::accumulate(
        direction_weights.begin(), direction_weights.end(), 0.0
    );
    std::vector<WindState> result;
    result.reserve(36U * speed_weights.size());
    for (int direction = 0; direction < 36; ++direction) {
        for (std::size_t speed = 0; speed < speed_weights.size(); ++speed) {
            result.push_back({
                .direction_deg = static_cast<double>(10 * direction),
                .reference_speed_mps = static_cast<double>(speed),
                .probability =
                    direction_weights[static_cast<std::size_t>(direction)]
                    / direction_total * speed_weights[speed] / speed_total,
            });
        }
    }
    return result;
}

bool better(const Individual& left, const Individual& right) {
    if (left.evaluation.feasible != right.evaluation.feasible) {
        return left.evaluation.feasible;
    }
    if (
        left.evaluation.boundary_violation_m
        != right.evaluation.boundary_violation_m
    ) {
        return left.evaluation.boundary_violation_m
            < right.evaluation.boundary_violation_m;
    }
    return left.evaluation.expected_power_mw
        > right.evaluation.expected_power_mw;
}

int best_local(
    const std::vector<Individual>& population,
    const int offset,
    const int count
) {
    int result = offset;
    for (int index = offset + 1; index < offset + count; ++index) {
        if (better(
                population[static_cast<std::size_t>(index)],
                population[static_cast<std::size_t>(result)]
            )) {
            result = index;
        }
    }
    return result;
}

int worst_local(
    const std::vector<Individual>& population,
    const int offset,
    const int count
) {
    int result = offset;
    for (int index = offset + 1; index < offset + count; ++index) {
        if (better(
                population[static_cast<std::size_t>(result)],
                population[static_cast<std::size_t>(index)]
            )) {
            result = index;
        }
    }
    return result;
}

int best_global(const std::vector<Individual>& population) {
    return best_local(population, 0, static_cast<int>(population.size()));
}

std::uint64_t hash_mix(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

std::uint64_t scientific_hash(
    const Individual& best,
    const int generations,
    const std::uint64_t physical_fes
) {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hash_mix(hash, static_cast<std::uint64_t>(generations));
    hash = hash_mix(hash, physical_fes);
    for (const std::uint32_t gene : best.genes) {
        hash = hash_mix(hash, gene);
    }
    hash = hash_mix(
        hash,
        std::bit_cast<std::uint64_t>(best.evaluation.expected_power_mw)
    );
    return hash;
}

}  // namespace

const std::vector<TurbineSpec>& turbine_catalog() {
    static const std::vector<TurbineSpec> result{
        {"E-126", 7580.0, 127.0, 3.0, 17.0, 25.0, 135.0,
            10.0, 3750.0},
        {"E-126 EP4", 4200.0, 127.0, 3.0, 14.0, 25.0, 135.0,
            10.0, 3097.0},
        {"E-101", 3050.0, 101.0, 2.0, 13.0, 25.0, 99.0,
            8.8, 1631.0},
        {"E-82", 2000.0, 82.0, 2.0, 13.0, 25.0, 78.0,
            8.0, 837.0},
        {"E-44", 900.0, 44.0, 3.0, 17.0, 25.0, 45.0,
            6.4, 119.0},
    };
    return result;
}

double completed_power_kw(
    const TurbineSpec& turbine,
    const double speed_mps
) {
    if (speed_mps <= turbine.cut_in_mps
        || speed_mps > turbine.cut_out_mps) {
        return 0.0;
    }
    if (speed_mps >= turbine.rated_speed_mps) {
        return turbine.rated_power_kw;
    }
    if (speed_mps <= turbine.anchor_speed_mps) {
        const double speed_cubed = speed_mps * speed_mps * speed_mps;
        const double cut_in_cubed = turbine.cut_in_mps
            * turbine.cut_in_mps * turbine.cut_in_mps;
        const double anchor_cubed = turbine.anchor_speed_mps
            * turbine.anchor_speed_mps * turbine.anchor_speed_mps;
        return turbine.anchor_power_kw
            * (speed_cubed - cut_in_cubed)
            / (anchor_cubed - cut_in_cubed);
    }
    const double speed_cubed = speed_mps * speed_mps * speed_mps;
    const double anchor_cubed = turbine.anchor_speed_mps
        * turbine.anchor_speed_mps * turbine.anchor_speed_mps;
    const double rated_cubed = turbine.rated_speed_mps
        * turbine.rated_speed_mps * turbine.rated_speed_mps;
    return turbine.anchor_power_kw
        + (turbine.rated_power_kw - turbine.anchor_power_kw)
            * (speed_cubed - anchor_cubed)
            / (rated_cubed - anchor_cubed);
}

std::string case_role_name(const CaseRole role) {
    switch (role) {
    case CaseRole::case1_omnidirectional_aligned:
        return "t76_case1_omnidirectional_aligned";
    case CaseRole::case1_directional_aligned:
        return "t76_case1_directional_aligned";
    case CaseRole::case2_omnidirectional_mpga:
        return "t76_case2_omnidirectional_mpga";
    case CaseRole::case2_directional_mpga:
        return "t76_case2_directional_mpga";
    case CaseRole::case3_directional_multitype_mpga:
        return "t76_case3_directional_multitype_mpga";
    case CaseRole::case4_sha_chau_multitype_mpga:
        return "t76_case4_sha_chau_multitype_mpga";
    }
    throw std::invalid_argument("T76 unknown case role");
}

Problem::Problem(const CaseRole role)
    : id_(case_role_name(role)), role_(role),
      restriction_(
          role == CaseRole::case1_omnidirectional_aligned
              || role == CaseRole::case2_omnidirectional_mpga
              ? Restriction::omnidirectional
              : Restriction::directional
      ) {
    if (
        role == CaseRole::case1_omnidirectional_aligned
        || role == CaseRole::case1_directional_aligned
        || role == CaseRole::case2_omnidirectional_mpga
        || role == CaseRole::case2_directional_mpga
    ) {
        turbine_count_ = 48;
        turbine_types_.assign(48U, 3);
        optimized_ = role == CaseRole::case2_omnidirectional_mpga
            || role == CaseRole::case2_directional_mpga;
        reference_height_m_ = 78.0;
        shear_exponent_ = 0.0;
        crosswind_ratio_ = 3.0;
        wind_states_ = {{0.0, 8.0, 1.0}};
    } else {
        turbine_count_ = 45;
        optimized_ = true;
        for (int type = 0; type < 5; ++type) {
            for (int count = 0; count < 9; ++count) {
                turbine_types_.push_back(type);
            }
        }
        if (role == CaseRole::case3_directional_multitype_mpga) {
            crosswind_ratio_ = 2.5;
            reference_height_m_ = 135.0;
            shear_exponent_ = 0.40;
            wind_states_ = {{0.0, 10.0, 1.0}};
        } else {
            crosswind_ratio_ = 3.0;
            reference_height_m_ = 31.0;
            shear_exponent_ = 0.10;
            wind_states_ = reconstructed_sha_chau_states();
        }
    }
    for (std::size_t state = 0; state < wind_states_.size(); ++state) {
        const double direction = wind_states_[state].direction_deg;
        auto found = std::find(
            direction_degrees_.begin(), direction_degrees_.end(), direction
        );
        if (found == direction_degrees_.end()) {
            direction_degrees_.push_back(direction);
            state_indices_by_direction_.push_back({});
            found = std::prev(direction_degrees_.end());
        }
        const std::size_t index = static_cast<std::size_t>(
            std::distance(direction_degrees_.begin(), found)
        );
        state_indices_by_direction_[index].push_back(
            static_cast<int>(state)
        );
    }
    const auto& catalog = turbine_catalog();
    ambient_speed_by_state_type_.resize(
        wind_states_.size() * catalog.size()
    );
    no_wake_power_by_state_type_.resize(
        wind_states_.size() * catalog.size()
    );
    for (std::size_t state = 0; state < wind_states_.size(); ++state) {
        for (std::size_t type = 0; type < catalog.size(); ++type) {
            const std::size_t index = state * catalog.size() + type;
            const double ambient = wind_states_[state].reference_speed_mps
                * std::pow(
                    catalog[type].hub_height_m / reference_height_m_,
                    shear_exponent_
                );
            ambient_speed_by_state_type_[index] = ambient;
            no_wake_power_by_state_type_[index] =
                completed_power_kw(catalog[type], ambient);
        }
    }
}

const std::string& Problem::id() const noexcept { return id_; }
CaseRole Problem::role() const noexcept { return role_; }
Restriction Problem::restriction() const noexcept { return restriction_; }
bool Problem::optimized() const noexcept { return optimized_; }
int Problem::turbine_count() const noexcept { return turbine_count_; }
double Problem::farm_side_m() const noexcept { return kFarmSideM; }
double Problem::directional_crosswind_ratio() const noexcept {
    return crosswind_ratio_;
}
double Problem::reference_height_m() const noexcept {
    return reference_height_m_;
}
double Problem::shear_exponent() const noexcept { return shear_exponent_; }
const std::vector<int>& Problem::turbine_types() const noexcept {
    return turbine_types_;
}
const std::vector<WindState>& Problem::wind_states() const noexcept {
    return wind_states_;
}

std::vector<Point> Problem::aligned_layout() const {
    if (optimized_) {
        throw std::logic_error("T76 optimized case has no aligned layout");
    }
    std::vector<Point> result;
    result.reserve(48);
    const int columns =
        restriction_ == Restriction::omnidirectional ? 8 : 16;
    const int rows = 48 / columns;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            result.push_back({
                kFarmSideM * static_cast<double>(column)
                    / static_cast<double>(columns - 1),
                kFarmSideM * static_cast<double>(row)
                    / static_cast<double>(rows - 1),
            });
        }
    }
    return result;
}

std::vector<Point> Problem::decode(
    const std::vector<std::uint32_t>& genes
) const {
    if (genes.size() != static_cast<std::size_t>(2 * turbine_count_)) {
        throw std::invalid_argument("T76 gene cardinality mismatch");
    }
    std::vector<Point> result(static_cast<std::size_t>(turbine_count_));
    for (int turbine = 0; turbine < turbine_count_; ++turbine) {
        result[static_cast<std::size_t>(turbine)] = {
            kFarmSideM * static_cast<double>(
                genes[static_cast<std::size_t>(2 * turbine)] & kGeneMask
            ) / static_cast<double>(kGeneMask),
            kFarmSideM * static_cast<double>(
                genes[static_cast<std::size_t>(2 * turbine + 1)] & kGeneMask
            ) / static_cast<double>(kGeneMask),
        };
    }
    return result;
}

Evaluation Problem::evaluate(const std::vector<Point>& layout) const {
    if (layout.size() != static_cast<std::size_t>(turbine_count_)) {
        throw std::invalid_argument("T76 layout cardinality mismatch");
    }
    Evaluation result;
    for (const Point& point : layout) {
        result.boundary_violation_m += std::max({
            0.0, -point.x_m, point.x_m - kFarmSideM
        });
        result.boundary_violation_m += std::max({
            0.0, -point.y_m, point.y_m - kFarmSideM
        });
    }
    const auto& catalog = turbine_catalog();
    static const std::vector<double> axial_by_type = [] {
        std::vector<double> result;
        result.reserve(turbine_catalog().size());
        for (const auto& type : turbine_catalog()) {
            result.push_back(axial_induction(type));
        }
        return result;
    }();
    std::vector<double> expected_by_turbine(
        static_cast<std::size_t>(turbine_count_), 0.0
    );
    for (std::size_t direction_index = 0;
         direction_index < direction_degrees_.size();
         ++direction_index) {
        const double radians =
            (90.0 - direction_degrees_[direction_index])
            * std::numbers::pi / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        std::vector<bool> active(
            static_cast<std::size_t>(turbine_count_), true
        );
        if (restriction_ == Restriction::omnidirectional) {
            for (int left = 0; left < turbine_count_; ++left) {
                for (int right = left + 1; right < turbine_count_; ++right) {
                    const auto& left_type = catalog[static_cast<std::size_t>(
                        turbine_types_[static_cast<std::size_t>(left)]
                    )];
                    const auto& right_type = catalog[static_cast<std::size_t>(
                        turbine_types_[static_cast<std::size_t>(right)]
                    )];
                    if (std::hypot(
                            layout[static_cast<std::size_t>(left)].x_m
                                - layout[static_cast<std::size_t>(right)].x_m,
                            layout[static_cast<std::size_t>(left)].y_m
                                - layout[static_cast<std::size_t>(right)].y_m
                        ) < 5.0 * std::max(
                            left_type.diameter_m, right_type.diameter_m
                        )) {
                        active[static_cast<std::size_t>(left)] = false;
                        active[static_cast<std::size_t>(right)] = false;
                    }
                }
            }
        } else {
            for (int source = 0; source < turbine_count_; ++source) {
                const std::size_t source_type_index =
                    static_cast<std::size_t>(turbine_types_[
                        static_cast<std::size_t>(source)
                    ]);
                const auto& source_type = catalog[source_type_index];
                for (int target = 0; target < turbine_count_; ++target) {
                    if (source == target) continue;
                    const double dx =
                        layout[static_cast<std::size_t>(target)].x_m
                        - layout[static_cast<std::size_t>(source)].x_m;
                    const double dy =
                        layout[static_cast<std::size_t>(target)].y_m
                        - layout[static_cast<std::size_t>(source)].y_m;
                    const double along = dx * cosine + dy * sine;
                    const double across = -dx * sine + dy * cosine;
                    if (
                        (std::hypot(dx, dy) < 1.0e-9)
                        || (
                            along > 0.0
                            && along < 5.0 * source_type.diameter_m
                            && std::abs(across)
                                < 0.5 * crosswind_ratio_
                                    * source_type.diameter_m
                        )
                    ) {
                        active[static_cast<std::size_t>(target)] = false;
                    }
                }
            }
        }
        std::vector<double> speed_factor(
            static_cast<std::size_t>(turbine_count_), 1.0
        );
        for (int target = 0; target < turbine_count_; ++target) {
            if (!active[static_cast<std::size_t>(target)]) {
                speed_factor[static_cast<std::size_t>(target)] = 0.0;
                continue;
            }
            const auto& target_type = catalog[static_cast<std::size_t>(
                turbine_types_[static_cast<std::size_t>(target)]
            )];
            double squared_deficit = 0.0;
            for (int source = 0; source < turbine_count_; ++source) {
                if (source == target
                    || !active[static_cast<std::size_t>(source)]) {
                    continue;
                }
                const double dx =
                    layout[static_cast<std::size_t>(target)].x_m
                    - layout[static_cast<std::size_t>(source)].x_m;
                const double dy =
                    layout[static_cast<std::size_t>(target)].y_m
                    - layout[static_cast<std::size_t>(source)].y_m;
                const double downstream = dx * cosine + dy * sine;
                if (downstream <= 0.0) continue;
                const double across = -dx * sine + dy * cosine;
                const std::size_t source_type_index =
                    static_cast<std::size_t>(turbine_types_[
                        static_cast<std::size_t>(source)
                    ]);
                const auto& source_type = catalog[source_type_index];
                const double source_radius = 0.5 * source_type.diameter_m;
                const double target_radius = 0.5 * target_type.diameter_m;
                const double wake_radius =
                    source_radius + kWakeDecay * downstream;
                const double centre_distance = std::hypot(
                    across,
                    target_type.hub_height_m - source_type.hub_height_m
                );
                const double fraction = circle_overlap(
                    wake_radius, target_radius, centre_distance
                ) / (std::numbers::pi * target_radius * target_radius);
                const double deficit = 2.0 * axial_by_type[source_type_index]
                    * source_radius * source_radius
                    / (wake_radius * wake_radius) * fraction;
                squared_deficit += deficit * deficit;
            }
            speed_factor[static_cast<std::size_t>(target)] = std::max(
                0.0, 1.0 - std::sqrt(squared_deficit)
            );
        }
        for (const int state_index :
             state_indices_by_direction_[direction_index]) {
            const auto& state = wind_states_[static_cast<std::size_t>(
                state_index
            )];
            for (int turbine = 0; turbine < turbine_count_; ++turbine) {
                const std::size_t type_index = static_cast<std::size_t>(
                    turbine_types_[static_cast<std::size_t>(turbine)]
                );
                const auto& type = catalog[type_index];
                const std::size_t ambient_index =
                    static_cast<std::size_t>(state_index) * catalog.size()
                    + type_index;
                const double ambient =
                    ambient_speed_by_state_type_[ambient_index];
                const double theoretical =
                    no_wake_power_by_state_type_[ambient_index];
                const double actual = active[static_cast<std::size_t>(turbine)]
                    ? completed_power_kw(
                        type,
                        ambient * speed_factor[static_cast<std::size_t>(turbine)]
                    )
                    : 0.0;
                result.theoretical_no_wake_power_mw +=
                    state.probability * theoretical / 1000.0;
                result.expected_power_mw +=
                    state.probability * actual / 1000.0;
                expected_by_turbine[static_cast<std::size_t>(turbine)] +=
                    state.probability * actual;
                if (!active[static_cast<std::size_t>(turbine)]) {
                    ++result.inactive_turbine_states;
                }
            }
        }
    }
    result.utilization_rate = result.expected_power_mw
        / std::max(1.0e-12, result.theoretical_no_wake_power_mw);
    result.minimum_turbine_power_kw = *std::min_element(
        expected_by_turbine.begin(), expected_by_turbine.end()
    );
    result.maximum_turbine_power_kw = *std::max_element(
        expected_by_turbine.begin(), expected_by_turbine.end()
    );
    result.feasible = result.boundary_violation_m <= 1.0e-12;
    return result;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const auto role : {
        CaseRole::case1_omnidirectional_aligned,
        CaseRole::case1_directional_aligned,
        CaseRole::case2_omnidirectional_mpga,
        CaseRole::case2_directional_mpga,
        CaseRole::case3_directional_multitype_mpga,
        CaseRole::case4_sha_chau_multitype_mpga,
    }) {
        result.push_back(case_role_name(role));
    }
    return result;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (!problem.optimized()) {
        throw std::invalid_argument("T76 aligned case is evaluation-only");
    }
    if (
        config.workers < 1 || config.demes < 1
        || config.individuals_per_deme < 2
        || config.unchanged_generations < 1
        || config.maximum_generations < 1
        || config.migration_period < 1
    ) {
        throw std::invalid_argument("T76 MPGA configuration invalid");
    }
    const auto started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    const fode::CounterRng rng(config.seed);
    const int deme_size = config.individuals_per_deme;
    const int population_size = config.demes * deme_size;
    const int gene_count = 2 * problem.turbine_count();
    const int offspring_per_deme = deme_size - 1;
    const int offspring_count = config.demes * offspring_per_deme;
    std::vector<Individual> population(
        static_cast<std::size_t>(population_size)
    );
    executor.parallel_for(0, population_size, [&](const int individual) {
        auto& genes = population[static_cast<std::size_t>(individual)].genes;
        genes.resize(static_cast<std::size_t>(gene_count));
        for (int gene = 0; gene < gene_count; ++gene) {
            genes[static_cast<std::size_t>(gene)] =
                static_cast<std::uint32_t>(rng.integer(
                    0, static_cast<int>(kGeneMask) + 1,
                    0, 7601, individual, gene
                ));
        }
    });
    double evaluator_seconds = 0.0;
    std::uint64_t physical_fes = 0;
    auto evaluate_all = [&](std::vector<Individual>& values) {
        const auto evaluation_started = Clock::now();
        executor.parallel_for(
            0, static_cast<int>(values.size()), [&](const int index) {
                auto& item = values[static_cast<std::size_t>(index)];
                item.evaluation = problem.evaluate(
                    problem.decode(item.genes)
                );
            }
        );
        evaluator_seconds += elapsed(evaluation_started);
        physical_fes += values.size();
    };
    evaluate_all(population);
    int best_index = best_global(population);
    double best_power = population[
        static_cast<std::size_t>(best_index)
    ].evaluation.expected_power_mw;
    int unchanged = 0;
    int generations = 0;
    while (
        generations < config.maximum_generations
        && unchanged < config.unchanged_generations
    ) {
        ++generations;
        std::vector<Individual> next(
            static_cast<std::size_t>(population_size)
        );
        for (int deme = 0; deme < config.demes; ++deme) {
            const int offset = deme * deme_size;
            next[static_cast<std::size_t>(offset)] = population[
                static_cast<std::size_t>(
                    best_local(population, offset, deme_size)
                )
            ];
        }
        executor.parallel_for(0, offspring_count, [&](const int task) {
            const int deme = task / offspring_per_deme;
            const int child_slot = task % offspring_per_deme + 1;
            const int offset = deme * deme_size;
            auto select = [&](const int draw) -> const Individual& {
                const int first = rng.integer(
                    0, deme_size, generations, 7602, task, draw, 0
                );
                const int second = rng.integer(
                    0, deme_size, generations, 7602, task, draw, 1
                );
                return population[static_cast<std::size_t>(
                    better(
                        population[static_cast<std::size_t>(offset + first)],
                        population[static_cast<std::size_t>(offset + second)]
                    ) ? offset + first : offset + second
                )];
            };
            const auto& first = select(0);
            const auto& second = select(1);
            auto& child = next[static_cast<std::size_t>(
                offset + child_slot
            )];
            child.genes = first.genes;
            const double fraction = config.demes == 1 ? 0.0
                : static_cast<double>(deme)
                    / static_cast<double>(config.demes - 1);
            const double crossover_probability = 0.7 + 0.2 * fraction;
            const double mutation_probability = 0.001 + 0.049 * fraction;
            const int bit_count = gene_count * 20;
            if (
                bit_count > 1
                && rng.uniform(generations, 7603, task) < crossover_probability
            ) {
                const int point = rng.integer(
                    1, bit_count, generations, 7604, task
                );
                const int boundary_gene = point / 20;
                const int boundary_bit = point % 20;
                if (boundary_bit != 0) {
                    const std::uint32_t mask =
                        (kGeneMask << static_cast<unsigned>(boundary_bit))
                        & kGeneMask;
                    child.genes[static_cast<std::size_t>(boundary_gene)] =
                        (child.genes[static_cast<std::size_t>(boundary_gene)]
                            & ~mask)
                        | (second.genes[
                            static_cast<std::size_t>(boundary_gene)
                        ] & mask);
                }
                const int first_full_gene = boundary_gene
                    + (boundary_bit == 0 ? 0 : 1);
                for (int gene = first_full_gene; gene < gene_count; ++gene) {
                    child.genes[static_cast<std::size_t>(gene)] =
                        second.genes[static_cast<std::size_t>(gene)];
                }
            }
            int position = -1;
            std::uint64_t draw = 0;
            const double log_survival = std::log1p(-mutation_probability);
            while (true) {
                const double uniform = std::max(
                    rng.uniform(generations, 7605, task, 0, draw++),
                    std::numeric_limits<double>::min()
                );
                const int gap = static_cast<int>(std::floor(
                    std::log1p(-uniform) / log_survival
                ));
                position += gap + 1;
                if (position >= bit_count) break;
                const int gene = position / 20;
                const int bit = position % 20;
                child.genes[static_cast<std::size_t>(gene)] ^=
                    1U << static_cast<unsigned>(bit);
            }
        });
        const auto evaluation_started = Clock::now();
        executor.parallel_for(0, offspring_count, [&](const int task) {
            const int deme = task / offspring_per_deme;
            const int child_slot = task % offspring_per_deme + 1;
            auto& child = next[static_cast<std::size_t>(
                deme * deme_size + child_slot
            )];
            child.evaluation = problem.evaluate(problem.decode(child.genes));
        });
        evaluator_seconds += elapsed(evaluation_started);
        physical_fes += static_cast<std::uint64_t>(offspring_count);
        if (generations % config.migration_period == 0) {
            std::vector<Individual> emigrants;
            emigrants.reserve(static_cast<std::size_t>(config.demes));
            for (int deme = 0; deme < config.demes; ++deme) {
                const int offset = deme * deme_size;
                emigrants.push_back(next[static_cast<std::size_t>(
                    best_local(next, offset, deme_size)
                )]);
            }
            for (int deme = 0; deme < config.demes; ++deme) {
                const int destination = (deme + 1) % config.demes;
                const int offset = destination * deme_size;
                next[static_cast<std::size_t>(
                    worst_local(next, offset, deme_size)
                )] = emigrants[static_cast<std::size_t>(deme)];
            }
        }
        population = std::move(next);
        best_index = best_global(population);
        const double current = population[
            static_cast<std::size_t>(best_index)
        ].evaluation.expected_power_mw;
        if (current > best_power + 1.0e-12) {
            best_power = current;
            unchanged = 0;
        } else {
            ++unchanged;
        }
    }
    best_index = best_global(population);
    const double end_to_end = elapsed(started);
    const auto& best = population[static_cast<std::size_t>(best_index)];
    return {
        .case_id = problem.id(),
        .method_semantic_id =
            "t76_mpga_directional_heterogeneous_declared_v1",
        .problem_semantic_id =
            "t76_fourcase_directional_multitype_declared_v1",
        .protocol_semantic_id = "t76_fourcase_25seed_500stall_v1",
        .seed = config.seed,
        .requested_workers = config.workers,
        .observed_workers =
            executor.work_receipt().distinct_participants,
        .demes = config.demes,
        .individuals_per_deme = config.individuals_per_deme,
        .generations = generations,
        .unchanged_generations = unchanged,
        .physical_fes = physical_fes,
        .evaluator_seconds = evaluator_seconds,
        .algorithm_seconds = std::max(0.0, end_to_end - evaluator_seconds),
        .end_to_end_seconds = end_to_end,
        .scientific_hash = scientific_hash(best, generations, physical_fes),
        .best_layout = problem.decode(best.genes),
        .best_evaluation = best.evaluation,
    };
}

}  // namespace core99::t76
