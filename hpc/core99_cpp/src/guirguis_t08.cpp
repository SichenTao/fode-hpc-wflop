/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 pure-C++ exact-gradient log-barrier/L-BFGS
interior-point and deterministic CPU-HPC implementation
Paper/DOI: Toward Efficient Optimization of Wind Farm Layouts: Utilizing
Exact Gradient Information; 10.1016/j.apenergy.2016.06.101
Public source: no paper-linked author implementation was found.
Missing fields, declared Reconstruction, semantic IDs, HPC details and Claim boundary:
hpc/core99_cpp/include/core99/guirguis_t08.hpp
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/guirguis_t08.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace core99::t08 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kStrictMargin = 1.0e-10;
// Keep substantially more deterministic chunks than hardware workers.  The
// most expensive paper-native wind rose otherwise completes before every
// persistent worker can claim work, even though the executor owns the full
// machine.  The fixed chunk identities preserve one/all-core reductions.
constexpr int kDeterministicChunks = 512;
constexpr int kLbfgsMemory = 8;
constexpr const char* kMethodSemanticId =
    "t08_exact_gradient_log_barrier_lbfgs_declared_ipm_v1";

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

double uniform01(
    const std::uint64_t seed,
    const std::uint64_t stream,
    const std::uint64_t index
) noexcept {
    const std::uint64_t bits = mix64(
        seed ^ mix64(stream + 0x517cc1b727220a95ULL) ^ mix64(index)
    );
    return static_cast<double>(bits >> 11U)
        * (1.0 / 9007199254740992.0);
}

double dot(
    const std::vector<double>& left,
    const std::vector<double>& right
) {
    double result = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        result += left[index] * right[index];
    }
    return result;
}

double maximum_absolute(const std::vector<double>& values) {
    double result = 0.0;
    for (const double value : values) result = std::max(result, std::abs(value));
    return result;
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::vector<WindState> classical_case_one() {
    return {{180.0, 12.0, 1.0}};
}

std::vector<WindState> classical_case_two() {
    std::vector<WindState> states;
    states.reserve(36);
    for (int direction = 0; direction < 36; ++direction) {
        states.push_back({10.0 * static_cast<double>(direction), 12.0, 1.0 / 36.0});
    }
    return states;
}

std::vector<WindState> classical_case_three() {
    // The paper prints only the polar curves. This is the same versioned
    // classical Case-C digitization used by the T05 package, expressed in the
    // T08 wind-to convention and normalized jointly across speed/direction.
    constexpr double profile17[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.55,1.7,2.7,3.2,2.7,1.7,1.55,1.2,1
    };
    constexpr double profile12[36]{
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1.2,1.45,1.75,1.7,2.35,1.7,1.75,1.45,1.2,1
    };
    std::vector<WindState> states;
    states.reserve(108);
    double total = 0.0;
    for (int direction = 0; direction < 36; ++direction) {
        const double p8 = 0.005;
        const double p12 = 0.008 * profile12[direction];
        const double p17 = 0.011 * profile17[direction];
        const double angle = 10.0 * static_cast<double>(direction);
        states.push_back({angle, 8.0, p8});
        states.push_back({angle, 12.0, p12});
        states.push_back({angle, 17.0, p17});
        total += p8 + p12 + p17;
    }
    for (WindState& state : states) state.probability /= total;
    return states;
}

std::vector<WindState> horns_rev_ten_mps() {
    // Public numeric Horns Rev lineage pinned and independently checked by
    // T25. T08 Fig. 5 uses the opposite plotted direction convention.
    constexpr double frequencies[12]{
        3.597152, 3.948682, 5.167395, 7.000154,
        8.364547, 6.434850, 8.643194, 11.770510,
        15.157570, 14.737920, 10.012050, 5.165975,
    };
    const double total = std::accumulate(
        std::begin(frequencies), std::end(frequencies), 0.0
    );
    std::vector<WindState> states;
    states.reserve(12);
    for (int direction = 0; direction < 12; ++direction) {
        const double plotted = std::fmod(
            30.0 * static_cast<double>(direction) + 180.0, 360.0
        );
        states.push_back({plotted, 10.0, frequencies[direction] / total});
    }
    return states;
}

std::vector<Point> copenhagen_polygon() {
    // Clockwise Fig.-9 digitization, then horizontal affine calibration about
    // the farm centre to make the polygon area exactly 66.2% of 2500x5000 m.
    std::vector<Point> polygon{
        {500,5000},{2100,5000},{2200,3900},{2500,2700},{2300,1800},
        {1800,0},{1100,500},{600,1000},{200,1600},{200,2600},
        {0,3200},{500,3900},
    };
    constexpr double raw_fraction = 8660000.0 / 12500000.0;
    constexpr double target_fraction = 0.662;
    constexpr double centre = 1250.0;
    constexpr double scale = target_fraction / raw_fraction;
    for (Point& point : polygon) {
        point.x_m = centre + scale * (point.x_m - centre);
        point.x_m = std::clamp(point.x_m, 0.0, 2500.0);
    }
    return polygon;
}

double polygon_area(const std::vector<Point>& polygon) {
    double twice_area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point& left = polygon[index];
        const Point& right = polygon[(index + 1U) % polygon.size()];
        twice_area += left.x_m * right.y_m - right.x_m * left.y_m;
    }
    return 0.5 * std::abs(twice_area);
}

bool point_inside_polygon(const Point& point, const std::vector<Point>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1U;
         i < polygon.size(); j = i++) {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        const bool crossing = ((a.y_m > point.y_m) != (b.y_m > point.y_m))
            && (point.x_m < (b.x_m - a.x_m) * (point.y_m - a.y_m)
                / (b.y_m - a.y_m) + a.x_m);
        if (crossing) inside = !inside;
    }
    return inside;
}

struct SignedDistance {
    double value_m = 0.0;
    double derivative_x = 0.0;
    double derivative_y = 0.0;
};

SignedDistance polygon_signed_distance(
    const Point& point,
    const std::vector<Point>& polygon
) {
    double best_squared = std::numeric_limits<double>::infinity();
    Point best_projection{};
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point& a = polygon[index];
        const Point& b = polygon[(index + 1U) % polygon.size()];
        const double vx = b.x_m - a.x_m;
        const double vy = b.y_m - a.y_m;
        const double length_squared = vx * vx + vy * vy;
        const double projection = std::clamp(
            ((point.x_m - a.x_m) * vx + (point.y_m - a.y_m) * vy)
                / length_squared,
            0.0,
            1.0
        );
        const Point closest{a.x_m + projection * vx, a.y_m + projection * vy};
        const double dx = point.x_m - closest.x_m;
        const double dy = point.y_m - closest.y_m;
        const double squared = dx * dx + dy * dy;
        if (squared < best_squared) {
            best_squared = squared;
            best_projection = closest;
        }
    }
    const double distance = std::sqrt(std::max(best_squared, 1.0e-24));
    const bool inside = point_inside_polygon(point, polygon);
    const double sign = inside ? 1.0 : -1.0;
    return {
        sign * distance,
        sign * (point.x_m - best_projection.x_m) / distance,
        sign * (point.y_m - best_projection.y_m) / distance,
    };
}

PaperCase make_case(const std::string& case_id) {
    for (int wind_case = 1; wind_case <= 3; ++wind_case) {
        for (const int turbines : {10, 20, 30}) {
            const std::string candidate = "t08_benchmark_c"
                + std::to_string(wind_case) + "_n" + std::to_string(turbines);
            if (case_id != candidate) continue;
            return {
                candidate,
                "t08_classical_case" + std::to_string(wind_case)
                    + "_continuous_v1",
                turbines,
                2000.0,
                2000.0,
                40.0,
                60.0,
                0.3,
                200.0,
                wind_case == 1 ? classical_case_one()
                    : (wind_case == 2 ? classical_case_two()
                                      : classical_case_three()),
                "rectangle",
                0.0,
            };
        }
    }
    for (const int turbines : {37, 50, 100}) {
        for (const int density : {4, 5, 6}) {
            const std::string candidate = "t08_scaling_n"
                + std::to_string(turbines) + "_d" + std::to_string(density);
            if (case_id != candidate) continue;
            const double side = 1000.0 * std::sqrt(
                static_cast<double>(turbines) / static_cast<double>(density)
            );
            return {
                candidate,
                "t08_horns_density_scaling_v1",
                turbines,
                side,
                side,
                80.0,
                70.0,
                0.3,
                400.0,
                horns_rev_ten_mps(),
                "rectangle",
                0.0,
            };
        }
    }
    if (case_id == "t08_land_copenhagen_n37") {
        return {
            case_id,
            "t08_copenhagen_figure66p2_v1",
            37,
            2500.0,
            5000.0,
            80.0,
            70.0,
            0.3,
            400.0,
            horns_rev_ten_mps(),
            "copenhagen_polygon",
            90.48,
        };
    }
    if (case_id == "t08_land_ring_n20") {
        return {
            case_id,
            "t08_ring50m_v1",
            20,
            2000.0,
            2000.0,
            40.0,
            60.0,
            0.3,
            200.0,
            {{180.0, 10.0, 1.0}},
            "ring_750_800",
            98.0,
        };
    }
    throw std::invalid_argument("unknown T08 case " + case_id);
}

struct PairWakeTerms {
    double deficit_fraction = 0.0;
    double derivative_downstream = 0.0;
    double derivative_crosswind = 0.0;
};

PairWakeTerms pair_wake(
    const double downstream,
    const double crosswind,
    const double rotor_radius,
    const double entrainment
) {
    if (!(downstream > 1.0e-12)) return {};
    const double absolute_crosswind = std::abs(crosswind);
    const double squared_radius = downstream * downstream
        + absolute_crosswind * absolute_crosswind;
    const double angle = std::atan2(absolute_crosswind, downstream);
    if (!(angle < kPi / 9.0)) return {};
    const double modulation = 0.5 * (1.0 + std::cos(9.0 * angle));
    const double denominator = rotor_radius + entrainment * downstream;
    const double wake_ratio = rotor_radius / denominator;
    const double deficit = (2.0 / 3.0) * modulation
        * wake_ratio * wake_ratio;
    const double modulation_derivative = -4.5 * std::sin(9.0 * angle);
    const double angle_downstream = -absolute_crosswind / squared_radius;
    const double angle_crosswind = crosswind == 0.0
        ? 0.0
        : std::copysign(downstream / squared_radius, crosswind);
    const double ratio_downstream = -entrainment * wake_ratio / denominator;
    const double common = 2.0 / 3.0;
    return {
        deficit,
        common * (
            modulation_derivative * angle_downstream
                * wake_ratio * wake_ratio
            + modulation * 2.0 * wake_ratio * ratio_downstream
        ),
        common * modulation_derivative * angle_crosswind
            * wake_ratio * wake_ratio,
    };
}

std::vector<double> flatten_normalized(
    const std::vector<Point>& layout,
    const PaperCase& paper_case
) {
    std::vector<double> values(static_cast<std::size_t>(2 * paper_case.turbine_count));
    for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
        values[static_cast<std::size_t>(2 * turbine)] =
            layout[static_cast<std::size_t>(turbine)].x_m / paper_case.width_m;
        values[static_cast<std::size_t>(2 * turbine + 1)] =
            layout[static_cast<std::size_t>(turbine)].y_m / paper_case.height_m;
    }
    return values;
}

std::vector<Point> unflatten_normalized(
    const std::vector<double>& values,
    const PaperCase& paper_case
) {
    std::vector<Point> layout(static_cast<std::size_t>(paper_case.turbine_count));
    for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
        layout[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(2 * turbine)] * paper_case.width_m,
            values[static_cast<std::size_t>(2 * turbine + 1)] * paper_case.height_m,
        };
    }
    return layout;
}

struct ObjectivePoint {
    bool valid = false;
    double value = 0.0;
    std::vector<double> gradient;
    Evaluation evaluation;
    ConstraintReceipt constraints;
};

ObjectivePoint barrier_objective(
    const Problem& problem,
    const std::vector<double>& variables,
    const double barrier_weight,
    fode::PersistentExecutor& executor
) {
    const PaperCase& paper_case = problem.paper_case();
    const std::vector<Point> layout = unflatten_normalized(variables, paper_case);
    ObjectivePoint result;
    result.evaluation = problem.evaluate(layout, true, executor);
    result.constraints = problem.barrier(layout, barrier_weight, true, executor);
    if (!std::isfinite(result.constraints.barrier_value)
        || result.constraints.maximum_violation >= -kStrictMargin) {
        return result;
    }
    result.valid = true;
    result.value = -result.evaluation.efficiency_percent
        + result.constraints.barrier_value;
    result.gradient.resize(variables.size());
    for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
        const std::size_t x = static_cast<std::size_t>(2 * turbine);
        const std::size_t y = x + 1U;
        result.gradient[x] = (
            -result.evaluation.gradient_percent_per_m[x]
            + result.constraints.barrier_gradient_per_m[x]
        ) * paper_case.width_m;
        result.gradient[y] = (
            -result.evaluation.gradient_percent_per_m[y]
            + result.constraints.barrier_gradient_per_m[y]
        ) * paper_case.height_m;
    }
    return result;
}

std::vector<double> lbfgs_direction(
    const std::vector<double>& gradient,
    const std::vector<std::vector<double>>& s_history,
    const std::vector<std::vector<double>>& y_history
) {
    std::vector<double> q = gradient;
    std::vector<double> alpha(s_history.size(), 0.0);
    std::vector<double> rho(s_history.size(), 0.0);
    for (std::size_t reverse = s_history.size(); reverse-- > 0U;) {
        const double curvature = dot(s_history[reverse], y_history[reverse]);
        rho[reverse] = curvature > 1.0e-20 ? 1.0 / curvature : 0.0;
        alpha[reverse] = rho[reverse] * dot(s_history[reverse], q);
        for (std::size_t index = 0; index < q.size(); ++index) {
            q[index] -= alpha[reverse] * y_history[reverse][index];
        }
    }
    double initial_scale = 1.0;
    if (!s_history.empty()) {
        const double sy = dot(s_history.back(), y_history.back());
        const double yy = dot(y_history.back(), y_history.back());
        if (sy > 0.0 && yy > 0.0) initial_scale = sy / yy;
    }
    for (double& value : q) value *= initial_scale;
    for (std::size_t index = 0; index < s_history.size(); ++index) {
        const double beta = rho[index] * dot(y_history[index], q);
        for (std::size_t variable = 0; variable < q.size(); ++variable) {
            q[variable] += s_history[index][variable]
                * (alpha[index] - beta);
        }
    }
    for (double& value : q) value = -value;
    return q;
}

StartReceipt optimize_start(
    const Problem& problem,
    const OptimizationConfig& config,
    const int start_index,
    fode::PersistentExecutor& executor
) {
    const Clock::time_point start_time = Clock::now();
    StartReceipt receipt;
    receipt.start_index = start_index;
    std::vector<Point> layout = problem.initial_layout(
        config.start_policy, config.seed, start_index
    );
    std::vector<double> variables = flatten_normalized(layout, problem.paper_case());
    Evaluation initial = problem.evaluate(layout, false, executor);
    receipt.initial_efficiency_percent = initial.efficiency_percent;
    receipt.evaluator_seconds += initial.seconds;
    ++receipt.objective_gradient_evaluations;

    std::vector<std::vector<double>> s_history;
    std::vector<std::vector<double>> y_history;
    ObjectivePoint current;
    double barrier_weight = config.initial_barrier;
    bool budget_exhausted = false;

    for (int phase = 0; phase < config.barrier_phases; ++phase) {
        if (receipt.objective_gradient_evaluations
            >= config.maximum_evaluations_per_start) {
            budget_exhausted = true;
            break;
        }
        current = barrier_objective(problem, variables, barrier_weight, executor);
        ++receipt.objective_gradient_evaluations;
        receipt.evaluator_seconds += current.evaluation.seconds;
        receipt.constraint_seconds += current.constraints.seconds;
        if (!current.valid) {
            receipt.termination = "invalid_strictly_feasible_initialization";
            break;
        }
        s_history.clear();
        y_history.clear();
        const int remaining = config.maximum_evaluations_per_start
            - receipt.objective_gradient_evaluations;
        const int phases_left = config.barrier_phases - phase;
        const int phase_budget = std::max(1, remaining / phases_left);
        const int phase_end = receipt.objective_gradient_evaluations + phase_budget;

        while (receipt.objective_gradient_evaluations < phase_end
               && receipt.objective_gradient_evaluations
                    < config.maximum_evaluations_per_start) {
            if (maximum_absolute(current.gradient) < config.gradient_tolerance) {
                break;
            }
            std::vector<double> direction = lbfgs_direction(
                current.gradient, s_history, y_history
            );
            double directional_derivative = dot(current.gradient, direction);
            if (!(directional_derivative < 0.0)) {
                direction = current.gradient;
                for (double& value : direction) value = -value;
                directional_derivative = -dot(current.gradient, current.gradient);
                s_history.clear();
                y_history.clear();
            }
            const double largest_step = maximum_absolute(direction);
            if (largest_step > 0.05) {
                const double factor = 0.05 / largest_step;
                for (double& value : direction) value *= factor;
                directional_derivative *= factor;
            }

            bool accepted = false;
            double step = 1.0;
            ObjectivePoint candidate;
            std::vector<double> candidate_variables(variables.size());
            for (int trial = 0; trial < 24
                 && receipt.objective_gradient_evaluations
                    < config.maximum_evaluations_per_start; ++trial) {
                for (std::size_t index = 0; index < variables.size(); ++index) {
                    candidate_variables[index] = variables[index]
                        + step * direction[index];
                }
                candidate = barrier_objective(
                    problem, candidate_variables, barrier_weight, executor
                );
                ++receipt.objective_gradient_evaluations;
                receipt.evaluator_seconds += candidate.evaluation.seconds;
                receipt.constraint_seconds += candidate.constraints.seconds;
                if (candidate.valid
                    && candidate.value <= current.value
                        + 1.0e-4 * step * directional_derivative) {
                    accepted = true;
                    break;
                }
                step *= 0.5;
            }
            if (!accepted) break;

            std::vector<double> s(variables.size());
            std::vector<double> y(variables.size());
            for (std::size_t index = 0; index < variables.size(); ++index) {
                s[index] = candidate_variables[index] - variables[index];
                y[index] = candidate.gradient[index] - current.gradient[index];
            }
            if (dot(s, y) > 1.0e-12) {
                if (s_history.size() == kLbfgsMemory) {
                    s_history.erase(s_history.begin());
                    y_history.erase(y_history.begin());
                }
                s_history.push_back(std::move(s));
                y_history.push_back(std::move(y));
            }
            variables = std::move(candidate_variables);
            current = std::move(candidate);
            ++receipt.accepted_steps;
        }
        ++receipt.barrier_phases_completed;
        barrier_weight *= 0.2;
    }

    layout = unflatten_normalized(variables, problem.paper_case());
    Evaluation final_evaluation = problem.evaluate(layout, false, executor);
    ConstraintReceipt final_constraints = problem.barrier(layout, 0.0, false, executor);
    receipt.evaluator_seconds += final_evaluation.seconds;
    receipt.constraint_seconds += final_constraints.seconds;
    ++receipt.objective_gradient_evaluations;
    receipt.final_efficiency_percent = final_evaluation.efficiency_percent;
    receipt.maximum_constraint_violation = final_constraints.maximum_violation;
    receipt.minimum_spacing_m = problem.minimum_spacing(layout);
    receipt.final_layout = std::move(layout);
    if (receipt.termination.empty()) {
        receipt.termination = budget_exhausted
            || receipt.objective_gradient_evaluations
                >= config.maximum_evaluations_per_start
            ? "maximum_physical_evaluations"
            : "barrier_schedule_complete";
    }
    receipt.optimizer_seconds = seconds_since(start_time);
    return receipt;
}

}  // namespace

struct Problem::Data {
    PaperCase paper_case;
    std::vector<Point> polygon;
    double entrainment = 0.0;
};

Problem::Problem(std::string case_id) : data_(std::make_shared<Data>()) {
    auto mutable_data = std::const_pointer_cast<Data>(data_);
    mutable_data->paper_case = make_case(case_id);
    mutable_data->entrainment = 1.0 / (
        2.0 * std::log(
            mutable_data->paper_case.hub_height_m
            / mutable_data->paper_case.roughness_m
        )
    );
    if (mutable_data->paper_case.land_model == "copenhagen_polygon") {
        mutable_data->polygon = copenhagen_polygon();
        const double fraction = polygon_area(mutable_data->polygon)
            / (mutable_data->paper_case.width_m
               * mutable_data->paper_case.height_m);
        if (std::abs(fraction - 0.662) > 1.0e-12) {
            throw std::runtime_error("T08 Copenhagen area calibration failed");
        }
    }
}

const PaperCase& Problem::paper_case() const noexcept {
    return data_->paper_case;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const bool need_gradient,
    fode::PersistentExecutor& executor
) const {
    const Clock::time_point start = Clock::now();
    const PaperCase& paper_case = data_->paper_case;
    if (static_cast<int>(layout.size()) != paper_case.turbine_count) {
        throw std::invalid_argument("T08 layout size mismatch");
    }
    const int variables = 2 * paper_case.turbine_count;
    const int total_tasks = static_cast<int>(paper_case.wind_states.size())
        * paper_case.turbine_count;
    const int chunks = std::min(kDeterministicChunks, total_tasks);
    std::vector<double> chunk_power(static_cast<std::size_t>(chunks), 0.0);
    std::vector<double> chunk_gradient(
        need_gradient ? static_cast<std::size_t>(chunks * variables) : 0U,
        0.0
    );
    double reference_power = 0.0;
    std::vector<double> state_cosine(paper_case.wind_states.size());
    std::vector<double> state_sine(paper_case.wind_states.size());
    for (std::size_t state = 0; state < paper_case.wind_states.size(); ++state) {
        const double radians = paper_case.wind_states[state].flow_to_degrees
            * kPi / 180.0;
        state_cosine[state] = std::cos(radians);
        state_sine[state] = std::sin(radians);
    }
    for (const WindState& wind : paper_case.wind_states) {
        reference_power += wind.probability
            * static_cast<double>(paper_case.turbine_count)
            * (1.0 / 3.0) * wind.speed_mps * wind.speed_mps * wind.speed_mps;
    }
    executor.reset_work_receipt();
    executor.parallel_for(0, chunks, [&](const int chunk) {
        double local_power = 0.0;
        double* local_gradient = need_gradient
            ? chunk_gradient.data() + static_cast<std::size_t>(chunk * variables)
            : nullptr;
        for (int task = chunk; task < total_tasks; task += chunks) {
            const int state_index = task / paper_case.turbine_count;
            const int target = task % paper_case.turbine_count;
            const WindState& wind = paper_case.wind_states[
                static_cast<std::size_t>(state_index)
            ];
            const double cosine = state_cosine[static_cast<std::size_t>(state_index)];
            const double sine = state_sine[static_cast<std::size_t>(state_index)];
            const Point& target_point = layout[static_cast<std::size_t>(target)];
            const double target_downstream =
                cosine * target_point.x_m + sine * target_point.y_m;
            const double target_crosswind =
                -sine * target_point.x_m + cosine * target_point.y_m;
            double deficit_squared = 0.0;
            struct ActivePair {
                int source = 0;
                PairWakeTerms terms;
            };
            std::array<ActivePair, 100> active{};
            int active_count = 0;
            for (int source = 0; source < paper_case.turbine_count; ++source) {
                if (source == target) continue;
                const Point& source_point = layout[static_cast<std::size_t>(source)];
                const double source_downstream =
                    cosine * source_point.x_m + sine * source_point.y_m;
                const double downstream = target_downstream - source_downstream;
                if (!(downstream > 0.0)) continue;
                const double source_crosswind =
                    -sine * source_point.x_m + cosine * source_point.y_m;
                const PairWakeTerms terms = pair_wake(
                    downstream,
                    target_crosswind - source_crosswind,
                    0.5 * paper_case.rotor_diameter_m,
                    data_->entrainment
                );
                deficit_squared += terms.deficit_fraction * terms.deficit_fraction;
                if (need_gradient && terms.deficit_fraction != 0.0) {
                    active[static_cast<std::size_t>(active_count++)] = {source, terms};
                }
            }
            const double root_deficit = std::sqrt(deficit_squared);
            const double effective_speed = wind.speed_mps
                * std::max(0.0, 1.0 - root_deficit);
            const double scale = 100.0 * wind.probability / reference_power;
            local_power += scale * (1.0 / 3.0)
                * effective_speed * effective_speed * effective_speed;
            if (need_gradient && root_deficit > 1.0e-15
                && effective_speed > 0.0) {
                for (int active_index = 0; active_index < active_count; ++active_index) {
                    const ActivePair& pair = active[static_cast<std::size_t>(active_index)];
                    const double power_deficit_derivative = scale
                        * effective_speed * effective_speed
                        * (-wind.speed_mps * pair.terms.deficit_fraction
                           / root_deficit);
                    const double downstream_derivative =
                        power_deficit_derivative
                        * pair.terms.derivative_downstream;
                    const double crosswind_derivative =
                        power_deficit_derivative
                        * pair.terms.derivative_crosswind;
                    const double global_x = downstream_derivative * cosine
                        - crosswind_derivative * sine;
                    const double global_y = downstream_derivative * sine
                        + crosswind_derivative * cosine;
                    const std::size_t target_x = static_cast<std::size_t>(2 * target);
                    const std::size_t source_x = static_cast<std::size_t>(2 * pair.source);
                    local_gradient[target_x] += global_x;
                    local_gradient[target_x + 1U] += global_y;
                    local_gradient[source_x] -= global_x;
                    local_gradient[source_x + 1U] -= global_y;
                }
            }
        }
        chunk_power[static_cast<std::size_t>(chunk)] = local_power;
    });
    Evaluation result;
    result.efficiency_percent = std::accumulate(
        chunk_power.begin(), chunk_power.end(), 0.0
    );
    if (need_gradient) {
        result.gradient_percent_per_m.assign(
            static_cast<std::size_t>(variables), 0.0
        );
        for (int chunk = 0; chunk < chunks; ++chunk) {
            for (int variable = 0; variable < variables; ++variable) {
                result.gradient_percent_per_m[static_cast<std::size_t>(variable)] +=
                    chunk_gradient[static_cast<std::size_t>(chunk * variables + variable)];
            }
        }
    }
    const fode::ExecutorWorkReceipt work = executor.work_receipt();
    result.wind_turbine_tasks = static_cast<std::uint64_t>(total_tasks);
    result.requested_workers = executor.thread_count();
    result.observed_workers = std::max(1, work.distinct_participants);
    result.seconds = seconds_since(start);
    return result;
}

ConstraintReceipt Problem::barrier(
    const std::vector<Point>& layout,
    const double barrier_weight,
    const bool need_gradient,
    fode::PersistentExecutor& executor
) const {
    const Clock::time_point start = Clock::now();
    const PaperCase& paper_case = data_->paper_case;
    if (static_cast<int>(layout.size()) != paper_case.turbine_count) {
        throw std::invalid_argument("T08 layout size mismatch");
    }
    const int variables = 2 * paper_case.turbine_count;
    const int pair_count = paper_case.turbine_count
        * (paper_case.turbine_count - 1) / 2;
    const int land_per_turbine = paper_case.land_model == "rectangle" ? 4
        : (paper_case.land_model == "ring_750_800" ? 2 : 1);
    const int total_constraints = pair_count
        + land_per_turbine * paper_case.turbine_count;
    const double scaled_weight = total_constraints > 0
        ? barrier_weight / static_cast<double>(total_constraints)
        : 0.0;
    // On the target 20-core CPU all paper cases have at most 4,950 pair
    // constraints. The fixed-thread dispatch cost exceeds this lightweight
    // arithmetic, so the measured-fast path is one deterministic chunk.
    // Larger future extensions automatically re-enable the same fixed-slot
    // parallel reduction without changing mathematical semantics.
    const int chunks = pair_count >= 20000
        ? std::min(kDeterministicChunks, paper_case.turbine_count)
        : 1;
    std::vector<double> chunk_barrier(static_cast<std::size_t>(chunks), 0.0);
    std::vector<double> chunk_violation(
        static_cast<std::size_t>(chunks), -std::numeric_limits<double>::infinity()
    );
    std::vector<double> chunk_margin(
        static_cast<std::size_t>(chunks), std::numeric_limits<double>::infinity()
    );
    std::vector<double> chunk_gradient(
        need_gradient ? static_cast<std::size_t>(chunks * variables) : 0U,
        0.0
    );
    std::atomic<bool> invalid{false};
    if (chunks > 1) executor.reset_work_receipt();
    const auto evaluate_pair_chunk = [&](const int chunk) {
        double local_barrier = 0.0;
        double local_violation = -std::numeric_limits<double>::infinity();
        double local_margin = std::numeric_limits<double>::infinity();
        double* local_gradient = need_gradient
            ? chunk_gradient.data() + static_cast<std::size_t>(chunk * variables)
            : nullptr;
        for (int left = chunk; left < paper_case.turbine_count; left += chunks) {
            for (int right = left + 1; right < paper_case.turbine_count; ++right) {
                const double dx = layout[static_cast<std::size_t>(left)].x_m
                    - layout[static_cast<std::size_t>(right)].x_m;
                const double dy = layout[static_cast<std::size_t>(left)].y_m
                    - layout[static_cast<std::size_t>(right)].y_m;
                const double distance = std::sqrt(dx * dx + dy * dy);
                const double c = (paper_case.minimum_spacing_m - distance)
                    / paper_case.minimum_spacing_m;
                local_violation = std::max(local_violation, c);
                local_margin = std::min(local_margin, -c);
                if (!(c < -kStrictMargin)) {
                    invalid.store(true, std::memory_order_relaxed);
                    continue;
                }
                if (scaled_weight != 0.0) {
                    local_barrier -= scaled_weight * std::log(-c);
                    if (need_gradient) {
                        const double factor = -scaled_weight / c
                            * (-1.0 / paper_case.minimum_spacing_m)
                            / distance;
                        const std::size_t left_x = static_cast<std::size_t>(2 * left);
                        const std::size_t right_x = static_cast<std::size_t>(2 * right);
                        local_gradient[left_x] += factor * dx;
                        local_gradient[left_x + 1U] += factor * dy;
                        local_gradient[right_x] -= factor * dx;
                        local_gradient[right_x + 1U] -= factor * dy;
                    }
                }
            }
        }
        chunk_barrier[static_cast<std::size_t>(chunk)] = local_barrier;
        chunk_violation[static_cast<std::size_t>(chunk)] = local_violation;
        chunk_margin[static_cast<std::size_t>(chunk)] = local_margin;
    };
    if (chunks == 1) evaluate_pair_chunk(0);
    else executor.parallel_for(0, chunks, evaluate_pair_chunk);

    double serial_barrier = 0.0;
    double maximum_violation = *std::max_element(
        chunk_violation.begin(), chunk_violation.end()
    );
    double minimum_margin = *std::min_element(
        chunk_margin.begin(), chunk_margin.end()
    );
    std::vector<double> serial_gradient(
        need_gradient ? static_cast<std::size_t>(variables) : 0U, 0.0
    );
    auto consume_constraint = [&](
        const double c,
        const int turbine,
        const double gradient_x,
        const double gradient_y
    ) {
        maximum_violation = std::max(maximum_violation, c);
        minimum_margin = std::min(minimum_margin, -c);
        if (!(c < -kStrictMargin)) {
            invalid.store(true, std::memory_order_relaxed);
            return;
        }
        if (scaled_weight == 0.0) return;
        serial_barrier -= scaled_weight * std::log(-c);
        if (need_gradient) {
            const double factor = -scaled_weight / c;
            serial_gradient[static_cast<std::size_t>(2 * turbine)] +=
                factor * gradient_x;
            serial_gradient[static_cast<std::size_t>(2 * turbine + 1)] +=
                factor * gradient_y;
        }
    };

    for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
        const Point& point = layout[static_cast<std::size_t>(turbine)];
        if (paper_case.land_model == "rectangle") {
            consume_constraint(-point.x_m / paper_case.width_m, turbine,
                               -1.0 / paper_case.width_m, 0.0);
            consume_constraint((point.x_m - paper_case.width_m) / paper_case.width_m,
                               turbine, 1.0 / paper_case.width_m, 0.0);
            consume_constraint(-point.y_m / paper_case.height_m, turbine,
                               0.0, -1.0 / paper_case.height_m);
            consume_constraint((point.y_m - paper_case.height_m) / paper_case.height_m,
                               turbine, 0.0, 1.0 / paper_case.height_m);
        } else if (paper_case.land_model == "ring_750_800") {
            const double dx = point.x_m - 1000.0;
            const double dy = point.y_m - 1000.0;
            const double radius = std::sqrt(dx * dx + dy * dy);
            consume_constraint((750.0 - radius) / paper_case.minimum_spacing_m,
                               turbine,
                               -dx / (radius * paper_case.minimum_spacing_m),
                               -dy / (radius * paper_case.minimum_spacing_m));
            consume_constraint((radius - 800.0) / paper_case.minimum_spacing_m,
                               turbine,
                               dx / (radius * paper_case.minimum_spacing_m),
                               dy / (radius * paper_case.minimum_spacing_m));
        } else {
            const SignedDistance signed_distance = polygon_signed_distance(
                point, data_->polygon
            );
            consume_constraint(
                -signed_distance.value_m / paper_case.minimum_spacing_m,
                turbine,
                -signed_distance.derivative_x / paper_case.minimum_spacing_m,
                -signed_distance.derivative_y / paper_case.minimum_spacing_m
            );
        }
    }

    ConstraintReceipt result;
    result.maximum_violation = maximum_violation;
    result.minimum_normalized_margin = minimum_margin;
    result.pair_constraints = static_cast<std::uint64_t>(pair_count);
    result.land_constraints = static_cast<std::uint64_t>(
        land_per_turbine * paper_case.turbine_count
    );
    if (invalid.load(std::memory_order_relaxed) && barrier_weight != 0.0) {
        result.barrier_value = std::numeric_limits<double>::infinity();
    } else {
        result.barrier_value = serial_barrier + std::accumulate(
            chunk_barrier.begin(), chunk_barrier.end(), 0.0
        );
    }
    if (need_gradient) {
        result.barrier_gradient_per_m = std::move(serial_gradient);
        for (int chunk = 0; chunk < chunks; ++chunk) {
            for (int variable = 0; variable < variables; ++variable) {
                result.barrier_gradient_per_m[static_cast<std::size_t>(variable)] +=
                    chunk_gradient[static_cast<std::size_t>(chunk * variables + variable)];
            }
        }
    }
    result.requested_workers = executor.thread_count();
    result.observed_workers = 1;
    if (chunks > 1) {
        const fode::ExecutorWorkReceipt work = executor.work_receipt();
        result.observed_workers = std::max(1, work.distinct_participants);
    }
    result.seconds = seconds_since(start);
    return result;
}

std::vector<Point> Problem::initial_layout(
    const StartPolicy policy,
    const std::uint64_t seed,
    const int start_index
) const {
    const PaperCase& paper_case = data_->paper_case;
    std::vector<Point> candidates;
    const double target_spacing = 1.015 * paper_case.minimum_spacing_m;
    if (paper_case.land_model == "ring_750_800") {
        constexpr double radius = 775.0;
        const int count = static_cast<int>(std::floor(
            2.0 * kPi / (2.0 * std::asin(target_spacing / (2.0 * radius)))
        ));
        for (int index = 0; index < count; ++index) {
            const double angle = 2.0 * kPi * static_cast<double>(index)
                / static_cast<double>(count);
            candidates.push_back({
                1000.0 + radius * std::cos(angle),
                1000.0 + radius * std::sin(angle),
            });
        }
    } else {
        const double vertical = std::sqrt(3.0) * 0.5 * target_spacing;
        // The paper constrains hub coordinates to the farm bounds; it does
        // not impose a rotor-radius setback. A small strict barrier margin is
        // therefore correct and is essential for the N=37, density=6 case.
        const double boundary_margin = 0.005 * paper_case.minimum_spacing_m;
        for (int row = 0;; ++row) {
            const double y = boundary_margin
                + static_cast<double>(row) * vertical;
            if (y >= paper_case.height_m - boundary_margin) break;
            const double offset = row % 2 == 0 ? 0.0 : 0.5 * target_spacing;
            for (int column = 0;; ++column) {
                const double x = boundary_margin + offset
                    + static_cast<double>(column) * target_spacing;
                if (x >= paper_case.width_m - boundary_margin) break;
                const Point point{x, y};
                if (paper_case.land_model == "copenhagen_polygon") {
                    const SignedDistance distance = polygon_signed_distance(
                        point, data_->polygon
                    );
                    if (distance.value_m <= 0.05 * paper_case.minimum_spacing_m) {
                        continue;
                    }
                }
                candidates.push_back(point);
            }
        }
    }
    if (static_cast<int>(candidates.size()) < paper_case.turbine_count) {
        throw std::runtime_error("T08 feasible candidate lattice is too small");
    }

    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(paper_case.turbine_count));
    if (policy == StartPolicy::uniform_staggered) {
        for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
            const std::size_t index = static_cast<std::size_t>(
                (static_cast<std::uint64_t>(turbine) * candidates.size())
                / static_cast<std::uint64_t>(paper_case.turbine_count)
            );
            result.push_back(candidates[std::min(index, candidates.size() - 1U)]);
        }
    } else {
        std::vector<std::size_t> available(candidates.size());
        std::iota(available.begin(), available.end(), 0U);
        for (int turbine = 0; turbine < paper_case.turbine_count; ++turbine) {
            const double target_x = (
                static_cast<double>(turbine)
                + uniform01(seed, static_cast<std::uint64_t>(start_index),
                            static_cast<std::uint64_t>(turbine))
            ) / static_cast<double>(paper_case.turbine_count)
                * paper_case.width_m;
            const std::uint64_t permuted = mix64(
                seed ^ mix64(static_cast<std::uint64_t>(start_index + 1))
                ^ mix64(static_cast<std::uint64_t>(turbine + 1))
            );
            const double target_y = (
                static_cast<double>(permuted
                    % static_cast<std::uint64_t>(paper_case.turbine_count))
                + uniform01(seed, static_cast<std::uint64_t>(start_index + 17),
                            static_cast<std::uint64_t>(turbine))
            ) / static_cast<double>(paper_case.turbine_count)
                * paper_case.height_m;
            auto best = available.begin();
            double best_distance = std::numeric_limits<double>::infinity();
            for (auto candidate = available.begin(); candidate != available.end(); ++candidate) {
                const double dx = candidates[*candidate].x_m - target_x;
                const double dy = candidates[*candidate].y_m - target_y;
                const double squared = dx * dx + dy * dy;
                if (squared < best_distance) {
                    best_distance = squared;
                    best = candidate;
                }
            }
            result.push_back(candidates[*best]);
            available.erase(best);
        }
    }
    if (!feasible(result)) {
        throw std::runtime_error("T08 initializer failed strict feasibility");
    }
    return result;
}

bool Problem::feasible(const std::vector<Point>& layout) const {
    fode::PersistentExecutor executor(1);
    const ConstraintReceipt constraints = barrier(layout, 0.0, false, executor);
    return constraints.maximum_violation < -kStrictMargin;
}

double Problem::minimum_spacing(const std::vector<Point>& layout) const {
    double result = std::numeric_limits<double>::infinity();
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            result = std::min(result, std::hypot(
                layout[left].x_m - layout[right].x_m,
                layout[left].y_m - layout[right].y_m
            ));
        }
    }
    return result;
}

OptimizationReceipt optimize(
    const Problem& problem,
    const OptimizationConfig& config
) {
    if (config.starts <= 0 || config.workers <= 0
        || config.maximum_evaluations_per_start <= 2
        || config.barrier_phases <= 0) {
        throw std::invalid_argument("invalid T08 optimization configuration");
    }
    const Clock::time_point start = Clock::now();
    OptimizationReceipt result;
    result.case_id = problem.paper_case().case_id;
    result.problem_semantic_id = problem.paper_case().problem_semantic_id;
    result.method_semantic_id = kMethodSemanticId;
    result.start_policy = start_policy_name(config.start_policy);
    result.starts = config.starts;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.start_receipts.resize(static_cast<std::size_t>(config.starts));

    const int groups = std::min(config.starts, config.workers);
    const int workers_per_group = std::max(1, config.workers / groups);
    std::atomic<int> next_start{0};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(groups));
    for (int group = 0; group < groups; ++group) {
        threads.emplace_back([&] {
            fode::PersistentExecutor executor(workers_per_group);
            for (;;) {
                const int index = next_start.fetch_add(1, std::memory_order_relaxed);
                if (index >= config.starts) break;
                result.start_receipts[static_cast<std::size_t>(index)] =
                    optimize_start(problem, config, index, executor);
            }
        });
    }
    for (std::thread& thread : threads) thread.join();
    result.observed_workers = std::min(config.workers, groups * workers_per_group);

    const StartReceipt* best = nullptr;
    for (const StartReceipt& receipt : result.start_receipts) {
        result.physical_layout_evaluations += static_cast<std::uint64_t>(
            receipt.objective_gradient_evaluations
        );
        result.evaluator_seconds += receipt.evaluator_seconds;
        result.constraint_seconds += receipt.constraint_seconds;
        result.optimizer_seconds += receipt.optimizer_seconds;
        if (receipt.maximum_constraint_violation < -kStrictMargin
            && (best == nullptr
                || receipt.final_efficiency_percent
                    > best->final_efficiency_percent)) {
            best = &receipt;
        }
    }
    if (best == nullptr) throw std::runtime_error("T08 produced no feasible start");
    result.best_efficiency_percent = best->final_efficiency_percent;
    result.maximum_constraint_violation = best->maximum_constraint_violation;
    result.minimum_spacing_m = best->minimum_spacing_m;
    result.best_layout = best->final_layout;
    result.end_to_end_seconds = seconds_since(start);
    result.initialization_seconds = std::max(
        0.0,
        result.end_to_end_seconds
        - result.evaluator_seconds / static_cast<double>(std::max(1, groups))
        - result.constraint_seconds / static_cast<double>(std::max(1, groups))
    );
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(result.best_efficiency_percent));
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(result.maximum_constraint_violation));
    for (const Point& point : result.best_layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    result.scientific_hash = hash;
    return result;
}

std::vector<std::string> paper_case_ids() {
    std::vector<std::string> result;
    for (int wind_case = 1; wind_case <= 3; ++wind_case) {
        for (const int turbines : {10, 20, 30}) {
            result.push_back("t08_benchmark_c" + std::to_string(wind_case)
                + "_n" + std::to_string(turbines));
        }
    }
    for (const int turbines : {37, 50, 100}) {
        for (const int density : {4, 5, 6}) {
            result.push_back("t08_scaling_n" + std::to_string(turbines)
                + "_d" + std::to_string(density));
        }
    }
    result.push_back("t08_land_copenhagen_n37");
    result.push_back("t08_land_ring_n20");
    return result;
}

const char* start_policy_name(const StartPolicy policy) noexcept {
    switch (policy) {
        case StartPolicy::uniform_staggered: return "uniform_staggered";
        case StartPolicy::latin_hypercube_feasible:
            return "latin_hypercube_feasible_lattice";
    }
    return "unknown";
}

}  // namespace core99::t08
