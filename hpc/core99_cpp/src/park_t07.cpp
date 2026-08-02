/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T07 pure-C++ continuous-wake explicit SCP
Paper DOI: 10.1016/j.apenergy.2015.03.139.
Public source: no author code/data found; publisher manuscript consumed.
Missing: CVX files, CFD arrays and unreported numerical solver fields.
Reconstruction: forward AD, explicit trust SCP/damped BFGS, pinned NLopt QP.
Claim boundary: declared equation-level reproduction, not author replay.
Contract: shared/contracts/core99_t07_park_scp_2015.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/park_t07.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace core99::t07 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double diameter_m = 126.0;
constexpr double rotor_radius_m = 63.0;
constexpr double minimum_spacing_m = 5.0 * diameter_m;
constexpr double basis_length_m = 7.0 * diameter_m;
constexpr double basis_angle_radians =
    97.35 * pi / 180.0;  // Figure-10 row shift; diagonal rounds to 10.4D
constexpr double basis_bx = basis_length_m * std::cos(basis_angle_radians);
constexpr double basis_by = basis_length_m * std::sin(basis_angle_radians);
constexpr double acceptance_ratio = 0.2;
constexpr double expansion = 1.1;
constexpr double contraction = 0.5;
constexpr double initial_trust_m = 0.25 * diameter_m;
constexpr double initial_hessian_scale = 1.0e-7;
constexpr int pair_constraints =
    turbine_count * (turbine_count - 1) / 2;
constexpr int boundary_constraints = 4 * turbine_count;
constexpr int qp_constraint_count =
    pair_constraints + boundary_constraints + 1;
constexpr const char* method_id = "t07_explicit_scp_open_qp_declared_v1";

double elapsed(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Dual {
    double value = 0.0;
    std::array<double, variables> derivative{};
    Dual() = default;
    Dual(const double scalar) : value(scalar) {}
    static Dual independent(const double scalar, const int index) {
        Dual result(scalar);
        result.derivative[static_cast<std::size_t>(index)] = 1.0;
        return result;
    }
};

Dual operator+(const Dual& a, const Dual& b) {
    Dual r(a.value + b.value);
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] = a.derivative[i] + b.derivative[i];
    }
    return r;
}
Dual operator-(const Dual& a, const Dual& b) {
    Dual r(a.value - b.value);
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] = a.derivative[i] - b.derivative[i];
    }
    return r;
}
Dual operator-(const Dual& a) {
    Dual r(-a.value);
    for (int i = 0; i < variables; ++i) r.derivative[i] = -a.derivative[i];
    return r;
}
Dual operator*(const Dual& a, const Dual& b) {
    Dual r(a.value * b.value);
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] =
            a.derivative[i] * b.value + a.value * b.derivative[i];
    }
    return r;
}
Dual operator/(const Dual& a, const Dual& b) {
    Dual r(a.value / b.value);
    const double d = b.value * b.value;
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] =
            (a.derivative[i] * b.value - a.value * b.derivative[i]) / d;
    }
    return r;
}
Dual& operator+=(Dual& a, const Dual& b) {
    a = a + b;
    return a;
}
Dual sqrt(const Dual& x) {
    const double root = std::sqrt(std::max(0.0, x.value));
    Dual r(root);
    if (root <= 1.0e-18) return r;
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] = 0.5 * x.derivative[i] / root;
    }
    return r;
}
Dual exp(const Dual& x) {
    const double value = std::exp(x.value);
    Dual r(value);
    for (int i = 0; i < variables; ++i) {
        r.derivative[i] = value * x.derivative[i];
    }
    return r;
}

template<typename Scalar>
Scalar scalar_sqrt(const Scalar& x) {
    if constexpr (std::is_same_v<Scalar, double>) {
        return std::sqrt(std::max(0.0, x));
    } else {
        return sqrt(x);
    }
}

template<typename Scalar>
Scalar scalar_exp(const Scalar& x) {
    if constexpr (std::is_same_v<Scalar, double>) {
        return std::exp(x);
    } else {
        return exp(x);
    }
}

template<typename Scalar>
double numeric(const Scalar& x) {
    if constexpr (std::is_same_v<Scalar, double>) return x;
    else return x.value;
}

template<typename Scalar>
Scalar constant(const double x) {
    return Scalar(x);
}

struct SpeedBin {
    double speed_mps = 0.0;
    double probability = 0.0;
};

struct Direction {
    double from_degrees = 0.0;
    double probability = 0.0;
    std::vector<SpeedBin> speeds;
};

std::vector<SpeedBin> weibull_bins(
    const double scale,
    const double shape
) {
    auto cdf = [&](const double speed) {
        if (speed <= 0.0) return 0.0;
        return 1.0 - std::exp(-std::pow(speed / scale, shape));
    };
    std::vector<SpeedBin> result;
    for (int speed = 0; speed <= 30; ++speed) {
        const double probability = cdf(speed + 0.5) - cdf(speed - 0.5);
        result.push_back({static_cast<double>(speed), probability});
    }
    return result;
}

std::vector<Direction> expected_wind() {
    constexpr double direction_probability[12]{
        .051,.043,.044,.066,.089,.065,.087,.115,.121,.111,.114,.096
    };
    constexpr double scale[12]{
        8.65,8.86,8.15,9.98,11.35,10.96,
        11.28,11.50,11.08,10.94,11.27,10.55
    };
    constexpr double shape[12]{
        2.11,2.05,2.35,2.55,2.81,2.74,
        2.63,2.40,2.23,2.28,2.29,2.28
    };
    std::vector<Direction> result;
    for (int k = 0; k < 12; ++k) {
        result.push_back({
            30.0 * k,
            direction_probability[k],
            weibull_bins(scale[k], shape[k]),
        });
    }
    return result;
}

std::vector<Point> horns_rev_layout() {
    std::vector<Point> result;
    result.reserve(turbine_count);
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 10; ++column) {
            result.push_back({
                column * basis_length_m + row * basis_bx,
                row * basis_by,
            });
        }
    }
    return result;
}

double no_wake_power(const double speed) {
    if (speed < 3.0) return 0.0;
    return std::pow(std::min(speed, 12.0), 3.0);
}

template<typename Scalar>
Scalar wake_averaged_deficit(
    const Scalar& downstream,
    const Scalar& crosswind,
    const double wake_expansion
) {
    if (numeric(downstream) <= 0.0) return constant<Scalar>(0.0);
    const Scalar radius = rotor_radius_m + wake_expansion * downstream;
    constexpr double calibration = 0.9;
    constexpr double axial_induction = 1.0 / 3.0;
    Scalar sum = constant<Scalar>(0.0);
    // Four equal-area radial annuli and sixteen midpoint azimuths.
    constexpr int radial_samples = 4;
    constexpr int azimuth_samples = 16;
    for (int radial = 0; radial < radial_samples; ++radial) {
        const double local_radius = rotor_radius_m * std::sqrt(
            (static_cast<double>(radial) + 0.5) / radial_samples
        );
        for (int azimuth = 0; azimuth < azimuth_samples; ++azimuth) {
            const double angle = 2.0 * pi
                * (static_cast<double>(azimuth) + 0.5)
                / azimuth_samples;
            const double local_cross = local_radius * std::cos(angle);
            const double local_vertical = local_radius * std::sin(angle);
            const Scalar radial2 =
                (crosswind - local_cross) * (crosswind - local_cross)
                + local_vertical * local_vertical;
            const Scalar deficit = calibration * 2.0 * axial_induction
                * (rotor_radius_m * rotor_radius_m) / (radius * radius)
                * scalar_exp(-radial2 / (radius * radius));
            sum += deficit;
        }
    }
    return sum / static_cast<double>(radial_samples * azimuth_samples);
}

template<typename Scalar>
Scalar power_ratio(const Scalar& inflow_ratio, const double speed) {
    const double effective = speed * numeric(inflow_ratio);
    if (effective < 3.0) return constant<Scalar>(0.0);
    if (effective >= 12.0) {
        return constant<Scalar>(12.0 * 12.0 * 12.0);
    }
    return inflow_ratio * inflow_ratio * inflow_ratio
        * (speed * speed * speed);
}

template<typename Scalar>
Scalar turbine_direction_numerator(
    const std::vector<Scalar>& coordinates,
    const int target,
    const Direction& direction,
    const double wake_expansion
) {
    const double radians = direction.from_degrees * pi / 180.0;
    const double flow_x = -std::sin(radians);
    const double flow_y = -std::cos(radians);
    const double cross_x = -flow_y;
    const double cross_y = flow_x;
    Scalar squared = constant<Scalar>(0.0);
    for (int source = 0; source < turbine_count; ++source) {
        if (source == target) continue;
        const Scalar dx =
            coordinates[target] - coordinates[source];
        const Scalar dy =
            coordinates[turbine_count + target]
            - coordinates[turbine_count + source];
        const Scalar downstream = dx * flow_x + dy * flow_y;
        const Scalar crosswind = dx * cross_x + dy * cross_y;
        const Scalar deficit = wake_averaged_deficit(
            downstream, crosswind, wake_expansion
        );
        squared += deficit * deficit;
    }
    Scalar inflow = constant<Scalar>(1.0) - scalar_sqrt(squared);
    if (numeric(inflow) < 0.0) inflow = constant<Scalar>(0.0);
    Scalar result = constant<Scalar>(0.0);
    for (const auto& bin : direction.speeds) {
        result += bin.probability * power_ratio(inflow, bin.speed_mps);
    }
    return direction.probability * result;
}

std::vector<double> flatten(const std::vector<Point>& points) {
    std::vector<double> x(variables);
    for (int i = 0; i < turbine_count; ++i) {
        x[i] = points[i].x_m;
        x[turbine_count + i] = points[i].y_m;
    }
    return x;
}

std::vector<Point> unflatten(const double* x) {
    std::vector<Point> points(turbine_count);
    for (int i = 0; i < turbine_count; ++i) {
        points[i] = {x[i], x[turbine_count + i]};
    }
    return points;
}

double norm(const std::vector<double>& x) {
    return std::sqrt(std::inner_product(x.begin(), x.end(), x.begin(), 0.0));
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    return std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
}

std::uint64_t mix_hash(std::uint64_t h, const std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U);
    return h;
}

struct QpContext {
    const std::vector<double>* current = nullptr;
    const std::vector<double>* gradient = nullptr;
    const std::vector<double>* positive_hessian = nullptr;
    double trust_radius = 0.0;
    int evaluations = 0;
};

double qp_objective(
    const unsigned n,
    const double* x,
    double* gradient,
    void* opaque
) {
    auto& context = *static_cast<QpContext*>(opaque);
    if (n != variables) return std::numeric_limits<double>::infinity();
    ++context.evaluations;
    const auto& current = *context.current;
    const auto& g = *context.gradient;
    const auto& a = *context.positive_hessian;
    std::vector<double> step(variables);
    for (int i = 0; i < variables; ++i) step[i] = x[i] - current[i];
    double quadratic = 0.0;
    for (int i = 0; i < variables; ++i) {
        double row = 0.0;
        for (int j = 0; j < variables; ++j) {
            row += a[i * variables + j] * step[j];
        }
        quadratic += step[i] * row;
        if (gradient) gradient[i] = -g[i] + row;
    }
    return -dot(g, step) + 0.5 * quadratic;
}

void qp_constraints(
    const unsigned m,
    double* result,
    const unsigned n,
    const double* x,
    double* jacobian,
    void* opaque
) {
    auto& context = *static_cast<QpContext*>(opaque);
    if (m != qp_constraint_count || n != variables) return;
    if (jacobian) {
        std::fill(
            jacobian,
            jacobian + static_cast<std::size_t>(m) * variables,
            0.0
        );
    }
    const auto& current = *context.current;
    int row = 0;
    for (int left = 0; left < turbine_count; ++left) {
        for (int right = left + 1; right < turbine_count; ++right) {
            const double dx0 = current[left] - current[right];
            const double dy0 = current[turbine_count + left]
                - current[turbine_count + right];
            const double distance = std::hypot(dx0, dy0);
            const double scale = minimum_spacing_m * distance;
            const double dx = x[left] - x[right];
            const double dy =
                x[turbine_count + left] - x[turbine_count + right];
            result[row] = (scale - dx0 * dx - dy0 * dy) / scale;
            if (jacobian) {
                double* g = jacobian
                    + static_cast<std::size_t>(row) * variables;
                g[left] = -dx0 / scale;
                g[right] = dx0 / scale;
                g[turbine_count + left] = -dy0 / scale;
                g[turbine_count + right] = dy0 / scale;
            }
            ++row;
        }
    }
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        const double px = x[turbine];
        const double py = x[turbine_count + turbine];
        const double v = py / basis_by;
        const double u = (px - basis_bx * v) / basis_length_m;
        const double du_dx = 1.0 / basis_length_m;
        const double du_dy =
            -basis_bx / (basis_length_m * basis_by);
        const double dv_dy = 1.0 / basis_by;
        const std::array<double, 4> values{-u, u - 9.0, -v, v - 7.0};
        for (int side = 0; side < 4; ++side) {
            result[row] = values[side];
            if (jacobian) {
                double* g = jacobian
                    + static_cast<std::size_t>(row) * variables;
                const double sign = (side == 0 || side == 2) ? -1.0 : 1.0;
                if (side < 2) {
                    g[turbine] = sign * du_dx;
                    g[turbine_count + turbine] = sign * du_dy;
                } else {
                    g[turbine_count + turbine] = sign * dv_dy;
                }
            }
            ++row;
        }
    }
    double squared = 0.0;
    for (int i = 0; i < variables; ++i) {
        const double step = x[i] - current[i];
        squared += step * step;
        if (jacobian) {
            jacobian[
                static_cast<std::size_t>(row) * variables + i
            ] = 2.0 * step
                / (context.trust_radius * context.trust_radius);
        }
    }
    result[row] = squared
        / (context.trust_radius * context.trust_radius) - 1.0;
}

void damped_bfgs_update(
    std::vector<double>& a,
    const std::vector<double>& step,
    const std::vector<double>& old_gradient,
    const std::vector<double>& new_gradient
) {
    std::vector<double> y(variables);
    std::vector<double> a_step(variables, 0.0);
    for (int i = 0; i < variables; ++i) {
        y[i] = -(new_gradient[i] - old_gradient[i]);
        for (int j = 0; j < variables; ++j) {
            a_step[i] += a[i * variables + j] * step[j];
        }
    }
    const double s_as = dot(step, a_step);
    const double s_y = dot(step, y);
    if (!(s_as > 1.0e-18)) return;
    double theta = 1.0;
    if (s_y < 0.2 * s_as) {
        theta = 0.8 * s_as / (s_as - s_y);
    }
    std::vector<double> r(variables);
    for (int i = 0; i < variables; ++i) {
        r[i] = theta * y[i] + (1.0 - theta) * a_step[i];
    }
    const double s_r = dot(step, r);
    if (!(s_r > 1.0e-18)) return;
    for (int i = 0; i < variables; ++i) {
        for (int j = 0; j < variables; ++j) {
            a[i * variables + j] +=
                -a_step[i] * a_step[j] / s_as + r[i] * r[j] / s_r;
        }
    }
}

}  // namespace

struct Problem::Data {
    std::vector<Point> initial = horns_rev_layout();
    std::vector<Direction> directions;
    double wake_expansion = 0.033;
    double published_initial = 0.0;
    double published_final = 0.0;
};

Problem::Problem(std::string case_id) : case_id_(std::move(case_id)) {
    auto data = std::make_shared<Data>();
    if (case_id_ == "t07_single_0"
        || case_id_ == "t07_single_41"
        || case_id_ == "t07_single_90") {
        const double direction = case_id_ == "t07_single_0" ? 0.0
            : (case_id_ == "t07_single_41" ? 41.0 : 90.0);
        data->directions = {{direction, 1.0, {{8.0, 1.0}}}};
        data->published_initial = direction == 0.0 ? .828
            : (direction == 41.0 ? .583 : .432);
        data->published_final = direction == 0.0 ? .913
            : (direction == 41.0 ? .908 : .873);
        semantic_id_ = "t07_hornsrev80_single_direction_v1";
    } else {
        double expansion_value = 0.033;
        if (case_id_.starts_with("t07_expected_k")) {
            expansion_value = std::stod(case_id_.substr(14)) / 1000.0;
        } else {
            throw std::invalid_argument("invalid T07 case " + case_id_);
        }
        data->wake_expansion = expansion_value;
        data->directions = expected_wind();
        semantic_id_ = expansion_value == .033
            ? "t07_hornsrev80_expected_wind_v1"
            : "t07_hornsrev80_k_sensitivity_v1";
        if (std::abs(expansion_value - .033) < 1e-12) {
            data->published_initial = .836;
            data->published_final = .898;
        } else {
            constexpr double ks[5]{.030,.035,.040,.045,.050};
            constexpr double initial[5]{.828,.843,.856,.868,.877};
            constexpr double final[5]{.892,.898,.904,.910,.916};
            bool found = false;
            for (int i = 0; i < 5; ++i) {
                if (std::abs(expansion_value - ks[i]) < 1e-12) {
                    data->published_initial = initial[i];
                    data->published_final = final[i];
                    found = true;
                }
            }
            if (!found) throw std::invalid_argument("invalid T07 k case");
        }
    }
    data_ = std::move(data);
}

const std::string& Problem::case_id() const noexcept { return case_id_; }
const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}
const std::vector<Point>& Problem::initial_layout() const noexcept {
    return data_->initial;
}
double Problem::wake_expansion() const noexcept {
    return data_->wake_expansion;
}
double Problem::published_initial_efficiency() const noexcept {
    return data_->published_initial;
}
double Problem::published_optimized_efficiency() const noexcept {
    return data_->published_final;
}

double Problem::maximum_constraint_violation(
    const std::vector<Point>& layout
) const {
    if (layout.size() != turbine_count) {
        return std::numeric_limits<double>::infinity();
    }
    double violation = 0.0;
    for (int i = 0; i < turbine_count; ++i) {
        const double v = layout[i].y_m / basis_by;
        const double u =
            (layout[i].x_m - basis_bx * v) / basis_length_m;
        violation = std::max({
            violation,
            -u * basis_length_m,
            (u - 9.0) * basis_length_m,
            -v * basis_length_m,
            (v - 7.0) * basis_length_m,
        });
        for (int j = i + 1; j < turbine_count; ++j) {
            violation = std::max(
                violation,
                minimum_spacing_m - std::hypot(
                    layout[i].x_m - layout[j].x_m,
                    layout[i].y_m - layout[j].y_m
                )
            );
        }
    }
    return std::max(0.0, violation);
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const bool calculate_gradient,
    fode::PersistentExecutor& executor
) const {
    if (layout.size() != turbine_count) {
        throw std::invalid_argument("T07 layout size");
    }
    const auto start = Clock::now();
    executor.reset_work_receipt();
    const int directions = static_cast<int>(data_->directions.size());
    const int tasks = directions * turbine_count;
    double denominator = 0.0;
    for (const auto& direction : data_->directions) {
        for (const auto& bin : direction.speeds) {
            denominator += direction.probability * bin.probability
                * no_wake_power(bin.speed_mps) * turbine_count;
        }
    }
    Evaluation result;
    if (calculate_gradient) {
        std::vector<Dual> coordinates(variables);
        for (int i = 0; i < turbine_count; ++i) {
            coordinates[i] = Dual::independent(layout[i].x_m, i);
            coordinates[turbine_count + i] =
                Dual::independent(layout[i].y_m, turbine_count + i);
        }
        std::vector<Dual> values(tasks);
        executor.parallel_for(0, tasks, [&](const int task) {
            const int direction = task / turbine_count;
            const int target = task % turbine_count;
            values[task] = turbine_direction_numerator(
                coordinates,
                target,
                data_->directions[direction],
                data_->wake_expansion
            );
        });
        Dual total;
        for (const auto& value : values) total += value;
        result.efficiency = total.value / denominator;
        result.gradient.resize(variables);
        for (int i = 0; i < variables; ++i) {
            result.gradient[i] = total.derivative[i] / denominator;
        }
    } else {
        std::vector<double> coordinates = flatten(layout);
        std::vector<double> values(tasks);
        executor.parallel_for(0, tasks, [&](const int task) {
            const int direction = task / turbine_count;
            const int target = task % turbine_count;
            values[task] = turbine_direction_numerator(
                coordinates,
                target,
                data_->directions[direction],
                data_->wake_expansion
            );
        });
        result.efficiency =
            std::accumulate(values.begin(), values.end(), 0.0) / denominator;
    }
    const auto receipt = executor.work_receipt();
    result.requested_workers = executor.thread_count();
    result.observed_workers = receipt.distinct_participants;
    result.seconds = elapsed(start);
    return result;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.maximum_scp_iterations < 1
        || config.maximum_qp_evaluations < 1 || config.epsilon_m <= 0.0) {
        throw std::invalid_argument("T07 invalid run config");
    }
    const auto total_start = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    auto layout = problem.initial_layout();
    std::vector<double> current = flatten(layout);
    Evaluation current_evaluation =
        problem.evaluate(layout, true, executor);
    const Evaluation initial = current_evaluation;
    double evaluator_seconds = current_evaluation.seconds;
    double qp_seconds = 0.0;
    double trust = initial_trust_m;
    std::vector<double> a(
        static_cast<std::size_t>(variables) * variables, 0.0
    );
    for (int i = 0; i < variables; ++i) {
        a[i * variables + i] = initial_hessian_scale;
    }
    std::vector<ScpStage> stages;
    int observed_workers = current_evaluation.observed_workers;
    for (int iteration = 0;
         iteration < config.maximum_scp_iterations;
         ++iteration) {
        const auto stage_start = Clock::now();
        QpContext context{
            .current = &current,
            .gradient = &current_evaluation.gradient,
            .positive_hessian = &a,
            .trust_radius = trust,
        };
        std::vector<double> proposed = current;
        nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, variables);
        if (!optimizer) throw std::runtime_error("T07 NLopt create");
        nlopt_set_min_objective(optimizer, qp_objective, &context);
        std::vector<double> tolerance(qp_constraint_count, 1.0e-10);
        nlopt_add_inequality_mconstraint(
            optimizer,
            qp_constraint_count,
            qp_constraints,
            &context,
            tolerance.data()
        );
        nlopt_set_ftol_rel(optimizer, 1.0e-11);
        nlopt_set_xtol_rel(optimizer, 1.0e-10);
        nlopt_set_maxeval(optimizer, config.maximum_qp_evaluations);
        double qp_minimum = 0.0;
        const auto qp_start = Clock::now();
        const nlopt_result status =
            nlopt_optimize(optimizer, proposed.data(), &qp_minimum);
        qp_seconds += elapsed(qp_start);
        nlopt_destroy(optimizer);

        std::vector<double> step(variables);
        for (int i = 0; i < variables; ++i) {
            step[i] = proposed[i] - current[i];
        }
        const double step_norm = norm(step);
        const auto proposed_layout = unflatten(proposed.data());
        const double violation =
            problem.maximum_constraint_violation(proposed_layout);
        Evaluation proposed_evaluation;
        double predicted = 0.0;
        if (status > 0 || status == NLOPT_ROUNDOFF_LIMITED) {
            proposed_evaluation =
                problem.evaluate(proposed_layout, true, executor);
            evaluator_seconds += proposed_evaluation.seconds;
            observed_workers = std::max(
                observed_workers, proposed_evaluation.observed_workers
            );
            double quadratic = 0.0;
            for (int i = 0; i < variables; ++i) {
                double row = 0.0;
                for (int j = 0; j < variables; ++j) {
                    row += a[i * variables + j] * step[j];
                }
                quadratic += step[i] * row;
            }
            predicted = dot(current_evaluation.gradient, step)
                - 0.5 * quadratic;
        } else {
            proposed_evaluation = current_evaluation;
        }
        const double actual = proposed_evaluation.efficiency
            - current_evaluation.efficiency;
        const double ratio = predicted > 1.0e-16
            ? actual / predicted : -std::numeric_limits<double>::infinity();
        const bool accepted =
            (status > 0 || status == NLOPT_ROUNDOFF_LIMITED)
            && violation <= 1.0e-5
            && actual >= 0.0
            && ratio >= acceptance_ratio;
        const double before = current_evaluation.efficiency;
        const double proposed_efficiency = proposed_evaluation.efficiency;
        if (accepted) {
            damped_bfgs_update(
                a,
                step,
                current_evaluation.gradient,
                proposed_evaluation.gradient
            );
            current = std::move(proposed);
            layout = proposed_layout;
            current_evaluation = std::move(proposed_evaluation);
            trust *= expansion;
        } else {
            trust *= contraction;
        }
        stages.push_back({
            .iteration = iteration,
            .accepted = accepted,
            .qp_status = static_cast<int>(status),
            .qp_evaluations = context.evaluations,
            .trust_radius_m = context.trust_radius,
            .initial_efficiency = before,
            .proposed_efficiency = proposed_efficiency,
            .actual_predicted_ratio = ratio,
            .step_norm_m = step_norm,
            .maximum_constraint_violation_m = violation,
            .seconds = elapsed(stage_start),
        });
        if ((accepted && step_norm < config.epsilon_m)
            || trust < 1.0e-4 * diameter_m) {
            break;
        }
    }
    Evaluation final = problem.evaluate(layout, true, executor);
    evaluator_seconds += final.seconds;
    observed_workers = std::max(observed_workers, final.observed_workers);
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& point : layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(final.efficiency));
    return {
        .case_id = problem.case_id(),
        .problem_semantic_id = problem.semantic_id(),
        .method_semantic_id = method_id,
        .requested_workers = config.workers,
        .observed_workers = observed_workers,
        .wake_expansion = problem.wake_expansion(),
        .published_initial_efficiency =
            problem.published_initial_efficiency(),
        .published_optimized_efficiency =
            problem.published_optimized_efficiency(),
        .initial = initial,
        .final = final,
        .final_layout = layout,
        .stages = std::move(stages),
        .maximum_constraint_violation_m =
            problem.maximum_constraint_violation(layout),
        .evaluator_seconds = evaluator_seconds,
        .qp_seconds = qp_seconds,
        .end_to_end_seconds = elapsed(total_start),
        .scientific_hash = hash,
    };
}

std::vector<std::string> paper_case_ids() {
    return {
        "t07_single_0",
        "t07_single_41",
        "t07_single_90",
        "t07_expected_k033",
        "t07_expected_k030",
        "t07_expected_k035",
        "t07_expected_k040",
        "t07_expected_k045",
        "t07_expected_k050",
    };
}

}  // namespace core99::t07
