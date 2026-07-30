/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T16 pure-C++ evaluator, exact-gradient WEC and open SQP
Paper/DOI: Comparison of Wind Farm Layout Optimization Results Using a
Simple Wake Model and Gradient-Based Optimization to Large Eddy Simulations;
10.2514/6.2019-0538
Public source, missing facts, conflict resolutions, semantic IDs, backend and
claim boundary: hpc/core99_cpp/include/core99/thomas_t16.hpp
Controlling contract: shared/contracts/core99_t16_thomas_2019.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/thomas_t16.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace core99::t16 {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double diameter_m = 126.4;
constexpr double rotor_radius_m = 0.5 * diameter_m;
constexpr double hub_height_m = 90.0;
constexpr double reference_height_m = 80.0;
constexpr double air_density_kg_m3 = 1.225;
constexpr double generator_efficiency = 0.944;
constexpr double ambient_ti = 0.108;
constexpr double shear_exponent = 0.31;
constexpr double physical_farm_radius_m = 2000.0;
constexpr double hub_centre_radius_m =
    physical_farm_radius_m - rotor_radius_m;
constexpr double minimum_spacing_m = 2.0 * diameter_m;
constexpr double rated_power_mw = 5.0;
constexpr double hours_per_year = 8760.0;
constexpr int variables = 2 * turbine_count;
constexpr int pair_constraints =
    turbine_count * (turbine_count - 1) / 2;
constexpr int total_constraints = pair_constraints + turbine_count;
constexpr std::array<char, 8> data_magic = {
    'T', '1', '6', 'D', 'A', 'T', 'A', '1',
};

using std::acos;
using std::exp;
using std::log;
using std::pow;
using std::sqrt;

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

    Dual& operator+=(const Dual& other);
    Dual& operator-=(const Dual& other);
};

Dual operator+(const Dual& left, const Dual& right) {
    Dual result(left.value + right.value);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)]
            + right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator-(const Dual& left, const Dual& right) {
    Dual result(left.value - right.value);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)]
            - right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator-(const Dual& value) {
    Dual result(-value.value);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            -value.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator*(const Dual& left, const Dual& right) {
    Dual result(left.value * right.value);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)] * right.value
            + left.value
                * right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator/(const Dual& left, const Dual& right) {
    Dual result(left.value / right.value);
    const double denominator = right.value * right.value;
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] = (
            left.derivative[static_cast<std::size_t>(index)] * right.value
            - left.value
                * right.derivative[static_cast<std::size_t>(index)]
        ) / denominator;
    }
    return result;
}

Dual& Dual::operator+=(const Dual& other) {
    *this = *this + other;
    return *this;
}

Dual& Dual::operator-=(const Dual& other) {
    *this = *this - other;
    return *this;
}

Dual sqrt(const Dual& input) {
    const double root = std::sqrt(input.value);
    Dual result(root);
    const double multiplier = 0.5 / root;
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            multiplier * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual exp(const Dual& input) {
    const double exponential = std::exp(input.value);
    Dual result(exponential);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            exponential * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual log(const Dual& input) {
    Dual result(std::log(input.value));
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            input.derivative[static_cast<std::size_t>(index)] / input.value;
    }
    return result;
}

Dual acos(const Dual& input) {
    Dual result(std::acos(input.value));
    const double multiplier =
        -1.0 / std::sqrt(1.0 - input.value * input.value);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            multiplier * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual pow(const Dual& input, const double exponent) {
    const double powered = std::pow(input.value, exponent);
    Dual result(powered);
    const double multiplier =
        exponent * std::pow(input.value, exponent - 1.0);
    for (int index = 0; index < variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            multiplier * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

template<typename Scalar>
double numeric_value(const Scalar& value) {
    if constexpr (std::is_same_v<Scalar, double>) {
        return value;
    } else {
        return value.value;
    }
}

template<typename Scalar>
Scalar scalar_constant(const double value) {
    return Scalar(value);
}

double radians(const double degrees) {
    return degrees * pi / 180.0;
}

std::string turbulence_name(const TurbulenceMode mode) {
    switch (mode) {
        case TurbulenceMode::ambient_only: return "ambient_only";
        case TurbulenceMode::smooth_local: return "smooth_local";
        case TurbulenceMode::hard_local: return "hard_local";
    }
    return "unknown";
}

std::string nlopt_status_name(const nlopt_result status) {
    switch (status) {
        case NLOPT_FAILURE: return "failure";
        case NLOPT_INVALID_ARGS: return "invalid_args";
        case NLOPT_OUT_OF_MEMORY: return "out_of_memory";
        case NLOPT_ROUNDOFF_LIMITED: return "roundoff_limited";
        case NLOPT_FORCED_STOP: return "forced_stop";
        case NLOPT_SUCCESS: return "success";
        case NLOPT_STOPVAL_REACHED: return "stopval_reached";
        case NLOPT_FTOL_REACHED: return "ftol_reached";
        case NLOPT_XTOL_REACHED: return "xtol_reached";
        case NLOPT_MAXEVAL_REACHED: return "maxeval_reached";
        case NLOPT_MAXTIME_REACHED: return "maxtime_reached";
        default: return "unknown";
    }
}

template<typename Scalar>
Scalar stable_smooth_max(
    const Scalar& left,
    const Scalar& right,
    const double smoothing
) {
    const double left_value = numeric_value(left);
    const double right_value = numeric_value(right);
    const Scalar& maximum = left_value >= right_value ? left : right;
    const Scalar& minimum = left_value >= right_value ? right : left;
    const Scalar scaled = smoothing * (minimum - maximum);
    if (numeric_value(scaled) < -40.0) return maximum;
    return maximum + log(scalar_constant<Scalar>(1.0) + exp(scaled))
        / smoothing;
}

template<typename Scalar>
Scalar interpolate(
    const Scalar& x,
    const std::vector<double>& xs,
    const std::vector<double>& ys
) {
    const double value = numeric_value(x);
    if (value <= xs.front()) return scalar_constant<Scalar>(ys.front());
    if (value >= xs.back()) return scalar_constant<Scalar>(ys.back());
    const auto upper = std::upper_bound(xs.begin(), xs.end(), value);
    const std::size_t right = static_cast<std::size_t>(
        std::distance(xs.begin(), upper)
    );
    const std::size_t left = right - 1U;
    const double width = xs[right] - xs[left];
    if (width <= 0.0) return scalar_constant<Scalar>(ys[left]);
    const double slope = (ys[right] - ys[left]) / width;
    return scalar_constant<Scalar>(ys[left])
        + (x - xs[left]) * slope;
}

template<typename Scalar>
Scalar circle_overlap_area(
    const Scalar& distance,
    const Scalar& wake_radius
) {
    const double d = std::abs(numeric_value(distance));
    const double r = std::max(1.0e-9, numeric_value(wake_radius));
    constexpr double rotor = rotor_radius_m;
    if (d >= r + rotor) return scalar_constant<Scalar>(0.0);
    if (d <= std::abs(r - rotor)) {
        const Scalar smaller = r <= rotor
            ? wake_radius
            : scalar_constant<Scalar>(rotor);
        return pi * smaller * smaller;
    }
    const Scalar d_safe = sqrt(
        distance * distance + scalar_constant<Scalar>(1.0e-20)
    );
    const Scalar cosine_wake = (
        d_safe * d_safe + wake_radius * wake_radius - rotor * rotor
    ) / (2.0 * d_safe * wake_radius);
    const Scalar cosine_rotor = (
        d_safe * d_safe + rotor * rotor - wake_radius * wake_radius
    ) / (2.0 * d_safe * rotor);
    const Scalar product =
        (-d_safe + wake_radius + rotor)
        * (d_safe + wake_radius - rotor)
        * (d_safe - wake_radius + rotor)
        * (d_safe + wake_radius + rotor);
    return wake_radius * wake_radius * acos(cosine_wake)
        + rotor * rotor * acos(cosine_rotor)
        - 0.5 * sqrt(product);
}

template<typename Scalar, typename DataT>
Scalar thrust_coefficient(const Scalar& speed, const DataT& data) {
    const double value = numeric_value(speed);
    if (value < 3.0 || value > 25.0) {
        return scalar_constant<Scalar>(0.0);
    }
    return interpolate(speed, data.speed_mps, data.ct);
}

template<typename Scalar, typename DataT>
Scalar turbine_power_mw(const Scalar& speed, const DataT& data) {
    const double value = numeric_value(speed);
    if (value < 3.0 || value > 25.0) {
        return scalar_constant<Scalar>(0.0);
    }
    const Scalar cp = interpolate(speed, data.speed_mps, data.cp);
    const Scalar raw = 0.5 * air_density_kg_m3
        * (pi * rotor_radius_m * rotor_radius_m)
        * cp * speed * speed * speed
        * generator_efficiency / 1.0e6;
    if (numeric_value(raw) >= rated_power_mw) {
        return scalar_constant<Scalar>(rated_power_mw);
    }
    return raw;
}

template<typename Scalar, typename DataT>
Scalar wake_deficit_ratio(
    const Scalar& downstream_m,
    const Scalar& lateral_m,
    const Scalar& vertical_m,
    const Scalar& source_ti,
    const Scalar& source_speed,
    const double wec_factor,
    const DataT& data
) {
    if (numeric_value(downstream_m) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    Scalar ct = thrust_coefficient(source_speed, data);
    if (numeric_value(ct) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    // Eq. (4) is undefined for CT >= 1.  The public NREL table contains
    // low-speed interpolation points slightly above one, so retain the
    // curve for power but cap the analytical wake CT at the physical limit.
    if (numeric_value(ct) >= 0.999) {
        ct = scalar_constant<Scalar>(0.999);
    }
    const Scalar root = sqrt(scalar_constant<Scalar>(1.0) - ct);
    const Scalar k = 0.3837 * source_ti + 0.003678;
    const Scalar x0 = diameter_m * (
        scalar_constant<Scalar>(1.0) + root
    ) / (
        std::sqrt(2.0)
        * (2.32 * source_ti + 0.154 * (
            scalar_constant<Scalar>(1.0) - root
        ))
    );

    const Scalar a = k * k;
    const Scalar b = diameter_m * (2.0 * k) / std::sqrt(8.0);
    const Scalar c = diameter_m * diameter_m * (ct - 1.0) / 8.0;
    const Scalar discriminant = b * b - 4.0 * a * c;
    const Scalar defined_offset = (
        -b + sqrt(discriminant)
    ) / (2.0 * a);
    const Scalar xd = x0 + defined_offset;
    const bool in_near_wake =
        numeric_value(downstream_m) < numeric_value(xd);
    const Scalar physical_sigma = in_near_wake
        ? k * defined_offset + diameter_m / std::sqrt(8.0)
        : k * (downstream_m - x0) + diameter_m / std::sqrt(8.0);
    const Scalar radicand = scalar_constant<Scalar>(1.0)
        - ct * diameter_m * diameter_m
        / (8.0 * physical_sigma * physical_sigma);
    const Scalar centre = scalar_constant<Scalar>(1.0) - sqrt(radicand);
    const Scalar gaussian_sigma = wec_factor * physical_sigma;
    return centre
        * exp(-0.5 * lateral_m * lateral_m
            / (gaussian_sigma * gaussian_sigma))
        * exp(-0.5 * vertical_m * vertical_m
            / (gaussian_sigma * gaussian_sigma));
}

template<typename Scalar, typename DataT>
Scalar added_turbulence_candidate(
    const Scalar& downstream_m,
    const Scalar& lateral_m,
    const Scalar& source_ti,
    const Scalar& source_speed,
    const DataT& data
) {
    Scalar ct = thrust_coefficient(source_speed, data);
    if (numeric_value(ct) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    if (numeric_value(ct) >= 0.999) {
        ct = scalar_constant<Scalar>(0.999);
    }
    const Scalar root = sqrt(scalar_constant<Scalar>(1.0) - ct);
    const Scalar beta = 0.5 * (
        scalar_constant<Scalar>(1.0) + root
    ) / root;
    const Scalar epsilon = 0.2 * sqrt(beta);
    const Scalar k = 0.3837 * source_ti + 0.003678;
    const Scalar sigma = k * downstream_m + diameter_m * epsilon;
    const Scalar wake_radius = 2.0 * sigma;
    const Scalar overlap = circle_overlap_area(lateral_m, wake_radius)
        / (pi * rotor_radius_m * rotor_radius_m);

    Scalar axial_induction;
    if (numeric_value(ct) > 0.96) {
        axial_induction = 0.143 + sqrt(
            scalar_constant<Scalar>(0.0203)
            - 0.6427 * (0.889 - ct)
        );
    } else {
        axial_induction = 0.5 * (
            scalar_constant<Scalar>(1.0)
            - sqrt(scalar_constant<Scalar>(1.0) - ct)
        );
    }
    return 0.73 * pow(axial_induction, 0.8325)
        * pow(source_ti, 0.0325)
        * pow(downstream_m / diameter_m, -0.32)
        * overlap;
}

std::vector<std::array<double, 2>> rotor_samples(const int count) {
    if (count <= 1) return {{0.0, 0.0}};
    std::vector<std::array<double, 2>> samples;
    samples.reserve(static_cast<std::size_t>(count));
    const double golden_ratio = (std::sqrt(5.0) + 1.0) / 2.0;
    const int boundary_points = static_cast<int>(
        std::llround(std::sqrt(static_cast<double>(count)))
    );
    for (int index = 0; index < count; ++index) {
        const int k = index + 1;
        const double radius = k > count - boundary_points
            ? 1.0
            : std::sqrt(static_cast<double>(k) - 0.5)
                / std::sqrt(
                    static_cast<double>(count)
                    - (static_cast<double>(boundary_points) + 1.0) / 2.0
                );
        const double theta =
            2.0 * pi * static_cast<double>(k)
            / (golden_ratio * golden_ratio);
        samples.push_back({
            rotor_radius_m * radius * std::cos(theta),
            rotor_radius_m * radius * std::sin(theta),
        });
    }
    return samples;
}

template<typename Scalar, typename DataT>
Scalar direction_power_mw(
    const std::vector<Scalar>& coordinates,
    const std::vector<Point>& numeric_layout,
    const int wind_index,
    const EvaluationSettings& settings,
    const DataT& data
) {
    const double from_radians = radians(
        data.wind_direction_degrees[static_cast<std::size_t>(wind_index)]
    );
    const double flow_x = -std::sin(from_radians);
    const double flow_y = -std::cos(from_radians);
    const double cross_x = -flow_y;
    const double cross_y = flow_x;
    std::array<double, turbine_count> streamwise_numeric{};
    std::array<int, turbine_count> order{};
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        streamwise_numeric[static_cast<std::size_t>(turbine)] =
            numeric_layout[static_cast<std::size_t>(turbine)].x_m * flow_x
            + numeric_layout[static_cast<std::size_t>(turbine)].y_m * flow_y;
        order[static_cast<std::size_t>(turbine)] = turbine;
    }
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        const double l = streamwise_numeric[static_cast<std::size_t>(left)];
        const double r = streamwise_numeric[static_cast<std::size_t>(right)];
        return l == r ? left < right : l < r;
    });

    const auto samples = rotor_samples(settings.rotor_sample_points);
    std::array<Scalar, turbine_count> inflow{};
    std::array<Scalar, turbine_count> local_ti{};
    const double reference_speed =
        data.wind_speed_mps[static_cast<std::size_t>(wind_index)];

    for (int position = 0; position < turbine_count; ++position) {
        const int target = order[static_cast<std::size_t>(position)];
        Scalar added_ti = scalar_constant<Scalar>(0.0);
        if (settings.turbulence_mode != TurbulenceMode::ambient_only) {
            for (int upstream_position = 0;
                 upstream_position < position;
                 ++upstream_position) {
                const int source =
                    order[static_cast<std::size_t>(upstream_position)];
                const Scalar dx =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)])
                        * flow_x
                    + (coordinates[
                        static_cast<std::size_t>(turbine_count + target)]
                        - coordinates[
                            static_cast<std::size_t>(turbine_count + source)])
                        * flow_y;
                const Scalar dy =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)])
                        * cross_x
                    + (coordinates[
                        static_cast<std::size_t>(turbine_count + target)]
                        - coordinates[
                            static_cast<std::size_t>(turbine_count + source)])
                        * cross_y;
                const Scalar candidate = added_turbulence_candidate(
                    dx,
                    dy,
                    local_ti[static_cast<std::size_t>(source)],
                    inflow[static_cast<std::size_t>(source)],
                    data
                );
                if (settings.turbulence_mode == TurbulenceMode::smooth_local) {
                    added_ti = stable_smooth_max(added_ti, candidate, 700.0);
                } else if (
                    numeric_value(candidate) > numeric_value(added_ti)
                ) {
                    added_ti = candidate;
                }
            }
            local_ti[static_cast<std::size_t>(target)] = sqrt(
                ambient_ti * ambient_ti + added_ti * added_ti
            );
        } else {
            local_ti[static_cast<std::size_t>(target)] =
                scalar_constant<Scalar>(ambient_ti);
        }

        Scalar rotor_sum = scalar_constant<Scalar>(0.0);
        for (const auto& sample : samples) {
            const double sample_cross = sample[0];
            const double sample_vertical = sample[1];
            const double sample_height =
                std::max(1.0, hub_height_m + sample_vertical);
            Scalar effective = scalar_constant<Scalar>(
                reference_speed * std::pow(
                    sample_height / reference_height_m,
                    shear_exponent
                )
            );
            for (int upstream_position = 0;
                 upstream_position < position;
                 ++upstream_position) {
                const int source =
                    order[static_cast<std::size_t>(upstream_position)];
                const Scalar dx =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)])
                        * flow_x
                    + (coordinates[
                        static_cast<std::size_t>(turbine_count + target)]
                        - coordinates[
                            static_cast<std::size_t>(turbine_count + source)])
                        * flow_y;
                const Scalar dy =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)])
                        * cross_x
                    + (coordinates[
                        static_cast<std::size_t>(turbine_count + target)]
                        - coordinates[
                            static_cast<std::size_t>(turbine_count + source)])
                        * cross_y
                    + sample_cross;
                const Scalar deficit = wake_deficit_ratio(
                    dx,
                    dy,
                    scalar_constant<Scalar>(sample_vertical),
                    local_ti[static_cast<std::size_t>(source)],
                    inflow[static_cast<std::size_t>(source)],
                    settings.wec_factor,
                    data
                );
                effective -=
                    inflow[static_cast<std::size_t>(source)] * deficit;
            }
            if (numeric_value(effective) < 0.0) {
                effective = scalar_constant<Scalar>(0.0);
            }
            rotor_sum += effective;
        }
        inflow[static_cast<std::size_t>(target)] =
            rotor_sum / static_cast<double>(samples.size());
    }

    Scalar total_power = scalar_constant<Scalar>(0.0);
    for (const Scalar& speed : inflow) {
        total_power += turbine_power_mw(speed, data);
    }
    return total_power;
}

std::vector<double> flatten(const std::vector<Point>& layout) {
    std::vector<double> values(static_cast<std::size_t>(variables));
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        values[static_cast<std::size_t>(turbine)] =
            layout[static_cast<std::size_t>(turbine)].x_m;
        values[static_cast<std::size_t>(turbine_count + turbine)] =
            layout[static_cast<std::size_t>(turbine)].y_m;
    }
    return values;
}

std::vector<Point> unflatten(const double* values) {
    std::vector<Point> layout(static_cast<std::size_t>(turbine_count));
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        layout[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(turbine)],
            values[static_cast<std::size_t>(turbine_count + turbine)],
        };
    }
    return layout;
}

std::uint64_t hash_mix(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

struct ObjectiveContext {
    const Problem* problem = nullptr;
    fode::PersistentExecutor* executor = nullptr;
    EvaluationSettings settings;
    int objective_calls = 0;
    int gradient_calls = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
};

double objective_callback(
    const unsigned n,
    const double* x,
    double* gradient,
    void* opaque
) {
    if (n != static_cast<unsigned>(variables)) {
        return std::numeric_limits<double>::infinity();
    }
    auto& context = *static_cast<ObjectiveContext*>(opaque);
    context.settings.calculate_gradient = gradient != nullptr;
    const std::vector<Point> layout = unflatten(x);
    const Evaluation evaluation = context.problem->evaluate(
        layout, context.settings, *context.executor
    );
    ++context.objective_calls;
    if (gradient != nullptr) {
        ++context.gradient_calls;
        for (int index = 0; index < variables; ++index) {
            gradient[static_cast<std::size_t>(index)] =
                -evaluation.gradient_gwh_per_m[
                    static_cast<std::size_t>(index)
                ];
        }
    }
    context.observed_workers = std::max(
        context.observed_workers, evaluation.observed_workers
    );
    context.evaluator_seconds += evaluation.seconds;
    return -evaluation.aep_gwh;
}

struct ConstraintContext {
    int calls = 0;
};

void constraint_callback(
    const unsigned m,
    double* result,
    const unsigned n,
    const double* x,
    double* gradient,
    void* opaque
) {
    if (
        m != static_cast<unsigned>(total_constraints)
        || n != static_cast<unsigned>(variables)
    ) {
        return;
    }
    auto& context = *static_cast<ConstraintContext*>(opaque);
    ++context.calls;
    if (gradient != nullptr) {
        std::fill(
            gradient,
            gradient
                + static_cast<std::size_t>(m) * static_cast<std::size_t>(n),
            0.0
        );
    }
    int row = 0;
    const double spacing_scale =
        minimum_spacing_m * minimum_spacing_m;
    for (int left = 0; left < turbine_count; ++left) {
        for (int right = left + 1; right < turbine_count; ++right) {
            const double dx = x[static_cast<std::size_t>(left)]
                - x[static_cast<std::size_t>(right)];
            const double dy =
                x[static_cast<std::size_t>(turbine_count + left)]
                - x[static_cast<std::size_t>(turbine_count + right)];
            result[static_cast<std::size_t>(row)] =
                1.0 - (dx * dx + dy * dy) / spacing_scale;
            if (gradient != nullptr) {
                double* jacobian = gradient
                    + static_cast<std::size_t>(row)
                        * static_cast<std::size_t>(variables);
                jacobian[static_cast<std::size_t>(left)] =
                    -2.0 * dx / spacing_scale;
                jacobian[static_cast<std::size_t>(right)] =
                    2.0 * dx / spacing_scale;
                jacobian[
                    static_cast<std::size_t>(turbine_count + left)
                ] = -2.0 * dy / spacing_scale;
                jacobian[
                    static_cast<std::size_t>(turbine_count + right)
                ] = 2.0 * dy / spacing_scale;
            }
            ++row;
        }
    }
    const double boundary_scale =
        hub_centre_radius_m * hub_centre_radius_m;
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        const double px = x[static_cast<std::size_t>(turbine)];
        const double py =
            x[static_cast<std::size_t>(turbine_count + turbine)];
        result[static_cast<std::size_t>(row)] =
            (px * px + py * py) / boundary_scale - 1.0;
        if (gradient != nullptr) {
            double* jacobian = gradient
                + static_cast<std::size_t>(row)
                    * static_cast<std::size_t>(variables);
            jacobian[static_cast<std::size_t>(turbine)] =
                2.0 * px / boundary_scale;
            jacobian[
                static_cast<std::size_t>(turbine_count + turbine)
            ] = 2.0 * py / boundary_scale;
        }
        ++row;
    }
}

}  // namespace

struct Problem::Data {
    std::array<double, wind_state_count> wind_direction_degrees{};
    std::array<double, wind_state_count> wind_speed_mps{};
    std::array<double, wind_state_count> wind_probability{};
    std::vector<double> speed_mps;
    std::vector<double> cp;
    std::vector<double> ct;
    std::vector<Point> baseline;
};

namespace {

template<typename Value>
Value read_binary(std::ifstream& stream) {
    static_assert(std::is_trivially_copyable_v<Value>);
    std::array<char, sizeof(Value)> bytes{};
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("truncated T16 data fixture");
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(bytes.begin(), bytes.end());
    }
    Value value{};
    std::memcpy(&value, bytes.data(), sizeof(Value));
    return value;
}

}  // namespace

Problem::Problem(const std::string& data_path)
    : semantic_id_("t16_nantucket38_author_lineage_reconstructed_v1") {
    std::ifstream stream(data_path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open T16 public data");
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != data_magic) {
        throw std::runtime_error("invalid T16 public data magic");
    }
    const std::uint32_t count = read_binary<std::uint32_t>(stream);
    if (count < 100U || count > 1000U) {
        throw std::runtime_error("invalid T16 CP/CT table length");
    }
    auto data = std::make_shared<Data>();
    for (int state = 0; state < wind_state_count; ++state) {
        data->wind_direction_degrees[static_cast<std::size_t>(state)] =
            read_binary<double>(stream);
        data->wind_speed_mps[static_cast<std::size_t>(state)] =
            read_binary<double>(stream);
        data->wind_probability[static_cast<std::size_t>(state)] =
            read_binary<double>(stream);
    }
    data->speed_mps.reserve(count);
    data->cp.reserve(count);
    data->ct.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        data->speed_mps.push_back(read_binary<double>(stream));
        data->cp.push_back(read_binary<double>(stream));
        data->ct.push_back(read_binary<double>(stream));
    }
    data->baseline.reserve(turbine_count);
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        data->baseline.push_back({
            read_binary<double>(stream),
            read_binary<double>(stream),
        });
    }
    char trailing = '\0';
    if (stream.read(&trailing, 1)) {
        throw std::runtime_error("unexpected trailing T16 data");
    }
    if (!std::is_sorted(data->speed_mps.begin(), data->speed_mps.end())) {
        throw std::runtime_error("T16 CP/CT speeds are not sorted");
    }
    const double probability_mass = std::accumulate(
        data->wind_probability.begin(),
        data->wind_probability.end(),
        0.0
    );
    if (std::abs(probability_mass - 0.962) > 1.0e-12) {
        throw std::runtime_error("unexpected T16 wind probability mass");
    }
    data_ = std::move(data);
}

const std::string& Problem::semantic_id() const noexcept {
    return semantic_id_;
}

const std::vector<Point>& Problem::baseline_layout() const noexcept {
    return data_->baseline;
}

std::vector<Point> Problem::reconstructed_start(
    const int start_index,
    const std::uint64_t seed
) const {
    if (start_index <= 0) return data_->baseline;
    std::mt19937_64 generator(
        seed ^ (
            0x9e3779b97f4a7c15ULL
            * static_cast<std::uint64_t>(start_index)
        )
    );
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    std::vector<Point> layout;
    layout.reserve(turbine_count);
    int attempts = 0;
    while (static_cast<int>(layout.size()) < turbine_count) {
        if (++attempts > 2000000) {
            throw std::runtime_error("failed to generate feasible T16 start");
        }
        const double radius =
            hub_centre_radius_m * std::sqrt(uniform(generator));
        const double angle = 2.0 * pi * uniform(generator);
        const Point candidate = {
            radius * std::cos(angle),
            radius * std::sin(angle),
        };
        bool feasible = true;
        for (const Point& existing : layout) {
            if (
                std::hypot(
                    candidate.x_m - existing.x_m,
                    candidate.y_m - existing.y_m
                ) < minimum_spacing_m
            ) {
                feasible = false;
                break;
            }
        }
        if (feasible) layout.push_back(candidate);
    }
    return layout;
}

double Problem::maximum_constraint_violation(
    const std::vector<Point>& layout
) const {
    if (layout.size() != turbine_count) {
        return minimum_spacing_m
            * std::abs(
                static_cast<double>(layout.size()) - turbine_count
            );
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        violation = std::max(
            violation,
            std::max(
                0.0,
                std::hypot(point.x_m, point.y_m) - hub_centre_radius_m
            )
        );
    }
    for (int left = 0; left < turbine_count; ++left) {
        for (int right = left + 1; right < turbine_count; ++right) {
            violation = std::max(
                violation,
                std::max(
                    0.0,
                    minimum_spacing_m - std::hypot(
                        layout[static_cast<std::size_t>(left)].x_m
                            - layout[static_cast<std::size_t>(right)].x_m,
                        layout[static_cast<std::size_t>(left)].y_m
                            - layout[static_cast<std::size_t>(right)].y_m
                    )
                )
            );
        }
    }
    return violation;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const EvaluationSettings& settings,
    fode::PersistentExecutor& executor
) const {
    Evaluation evaluation;
    evaluation.requested_workers = executor.thread_count();
    evaluation.maximum_constraint_violation_m =
        maximum_constraint_violation(layout);
    if (layout.size() != turbine_count) return evaluation;
    const auto started = std::chrono::steady_clock::now();
    executor.reset_work_receipt();
    const std::vector<double> numeric_coordinates = flatten(layout);

    std::array<double, wind_state_count> powers{};
    std::array<std::array<double, variables>, wind_state_count> gradients{};
    executor.parallel_for(0, wind_state_count, [&](const int state) {
        if (settings.calculate_gradient) {
            std::vector<Dual> coordinates;
            coordinates.reserve(variables);
            for (int index = 0; index < variables; ++index) {
                coordinates.push_back(Dual::independent(
                    numeric_coordinates[static_cast<std::size_t>(index)],
                    index
                ));
            }
            const Dual power = direction_power_mw(
                coordinates, layout, state, settings, *data_
            );
            powers[static_cast<std::size_t>(state)] = power.value;
            auto& direction_gradient =
                gradients[static_cast<std::size_t>(state)];
            for (int index = 0; index < variables; ++index) {
                direction_gradient[static_cast<std::size_t>(index)] =
                    power.derivative[static_cast<std::size_t>(index)];
            }
        } else {
            powers[static_cast<std::size_t>(state)] = direction_power_mw(
                numeric_coordinates, layout, state, settings, *data_
            );
        }
    });
    const fode::ExecutorWorkReceipt receipt = executor.work_receipt();
    evaluation.observed_workers = receipt.peak_region_participants;
    evaluation.directional_power_mw = powers;
    evaluation.gradient_gwh_per_m.assign(
        settings.calculate_gradient ? variables : 0,
        0.0
    );
    for (int state = 0; state < wind_state_count; ++state) {
        const double weight =
            data_->wind_probability[static_cast<std::size_t>(state)]
            * hours_per_year / 1000.0;
        evaluation.aep_gwh +=
            weight * powers[static_cast<std::size_t>(state)];
        if (settings.calculate_gradient) {
            for (int index = 0; index < variables; ++index) {
                evaluation.gradient_gwh_per_m[
                    static_cast<std::size_t>(index)
                ] += weight * gradients[static_cast<std::size_t>(state)][
                    static_cast<std::size_t>(index)
                ];
            }
        }
    }
    evaluation.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    return evaluation;
}

double smooth_max(
    const double left,
    const double right,
    const double smoothing
) {
    return stable_smooth_max(left, right, smoothing);
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1) {
        throw std::invalid_argument("T16 workers must be positive");
    }
    if (
        config.start_index < 0 || config.start_index >= 200
        || config.maximum_evaluations_per_stage < 1
    ) {
        throw std::invalid_argument("invalid T16 run configuration");
    }
    const auto run_started = std::chrono::steady_clock::now();
    fode::PersistentExecutor executor(config.workers);
    std::vector<Point> layout = problem.reconstructed_start(
        config.start_index, config.seed
    );
    RunResult result;
    result.problem_semantic_id = problem.semantic_id();
    result.method_semantic_id =
        "t16_wec_slsqp_autodiff_reconstruction_v1";
    result.start_index = config.start_index;
    result.seed = config.seed;
    result.requested_workers = config.workers;

    EvaluationSettings initial_settings;
    initial_settings.wec_factor = 1.0;
    initial_settings.turbulence_mode = TurbulenceMode::ambient_only;
    initial_settings.rotor_sample_points = 1;
    result.initial_optimization_evaluation = problem.evaluate(
        layout, initial_settings, executor
    );

    std::vector<std::pair<double, TurbulenceMode>> lifecycle;
    if (config.run_full_wec_lifecycle) {
        for (int step = 0; step <= 8; ++step) {
            lifecycle.emplace_back(
                3.0 - 0.25 * static_cast<double>(step),
                TurbulenceMode::ambient_only
            );
        }
    } else {
        lifecycle.emplace_back(3.0, TurbulenceMode::ambient_only);
        lifecycle.emplace_back(1.0, TurbulenceMode::ambient_only);
    }
    lifecycle.emplace_back(1.0, TurbulenceMode::smooth_local);

    for (const auto& [wec_factor, turbulence_mode] : lifecycle) {
        const auto stage_started = std::chrono::steady_clock::now();
        StageReceipt stage;
        stage.wec_factor = wec_factor;
        stage.turbulence_mode = turbulence_mode;
        EvaluationSettings stage_settings;
        stage_settings.wec_factor = wec_factor;
        stage_settings.turbulence_mode = turbulence_mode;
        stage_settings.rotor_sample_points = 1;
        stage.start_aep_gwh = problem.evaluate(
            layout, stage_settings, executor
        ).aep_gwh;

        std::vector<double> x = flatten(layout);
        const std::vector<double> lower(
            static_cast<std::size_t>(variables), -hub_centre_radius_m
        );
        const std::vector<double> upper(
            static_cast<std::size_t>(variables), hub_centre_radius_m
        );
        const std::vector<double> constraint_tolerances(
            static_cast<std::size_t>(total_constraints), 1.0e-7
        );
        ObjectiveContext objective_context;
        objective_context.problem = &problem;
        objective_context.executor = &executor;
        objective_context.settings = stage_settings;
        ConstraintContext constraint_context;
        nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, variables);
        if (optimizer == nullptr) {
            throw std::runtime_error("failed to create T16 SLSQP");
        }
        nlopt_result status = NLOPT_FAILURE;
        double minimum = 0.0;
        try {
            if (
                nlopt_set_lower_bounds(optimizer, lower.data()) < 0
                || nlopt_set_upper_bounds(optimizer, upper.data()) < 0
                || nlopt_set_min_objective(
                    optimizer, objective_callback, &objective_context
                ) < 0
                || nlopt_add_inequality_mconstraint(
                    optimizer,
                    total_constraints,
                    constraint_callback,
                    &constraint_context,
                    constraint_tolerances.data()
                ) < 0
                || nlopt_set_xtol_rel(
                    optimizer, config.relative_x_tolerance
                ) < 0
                || nlopt_set_maxeval(
                    optimizer, config.maximum_evaluations_per_stage
                ) < 0
            ) {
                throw std::runtime_error("failed to configure T16 SLSQP");
            }
            if (config.maximum_stage_seconds > 0.0) {
                if (
                    nlopt_set_maxtime(
                        optimizer, config.maximum_stage_seconds
                    ) < 0
                ) {
                    throw std::runtime_error("failed to set T16 time limit");
                }
            }
            status = nlopt_optimize(optimizer, x.data(), &minimum);
        } catch (...) {
            nlopt_destroy(optimizer);
            throw;
        }
        nlopt_destroy(optimizer);
        const std::vector<Point> candidate = unflatten(x.data());
        if (problem.maximum_constraint_violation(candidate) <= 1.0e-3) {
            layout = candidate;
        }
        const Evaluation stage_end = problem.evaluate(
            layout, stage_settings, executor
        );
        stage.end_aep_gwh = stage_end.aep_gwh;
        stage.objective_calls = objective_context.objective_calls;
        stage.gradient_calls = objective_context.gradient_calls;
        stage.constraint_calls = constraint_context.calls;
        stage.optimizer_status = static_cast<int>(status);
        stage.optimizer_status_name = nlopt_status_name(status);
        stage.seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - stage_started
        ).count();
        result.evaluator_seconds += objective_context.evaluator_seconds;
        result.observed_workers = std::max(
            result.observed_workers,
            std::max(
                objective_context.observed_workers,
                stage_end.observed_workers
            )
        );
        result.stages.push_back(std::move(stage));
    }

    EvaluationSettings final_optimization_settings;
    final_optimization_settings.wec_factor = 1.0;
    final_optimization_settings.turbulence_mode =
        TurbulenceMode::smooth_local;
    final_optimization_settings.rotor_sample_points = 1;
    result.final_optimization_evaluation = problem.evaluate(
        layout, final_optimization_settings, executor
    );
    EvaluationSettings assessment_settings;
    assessment_settings.wec_factor = 1.0;
    assessment_settings.turbulence_mode = TurbulenceMode::hard_local;
    assessment_settings.rotor_sample_points = 100;
    result.final_paper_assessment = problem.evaluate(
        layout, assessment_settings, executor
    );
    result.final_layout = layout;
    result.end_to_end_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - run_started
    ).count();
    result.optimizer_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : layout) {
        hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    hash = hash_mix(
        hash,
        std::bit_cast<std::uint64_t>(
            result.final_paper_assessment.aep_gwh
        )
    );
    result.scientific_hash = hash;
    return result;
}

}  // namespace core99::t16
