/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T64 pure-C++ problem wrapper, uniformity metric, and
three-penalty NSGA-II orchestration
Paper DOI: The Impact of Land Use Constraints in Multi-Objective
Energy-Noise Wind Farm Layout Optimization; 10.1016/j.renene.2015.06.026
Public source: no target source or native problem arrays were located.
Related public source:
https://gitlab.windenergy.dtu.dk/TOPFARM/PyWake.git at revision
5b07481ec9b3633a74844651648f266ba82a8b32 for an independent ISO check.
Missing/conflicts/reconstruction, semantic IDs, HPC design, and claim boundary:
include/core99/sorkhabi_t64.hpp
Shared project-native implementation: the H5-validated same-lineage T72
physical evaluator and NSGA-II kernel are reused with default T72 behavior
unchanged.
Contract: shared/contracts/core99_t64_sorkhabi_2016.json
Claim boundary: declared academic reconstruction, not author numerical replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/sorkhabi_t64.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace core99::t64 {
namespace {

double centered_discrepancy(
    const std::vector<t72::Point>& receptors
) {
    if (receptors.empty()) return 0.0;
    const double count = static_cast<double>(receptors.size());
    double first_sum = 0.0;
    for (const auto& point : receptors) {
        const double x = point.x_m / 3000.0;
        const double y = point.y_m / 3000.0;
        const auto factor = [](const double value) {
            const double centered = std::abs(value - 0.5);
            return 1.0 + 0.5 * centered
                - 0.5 * centered * centered;
        };
        first_sum += factor(x) * factor(y);
    }
    double pair_sum = 0.0;
    for (const auto& first : receptors) {
        const double first_x = first.x_m / 3000.0;
        const double first_y = first.y_m / 3000.0;
        for (const auto& second : receptors) {
            const double second_x = second.x_m / 3000.0;
            const double second_y = second.y_m / 3000.0;
            const auto factor = [](
                const double left,
                const double right
            ) {
                return 1.0
                    + 0.5 * std::abs(left - 0.5)
                    + 0.5 * std::abs(right - 0.5)
                    - 0.5 * std::abs(left - right);
            };
            pair_sum += factor(first_x, second_x)
                * factor(first_y, second_y);
        }
    }
    const double squared =
        std::pow(13.0 / 12.0, 2.0)
        - 2.0 * first_sum / count
        + pair_sum / (count * count);
    return std::sqrt(std::max(0.0, squared));
}

}  // namespace

Problem::Problem(
    const int land_availability_percent,
    const int turbine_count,
    const int map_variant
)
    : id_(
          "t64_phi" + std::to_string(land_availability_percent)
          + "_n" + std::to_string(turbine_count)
          + (
              map_variant == 0
                  ? "" : "_map" + std::to_string(map_variant)
          )
      ),
      evaluator_(
          land_availability_percent,
          turbine_count,
          map_variant,
          t72::PhysicsProfile::sorkhabi_2016_cubic_100db
      ),
      uniformity_parameter_(
          centered_discrepancy(evaluator_.receptors())
      ) {}

const std::string& Problem::id() const noexcept { return id_; }
int Problem::land_availability_percent() const noexcept {
    return evaluator_.land_availability_percent();
}
int Problem::turbine_count() const noexcept {
    return evaluator_.turbine_count();
}
int Problem::map_variant() const noexcept {
    return evaluator_.map_variant();
}
int Problem::population_size() const noexcept {
    return evaluator_.population_size();
}
double Problem::measured_land_availability() const noexcept {
    return evaluator_.measured_land_availability();
}
double Problem::uniformity_parameter() const noexcept {
    return uniformity_parameter_;
}
const std::vector<t72::Point>& Problem::receptors() const noexcept {
    return evaluator_.receptors();
}
t72::Evaluation Problem::evaluate(
    const std::vector<t72::Point>& layout
) const {
    return evaluator_.evaluate(layout);
}
const t72::Problem& Problem::shared_evaluator() const noexcept {
    return evaluator_;
}

std::string penalty_mode_name(const PenaltyMode mode) {
    switch (mode) {
        case PenaltyMode::static_1e4:
            return "static_1e4";
        case PenaltyMode::static_4e4:
            return "static_4e4";
        case PenaltyMode::dynamic_cgen_ngen:
            return "dynamic_cgen_ngen";
        case PenaltyMode::dynamic_cgen_half_ngen:
            return "dynamic_cgen_half_ngen";
        case PenaltyMode::death:
            return "death";
    }
    throw std::invalid_argument("T64 unknown penalty mode");
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (
        config.workers < 1
        || config.physical_fes
            < static_cast<std::uint64_t>(problem.population_size())
    ) {
        throw std::invalid_argument("T64 run configuration invalid");
    }
    t72::RunConfig shared;
    shared.seed = config.seed;
    shared.workers = config.workers;
    shared.physical_fes = config.physical_fes;
    shared.maximum_repair_distance_bin2 = 0.0;
    shared.feasible_initialization = true;
    shared.enable_convergence = config.enable_convergence;
    switch (config.penalty_mode) {
        case PenaltyMode::static_1e4:
            shared.constraint_mode =
                t72::ConstraintHandlingMode::static_penalty;
            shared.penalty_coefficient = 10000.0;
            break;
        case PenaltyMode::static_4e4:
            shared.constraint_mode =
                t72::ConstraintHandlingMode::static_penalty;
            shared.penalty_coefficient = 40000.0;
            break;
        case PenaltyMode::dynamic_cgen_ngen:
            shared.constraint_mode =
                t72::ConstraintHandlingMode::dynamic_penalty;
            shared.penalty_coefficient = 10000.0;
            shared.dynamic_penalty_multiplier = 1.0;
            break;
        case PenaltyMode::dynamic_cgen_half_ngen:
            shared.constraint_mode =
                t72::ConstraintHandlingMode::dynamic_penalty;
            shared.penalty_coefficient = 10000.0;
            shared.dynamic_penalty_multiplier = 2.0;
            break;
        case PenaltyMode::death:
            shared.constraint_mode =
                t72::ConstraintHandlingMode::death_penalty;
            shared.penalty_coefficient = 10000.0;
            break;
    }
    auto result = t72::run(problem.shared_evaluator(), shared);
    return {
        .problem_id = problem.id(),
        .problem_semantic_id =
            "t64_energy_noise_land13role_declared_reconstruction_v1",
        .method_semantic_id =
            "t64_nsga2_three_penalties_declared_reconstruction_v1",
        .protocol_semantic_id =
            "t64_80000fes_25seed_penalty_uniformity_v1",
        .penalty_mode = penalty_mode_name(config.penalty_mode),
        .seed = result.seed,
        .requested_workers = result.requested_workers,
        .observed_workers = result.observed_workers,
        .physical_fes = result.physical_fes,
        .generations = result.generations,
        .population_size = result.population_size,
        .converged = result.converged,
        .measured_land_availability =
            result.measured_land_availability,
        .uniformity_parameter = problem.uniformity_parameter(),
        .evaluator_seconds = result.evaluator_seconds,
        .algorithm_seconds = result.algorithm_seconds,
        .end_to_end_seconds = result.end_to_end_seconds,
        .scientific_hash = result.scientific_hash,
        .front = std::move(result.front),
    };
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (const int availability : {70, 80, 90}) {
        for (const int turbines : {5, 10, 15}) {
            result.push_back(
                "t64_phi" + std::to_string(availability)
                + "_n" + std::to_string(turbines)
            );
        }
    }
    for (int map = 0; map < 4; ++map) {
        result.push_back(
            "t64_uniformity_phi80_n10_map"
            + std::to_string(map)
        );
    }
    return result;
}

}  // namespace core99::t64
