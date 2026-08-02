/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T04 UWFLO evaluator, constrained PSO, and HPC execution
Paper title and DOI: Unrestricted Wind Farm Layout Optimization,
10.1016/j.renene.2011.06.033.
Public source: no author implementation was located.
Missing fields and Reconstruction:
include/core99/uwflo_t04.hpp
Semantic IDs and Contract: shared/contracts/core99_t04_uwflo_cases.json.
Independent equation oracle: scripts/validate_core99_t04.py
HPC design: persistent workers; parallel particle transition and evaluator;
fixed-order personal/global-best commits; counter-keyed random events
Claim boundary: academic declared reconstruction, not author-source or
author-exact numerical reproduction.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/uwflo_t04.hpp"

#include "fode/rng.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace core99::t04 {
namespace {

using Clock = std::chrono::steady_clock;

constexpr double kReferenceDiameter = 0.12;
constexpr double kHubHeight = 0.12;
constexpr double kRoughness = 0.001;
constexpr double kDensity = 1.2;
constexpr double kRatedPower = 0.385;
constexpr double kRatedSpeed = 6.17;
constexpr double kCase12Inflow = 7.0896;
constexpr double kCase3Inflow = 6.2;
constexpr double kInertia = 0.5;
constexpr double kLocal = 1.4;
constexpr double kGlobal = 1.4;

struct Particle {
    std::vector<double> position;
    std::vector<double> velocity;
    Evaluation value;
    std::vector<double> personal_best;
    Evaluation personal_value;
};

double circle_overlap(double distance, double first, double second) {
    if (distance >= first + second) {
        return 0.0;
    }
    if (distance <= std::abs(first - second)) {
        const double radius = std::min(first, second);
        return std::numbers::pi * radius * radius;
    }
    const double first_angle = std::acos(std::clamp(
        (distance * distance + first * first - second * second)
            / (2.0 * distance * first),
        -1.0,
        1.0
    ));
    const double second_angle = std::acos(std::clamp(
        (distance * distance + second * second - first * first)
            / (2.0 * distance * second),
        -1.0,
        1.0
    ));
    const double radicand = std::max(
        0.0,
        (-distance + first + second)
        * (distance + first - second)
        * (distance - first + second)
        * (distance + first + second)
    );
    return first * first * first_angle
        + second * second * second_angle
        - 0.5 * std::sqrt(radicand);
}

double induction(double speed) {
    if (speed <= 5.0) {
        return std::clamp(
            -0.0163 * speed * speed + 0.1635 * speed - 0.3142,
            0.0,
            0.5
        );
    }
    return std::clamp(-0.0063 * speed + 0.1273, 0.0, 0.5);
}

double generated_power(double speed, double diameter, bool adaptive) {
    if (!(speed > 0.0)) {
        return 0.0;
    }
    if (adaptive && speed >= kRatedSpeed) {
        return kRatedPower * (diameter / kReferenceDiameter)
            * (diameter / kReferenceDiameter);
    }
    const double cp = std::max(
        0.0,
        -0.0494 * speed * speed + 0.4914 * speed - 0.9097
    );
    const double area = std::numbers::pi * diameter * diameter / 4.0;
    return 0.5 * kDensity * area * cp * speed * speed * speed;
}

double equivalent_commercial_cost(double model_diameter) {
    const double commercial_diameter =
        model_diameter * (75.0 / kReferenceDiameter);
    return 143.85
        - 0.32447 * commercial_diameter
        - 1.4841e-3 * commercial_diameter * commercial_diameter;
}

bool better(
    const Evaluation& left,
    const std::vector<double>& left_position,
    const Evaluation& right,
    const std::vector<double>& right_position
) {
    const bool left_feasible = left.constraint_violation <= 1.0e-12;
    const bool right_feasible = right.constraint_violation <= 1.0e-12;
    if (left_feasible != right_feasible) {
        return left_feasible;
    }
    if (
        left.constraint_violation
        != right.constraint_violation
    ) {
        return left.constraint_violation < right.constraint_violation;
    }
    if (left.farm_efficiency != right.farm_efficiency) {
        return left.farm_efficiency > right.farm_efficiency;
    }
    return left_position < right_position;
}

std::uint64_t result_hash(
    const std::vector<double>& variables,
    const Evaluation& value
) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const double variable : variables) {
        hash ^= std::bit_cast<std::uint64_t>(variable);
        hash *= 1099511628211ULL;
    }
    hash ^= std::bit_cast<std::uint64_t>(value.farm_efficiency);
    hash *= 1099511628211ULL;
    return hash;
}

int trailing_integer(std::string_view value, std::string_view marker) {
    const std::size_t position = value.rfind(marker);
    if (position == std::string_view::npos) {
        throw std::invalid_argument("missing T04 case integer");
    }
    return std::stoi(std::string(value.substr(position + marker.size())));
}

}  // namespace

Problem::Problem(std::string problem_id) : id_(std::move(problem_id)) {
    if (id_ == "t04_uwflo_case1_n9") {
        paper_case_ = 1;
        turbines_ = 9;
        farm_x_ = 1.68;
        farm_y_ = 0.72;
    } else if (id_ == "t04_uwflo_case2_n9") {
        paper_case_ = 2;
        turbines_ = 9;
        farm_x_ = 1.68;
        farm_y_ = 0.72;
        variable_diameter_ = true;
    } else if (id_.starts_with("t04_uwflo_case3_i_n")) {
        paper_case_ = 3;
        turbines_ = trailing_integer(id_, "_n");
        farm_x_ = 1.68;
        farm_y_ = 0.72;
        if (
            turbines_ != 6 && turbines_ != 9 && turbines_ != 12
            && turbines_ != 15 && turbines_ != 18
        ) {
            throw std::invalid_argument("T04 Study-I count outside paper");
        }
    } else if (id_.starts_with("t04_uwflo_case3_ii_f")) {
        paper_case_ = 3;
        turbines_ = 18;
        const int farm = trailing_integer(id_, "_f");
        if (farm < 1 || farm > 5) {
            throw std::invalid_argument("T04 Study-II farm outside paper");
        }
        farm_x_ = static_cast<double>(7 * farm) * kReferenceDiameter;
        farm_y_ = static_cast<double>(3 * farm) * kReferenceDiameter;
    } else {
        throw std::invalid_argument("unknown T04 problem: " + id_);
    }
}

const std::string& Problem::id() const noexcept {
    return id_;
}

int Problem::turbine_count() const noexcept {
    return turbines_;
}

int Problem::dimension() const noexcept {
    return turbines_ * (variable_diameter_ ? 3 : 2);
}

int Problem::paper_case() const noexcept {
    return paper_case_;
}

double Problem::farm_x_m() const noexcept {
    return farm_x_;
}

double Problem::farm_y_m() const noexcept {
    return farm_y_;
}

std::vector<double> Problem::lower_bounds() const {
    std::vector<double> result(static_cast<std::size_t>(dimension()), 0.0);
    if (variable_diameter_) {
        std::fill(
            result.begin() + 2 * turbines_,
            result.end(),
            0.08
        );
    }
    return result;
}

std::vector<double> Problem::upper_bounds() const {
    std::vector<double> result(static_cast<std::size_t>(dimension()), 0.0);
    std::fill(result.begin(), result.begin() + turbines_, farm_x_);
    std::fill(
        result.begin() + turbines_,
        result.begin() + 2 * turbines_,
        farm_y_
    );
    if (variable_diameter_) {
        std::fill(
            result.begin() + 2 * turbines_,
            result.end(),
            0.16
        );
    }
    return result;
}

Evaluation Problem::evaluate(const std::vector<double>& variables) const {
    if (static_cast<int>(variables.size()) != dimension()) {
        throw std::invalid_argument("T04 variable dimension mismatch");
    }
    std::vector<double> diameter(
        static_cast<std::size_t>(turbines_),
        kReferenceDiameter
    );
    if (variable_diameter_) {
        for (int turbine = 0; turbine < turbines_; ++turbine) {
            diameter[static_cast<std::size_t>(turbine)] =
                variables[static_cast<std::size_t>(2 * turbines_ + turbine)];
        }
    }
    double violation = 0.0;
    const auto lower = lower_bounds();
    const auto upper = upper_bounds();
    for (int coordinate = 0; coordinate < dimension(); ++coordinate) {
        violation += std::max(
            {
                0.0,
                lower[static_cast<std::size_t>(coordinate)]
                    - variables[static_cast<std::size_t>(coordinate)],
                variables[static_cast<std::size_t>(coordinate)]
                    - upper[static_cast<std::size_t>(coordinate)]
            }
        );
    }
    for (int left = 0; left < turbines_; ++left) {
        for (int right = left + 1; right < turbines_; ++right) {
            const double dx =
                variables[static_cast<std::size_t>(left)]
                - variables[static_cast<std::size_t>(right)];
            const double dy =
                variables[static_cast<std::size_t>(turbines_ + left)]
                - variables[static_cast<std::size_t>(turbines_ + right)];
            const double required = 0.5 * (
                diameter[static_cast<std::size_t>(left)]
                + diameter[static_cast<std::size_t>(right)]
            );
            violation += std::max(
                0.0,
                required - std::sqrt(dx * dx + dy * dy)
            );
        }
    }
    if (variable_diameter_) {
        double cost = 0.0;
        for (const double value : diameter) {
            cost += equivalent_commercial_cost(value);
        }
        cost /= static_cast<double>(turbines_);
        violation += std::max(
            0.0,
            cost - equivalent_commercial_cost(kReferenceDiameter)
        );
    }

    const double inflow = paper_case_ == 3 ? kCase3Inflow : kCase12Inflow;
    const double expansion = 0.5 / std::log(kHubHeight / kRoughness);
    std::vector<double> local_speed(
        static_cast<std::size_t>(turbines_),
        inflow
    );
    std::vector<int> flow_order(static_cast<std::size_t>(turbines_));
    for (int turbine = 0; turbine < turbines_; ++turbine) {
        flow_order[static_cast<std::size_t>(turbine)] = turbine;
    }
    std::stable_sort(
        flow_order.begin(),
        flow_order.end(),
        [&](int left, int right) {
            return variables[static_cast<std::size_t>(left)]
                < variables[static_cast<std::size_t>(right)];
        }
    );
    for (int target_rank = 0; target_rank < turbines_; ++target_rank) {
        const int target =
            flow_order[static_cast<std::size_t>(target_rank)];
        double absolute_deficit_squared = 0.0;
        for (int source_rank = 0;
             source_rank < target_rank;
             ++source_rank) {
            const int source =
                flow_order[static_cast<std::size_t>(source_rank)];
            if (source == target) {
                continue;
            }
            const double downstream =
                variables[static_cast<std::size_t>(target)]
                - variables[static_cast<std::size_t>(source)];
            if (!(downstream > 0.0)) {
                continue;
            }
            const double crosswind = std::abs(
                variables[static_cast<std::size_t>(turbines_ + target)]
                - variables[static_cast<std::size_t>(turbines_ + source)]
            );
            const double source_radius =
                0.5 * diameter[static_cast<std::size_t>(source)];
            const double target_radius =
                0.5 * diameter[static_cast<std::size_t>(target)];
            const double wake_radius =
                source_radius + expansion * downstream;
            const double target_area =
                std::numbers::pi * target_radius * target_radius;
            const double overlap = circle_overlap(
                crosswind,
                wake_radius,
                target_radius
            );
            const double center_deficit =
                2.0 * induction(inflow)
                * source_radius * source_radius
                / (wake_radius * wake_radius);
            const double wake_speed =
                (1.0 - center_deficit)
                * local_speed[static_cast<std::size_t>(source)];
            const double absolute_deficit = inflow - wake_speed;
            absolute_deficit_squared +=
                overlap / target_area
                * absolute_deficit * absolute_deficit;
        }
        local_speed[static_cast<std::size_t>(target)] = std::max(
            0.0,
            inflow - std::sqrt(absolute_deficit_squared)
        );
    }
    double power = 0.0;
    for (int turbine = 0; turbine < turbines_; ++turbine) {
        power += generated_power(
            local_speed[static_cast<std::size_t>(turbine)],
            diameter[static_cast<std::size_t>(turbine)],
            paper_case_ == 3
        );
    }
    return {
        power,
        power / (static_cast<double>(turbines_) * kRatedPower),
        violation
    };
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<double>>& variables,
    fode::PersistentExecutor& executor
) const {
    std::vector<Evaluation> result(variables.size());
    executor.parallel_for(
        0,
        static_cast<int>(variables.size()),
        [&](int index) {
            result[static_cast<std::size_t>(index)] = evaluate(
                variables[static_cast<std::size_t>(index)]
            );
        }
    );
    return result;
}

std::uint64_t paper_physical_fes(const Problem& problem) {
    return problem.paper_case() == 2 ? 25000 : 15000;
}

RunResult run(
    const Problem& problem,
    std::uint64_t seed,
    std::uint64_t physical_fes,
    int workers
) {
    if (workers <= 0 || physical_fes == 0) {
        throw std::invalid_argument("invalid T04 run request");
    }
    const int population_size = 5 * problem.dimension();
    if (physical_fes < static_cast<std::uint64_t>(population_size)) {
        throw std::invalid_argument("T04 FES smaller than swarm");
    }
    const auto start = Clock::now();
    fode::PersistentExecutor executor(workers);
    const fode::CounterRng rng(seed ^ 0x5404a912ULL);
    const auto lower = problem.lower_bounds();
    const auto upper = problem.upper_bounds();
    std::vector<Particle> swarm(static_cast<std::size_t>(population_size));
    executor.parallel_for(0, population_size, [&](int particle) {
        auto& item = swarm[static_cast<std::size_t>(particle)];
        item.position.resize(static_cast<std::size_t>(problem.dimension()));
        item.velocity.assign(
            static_cast<std::size_t>(problem.dimension()),
            0.0
        );
        for (int coordinate = 0;
             coordinate < problem.dimension();
             ++coordinate) {
            item.position[static_cast<std::size_t>(coordinate)] =
                lower[static_cast<std::size_t>(coordinate)]
                + (
                    upper[static_cast<std::size_t>(coordinate)]
                    - lower[static_cast<std::size_t>(coordinate)]
                ) * rng.uniform(0, 500, particle, coordinate);
        }
    });
    double evaluator_seconds = 0.0;
    auto eval_start = Clock::now();
    {
        std::vector<std::vector<double>> positions;
        positions.reserve(swarm.size());
        for (const Particle& item : swarm) {
            positions.push_back(item.position);
        }
        const auto values = problem.evaluate_population(positions, executor);
        for (std::size_t index = 0; index < swarm.size(); ++index) {
            swarm[index].value = values[index];
            swarm[index].personal_best = swarm[index].position;
            swarm[index].personal_value = values[index];
        }
    }
    evaluator_seconds += std::chrono::duration<double>(
        Clock::now() - eval_start
    ).count();
    std::uint64_t fes = static_cast<std::uint64_t>(population_size);
    int global_index = 0;
    for (int particle = 1; particle < population_size; ++particle) {
        if (better(
                swarm[static_cast<std::size_t>(particle)].personal_value,
                swarm[static_cast<std::size_t>(particle)].personal_best,
                swarm[static_cast<std::size_t>(global_index)].personal_value,
                swarm[static_cast<std::size_t>(global_index)].personal_best
            )) {
            global_index = particle;
        }
    }
    std::vector<double> global_best =
        swarm[static_cast<std::size_t>(global_index)].personal_best;
    Evaluation global_value =
        swarm[static_cast<std::size_t>(global_index)].personal_value;
    std::uint64_t generation = 0;
    while (fes < physical_fes) {
        const int count = static_cast<int>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(population_size),
                physical_fes - fes
            )
        );
        executor.parallel_for(0, count, [&](int particle) {
            Particle& item = swarm[static_cast<std::size_t>(particle)];
            for (int coordinate = 0;
                 coordinate < problem.dimension();
                 ++coordinate) {
                const std::size_t slot =
                    static_cast<std::size_t>(coordinate);
                item.velocity[slot] =
                    kInertia * item.velocity[slot]
                    + kLocal * rng.uniform(
                        generation + 1,
                        501,
                        particle,
                        coordinate,
                        0
                    ) * (item.personal_best[slot] - item.position[slot])
                    + kGlobal * rng.uniform(
                        generation + 1,
                        501,
                        particle,
                        coordinate,
                        1
                    ) * (global_best[slot] - item.position[slot]);
                item.position[slot] = std::clamp(
                    item.position[slot] + item.velocity[slot],
                    lower[slot],
                    upper[slot]
                );
            }
        });
        std::vector<std::vector<double>> positions;
        positions.reserve(static_cast<std::size_t>(count));
        for (int particle = 0; particle < count; ++particle) {
            positions.push_back(
                swarm[static_cast<std::size_t>(particle)].position
            );
        }
        eval_start = Clock::now();
        const auto values = problem.evaluate_population(positions, executor);
        evaluator_seconds += std::chrono::duration<double>(
            Clock::now() - eval_start
        ).count();
        for (int particle = 0; particle < count; ++particle) {
            Particle& item = swarm[static_cast<std::size_t>(particle)];
            item.value = values[static_cast<std::size_t>(particle)];
            if (better(
                    item.value,
                    item.position,
                    item.personal_value,
                    item.personal_best
                )) {
                item.personal_best = item.position;
                item.personal_value = item.value;
            }
            if (better(
                    item.personal_value,
                    item.personal_best,
                    global_value,
                    global_best
                )) {
                global_best = item.personal_best;
                global_value = item.personal_value;
            }
        }
        fes += static_cast<std::uint64_t>(count);
        ++generation;
    }
    const double end_to_end = std::chrono::duration<double>(
        Clock::now() - start
    ).count();
    return {
        problem.id(),
        global_best,
        global_value,
        seed,
        fes,
        workers,
        executor.thread_count(),
        evaluator_seconds,
        std::max(0.0, end_to_end - evaluator_seconds),
        end_to_end,
        result_hash(global_best, global_value) ^ fes
    };
}

}  // namespace core99::t04
