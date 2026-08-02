/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 pure-C++ WEC evaluator, exact-gradient open SQP,
and augmented-Lagrangian PSO
Paper/DOI: Wake Expansion Continuation: Multi-Modality Reduction in the Wind
Farm Layout Optimization Problem; 10.1002/we.2692
Public source: byuflowlab/thomas2021-wec at commit 8ff27d66079591f25619a;
model oracles: PlantEnergy 356fafd95d0ff6396f531f8cc05e4526041df12c and
jensen3d 08c105334991617eea07f74a8118c9ff6e23c31c.
Missing/conflicts and resolution: proprietary SNOPT, Tapenade/pyOptSparse
state, exact streams, Case-1 10-versus-8 m/s and ALPSO 30-versus-25 facts are
fully declared in include/core99/thomas_t84.hpp; this is the independent
open-replacement reconstruction.
Semantic IDs: t84_wec_four_case_author_data_v1 and four t84_* method IDs.
Production backend: pure C++ CPU-HPC with parallel directions or particles,
fixed slots and ordered state commits.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: source-backed flexible academic reproduction, not author
SNOPT/Tapenade/pyOptSparse/environment/random-state numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/thomas_t84.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace core99::t84 {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double diameter_m = 80.0;
constexpr double rotor_radius_m = 40.0;
constexpr double hub_height_m = 70.0;
constexpr double reference_height_m = 80.0;
constexpr double air_density_kg_m3 = 1.225;
constexpr double generator_efficiency = 0.944;
constexpr double ambient_ti = 0.108;
constexpr double shear_exponent = 0.31;
constexpr double minimum_spacing_m = 2.0 * diameter_m;
constexpr double rated_power_mw = 2.0;
constexpr double hours_per_year = 8760.0;
constexpr std::array<char, 8> data_magic = {
    'T', '8', '4', 'D', 'A', 'T', 'A', '1',
};

using std::acos;
using std::atan;
using std::cos;
using std::exp;
using std::log;
using std::pow;
using std::sqrt;

struct Dual {
    double value = 0.0;
    std::array<double, maximum_variables> derivative{};

    Dual() = default;
    Dual(const double scalar) : value(scalar) {}

    static Dual independent(const double scalar, const int index) {
        Dual result(scalar);
        result.derivative[static_cast<std::size_t>(index)] = 1.0;
        return result;
    }
};

Dual operator+(const Dual& left, const Dual& right) {
    Dual result(left.value + right.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)]
            + right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator-(const Dual& left, const Dual& right) {
    Dual result(left.value - right.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)]
            - right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator-(const Dual& input) {
    Dual result(-input.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            -input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator*(const Dual& left, const Dual& right) {
    Dual result(left.value * right.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            left.derivative[static_cast<std::size_t>(index)] * right.value
            + left.value * right.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual operator/(const Dual& left, const Dual& right) {
    Dual result(left.value / right.value);
    const double denominator = right.value * right.value;
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] = (
            left.derivative[static_cast<std::size_t>(index)] * right.value
            - left.value * right.derivative[static_cast<std::size_t>(index)]
        ) / denominator;
    }
    return result;
}

Dual& operator+=(Dual& left, const Dual& right) {
    left = left + right;
    return left;
}

Dual& operator-=(Dual& left, const Dual& right) {
    left = left - right;
    return left;
}

Dual sqrt(const Dual& input) {
    const double root = std::sqrt(std::max(0.0, input.value));
    Dual result(root);
    if (root <= 1.0e-30) return result;
    const double scale = 0.5 / root;
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            scale * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual exp(const Dual& input) {
    const double exponential = std::exp(input.value);
    Dual result(exponential);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            exponential * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual log(const Dual& input) {
    Dual result(std::log(input.value));
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            input.derivative[static_cast<std::size_t>(index)] / input.value;
    }
    return result;
}

Dual acos(const Dual& input) {
    const double value = std::clamp(input.value, -1.0, 1.0);
    Dual result(std::acos(value));
    const double denominator = std::sqrt(std::max(1.0e-30, 1.0 - value * value));
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            -input.derivative[static_cast<std::size_t>(index)] / denominator;
    }
    return result;
}

Dual atan(const Dual& input) {
    Dual result(std::atan(input.value));
    const double scale = 1.0 / (1.0 + input.value * input.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            scale * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual cos(const Dual& input) {
    Dual result(std::cos(input.value));
    const double scale = -std::sin(input.value);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            scale * input.derivative[static_cast<std::size_t>(index)];
    }
    return result;
}

Dual pow(const Dual& input, const double exponent) {
    const double powered = std::pow(input.value, exponent);
    Dual result(powered);
    const double scale = exponent * std::pow(input.value, exponent - 1.0);
    for (int index = 0; index < maximum_variables; ++index) {
        result.derivative[static_cast<std::size_t>(index)] =
            scale * input.derivative[static_cast<std::size_t>(index)];
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
    const double slope = (ys[right] - ys[left]) / (xs[right] - xs[left]);
    return scalar_constant<Scalar>(ys[left]) + (x - xs[left]) * slope;
}

template<typename Scalar>
Scalar stable_smooth_max(
    const Scalar& left,
    const Scalar& right,
    const double smoothing
) {
    const Scalar& maximum = numeric_value(left) >= numeric_value(right)
        ? left : right;
    const Scalar& minimum = numeric_value(left) >= numeric_value(right)
        ? right : left;
    const Scalar scaled = smoothing * (minimum - maximum);
    if (numeric_value(scaled) < -40.0) return maximum;
    return maximum + log(scalar_constant<Scalar>(1.0) + exp(scaled))
        / smoothing;
}

template<typename Scalar>
Scalar circle_overlap_area(
    const Scalar& signed_distance,
    const Scalar& wake_radius
) {
    const Scalar distance = sqrt(
        signed_distance * signed_distance + scalar_constant<Scalar>(1.0e-20)
    );
    const double d = numeric_value(distance);
    const double r = std::max(1.0e-9, numeric_value(wake_radius));
    if (d >= r + rotor_radius_m) return scalar_constant<Scalar>(0.0);
    if (d <= std::abs(r - rotor_radius_m)) {
        const Scalar smaller = r <= rotor_radius_m
            ? wake_radius : scalar_constant<Scalar>(rotor_radius_m);
        return pi * smaller * smaller;
    }
    const Scalar wake_cosine = (
        distance * distance + wake_radius * wake_radius
        - rotor_radius_m * rotor_radius_m
    ) / (2.0 * distance * wake_radius);
    const Scalar rotor_cosine = (
        distance * distance + rotor_radius_m * rotor_radius_m
        - wake_radius * wake_radius
    ) / (2.0 * distance * rotor_radius_m);
    const Scalar product =
        (-distance + wake_radius + rotor_radius_m)
        * (distance + wake_radius - rotor_radius_m)
        * (distance - wake_radius + rotor_radius_m)
        * (distance + wake_radius + rotor_radius_m);
    return wake_radius * wake_radius * acos(wake_cosine)
        + rotor_radius_m * rotor_radius_m * acos(rotor_cosine)
        - 0.5 * sqrt(product);
}

std::uint64_t hash_mix(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

template<typename Value>
Value read_binary(std::ifstream& stream) {
    static_assert(std::is_trivially_copyable_v<Value>);
    std::array<char, sizeof(Value)> bytes{};
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("truncated T84 data fixture");
    if constexpr (std::endian::native == std::endian::big) {
        std::reverse(bytes.begin(), bytes.end());
    }
    Value value{};
    std::memcpy(&value, bytes.data(), sizeof(Value));
    return value;
}

std::vector<double> flatten(const std::vector<Point>& layout) {
    const int turbines = static_cast<int>(layout.size());
    std::vector<double> values(static_cast<std::size_t>(2 * turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        values[static_cast<std::size_t>(turbine)] =
            layout[static_cast<std::size_t>(turbine)].x_m;
        values[static_cast<std::size_t>(turbines + turbine)] =
            layout[static_cast<std::size_t>(turbine)].y_m;
    }
    return values;
}

std::vector<Point> unflatten(const std::vector<double>& values) {
    const int turbines = static_cast<int>(values.size() / 2U);
    std::vector<Point> layout(static_cast<std::size_t>(turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        layout[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(turbine)],
            values[static_cast<std::size_t>(turbines + turbine)],
        };
    }
    return layout;
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

}  // namespace

struct Problem::Data {
    enum class BoundaryKind : std::uint32_t { square = 0, circle = 1, polygon = 2 };

    int case_id = 0;
    int turbines = 0;
    std::vector<double> power_speed_mps;
    std::vector<double> power_mw;
    std::vector<double> ct_speed_mps;
    std::vector<double> ct;
    std::vector<double> wind_direction_degrees;
    std::vector<double> wind_speed_mps;
    std::vector<double> wind_probability;
    BoundaryKind boundary_kind = BoundaryKind::square;
    std::array<double, 4> square{};
    std::array<double, 3> circle{};
    std::vector<Point> polygon;
    std::vector<std::vector<Point>> layouts;
};

namespace {

template<typename Scalar>
Scalar thrust_coefficient(const Scalar& speed, const Problem::Data& data) {
    if (numeric_value(speed) < 4.0 || numeric_value(speed) > 25.0) {
        return scalar_constant<Scalar>(0.0);
    }
    return interpolate(speed, data.ct_speed_mps, data.ct);
}

template<typename Scalar>
Scalar turbine_power_mw(const Scalar& speed, const Problem::Data& data) {
    const double value = numeric_value(speed);
    if (value < 4.0 || value > 25.0) {
        return scalar_constant<Scalar>(0.0);
    }
    if (value >= 15.0) {
        return scalar_constant<Scalar>(rated_power_mw * generator_efficiency);
    }
    return interpolate(speed, data.power_speed_mps, data.power_mw)
        * generator_efficiency;
}

template<typename Scalar>
Scalar bastankhah_deficit(
    const Scalar& downstream_m,
    const Scalar& lateral_m,
    const Scalar& source_ti,
    const Scalar& source_speed,
    const double wec_factor,
    const Problem::Data& data
) {
    if (numeric_value(downstream_m) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    Scalar ct = thrust_coefficient(source_speed, data);
    if (numeric_value(ct) <= 0.0) return scalar_constant<Scalar>(0.0);
    if (numeric_value(ct) >= 0.999) ct = scalar_constant<Scalar>(0.999);
    const Scalar root = sqrt(scalar_constant<Scalar>(1.0) - ct);
    const Scalar k = 0.3837 * source_ti + 0.003678;
    const Scalar x0 = diameter_m * (
        scalar_constant<Scalar>(1.0) + root
    ) / (
        std::sqrt(2.0) * (2.32 * source_ti + 0.154 * (
            scalar_constant<Scalar>(1.0) - root
        ))
    );
    const Scalar a = k * k;
    const Scalar b = diameter_m * (2.0 * k) / std::sqrt(8.0);
    const Scalar c = diameter_m * diameter_m * (ct - 1.0) / 8.0;
    const Scalar offset = (-b + sqrt(b * b - 4.0 * a * c)) / (2.0 * a);
    const Scalar xd = x0 + offset;
    const Scalar sigma = numeric_value(downstream_m) < numeric_value(xd)
        ? k * offset + diameter_m / std::sqrt(8.0)
        : k * (downstream_m - x0) + diameter_m / std::sqrt(8.0);
    const Scalar radicand = scalar_constant<Scalar>(1.0)
        - ct * diameter_m * diameter_m / (8.0 * sigma * sigma);
    const Scalar centre = scalar_constant<Scalar>(1.0) - sqrt(radicand);
    const Scalar expanded_sigma = wec_factor * sigma;
    return centre * exp(
        -0.5 * lateral_m * lateral_m
        / (expanded_sigma * expanded_sigma)
    );
}

template<typename Scalar>
Scalar added_turbulence(
    const Scalar& downstream_m,
    const Scalar& lateral_m,
    const Scalar& source_ti,
    const Scalar& source_speed,
    const Problem::Data& data
) {
    if (numeric_value(downstream_m) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    Scalar ct = thrust_coefficient(source_speed, data);
    if (numeric_value(ct) <= 0.0) return scalar_constant<Scalar>(0.0);
    if (numeric_value(ct) >= 0.999) ct = scalar_constant<Scalar>(0.999);
    const Scalar root = sqrt(scalar_constant<Scalar>(1.0) - ct);
    const Scalar beta = 0.5 * (scalar_constant<Scalar>(1.0) + root) / root;
    const Scalar epsilon = 0.2 * sqrt(beta);
    const Scalar sigma = (0.3837 * source_ti + 0.003678) * downstream_m
        + diameter_m * epsilon;
    const Scalar overlap = circle_overlap_area(lateral_m, 2.0 * sigma)
        / (pi * rotor_radius_m * rotor_radius_m);
    Scalar induction;
    if (numeric_value(ct) > 0.96) {
        induction = 0.143 + sqrt(
            scalar_constant<Scalar>(0.0203) - 0.6427 * (0.889 - ct)
        );
    } else {
        induction = 0.5 * (
            scalar_constant<Scalar>(1.0)
            - sqrt(scalar_constant<Scalar>(1.0) - ct)
        );
    }
    return 0.73 * pow(induction, 0.8325) * pow(source_ti, 0.0325)
        * pow(downstream_m / diameter_m, -0.32) * overlap;
}

template<typename Scalar>
Scalar jensen_cosine_deficit(
    const Scalar& downstream_m,
    const Scalar& lateral_m,
    const double wec_factor
) {
    if (numeric_value(downstream_m) <= 0.0) {
        return scalar_constant<Scalar>(0.0);
    }
    constexpr double axial_induction = 1.0 / 3.0;
    constexpr double alpha = 0.1;
    const double beta = radians(20.0);
    const Scalar centre = 2.0 * axial_induction * pow(
        rotor_radius_m / (rotor_radius_m + alpha * downstream_m), 2.0
    );
    const Scalar magnitude = sqrt(
        lateral_m * lateral_m + scalar_constant<Scalar>(1.0e-20)
    );
    const Scalar theta = atan(
        magnitude / (
            downstream_m + wec_factor * rotor_radius_m / std::tan(beta)
        )
    );
    if (numeric_value(theta) >= beta) {
        return scalar_constant<Scalar>(0.0);
    }
    return centre * 0.5 * (
        scalar_constant<Scalar>(1.0) + cos((pi / beta) * theta)
    );
}

template<typename Scalar>
Scalar direction_power_mw(
    const std::vector<Scalar>& coordinates,
    const std::vector<Point>& numeric_layout,
    const int state,
    const EvaluationSettings& settings,
    const Problem::Data& data
) {
    const int turbines = data.turbines;
    const double from = radians(
        data.wind_direction_degrees[static_cast<std::size_t>(state)]
    );
    const double flow_x = -std::sin(from);
    const double flow_y = -std::cos(from);
    const double cross_x = -flow_y;
    const double cross_y = flow_x;
    // The author PlantEnergy lineage passes the tabulated speed directly to
    // Jensen3D, while the Bastankhah kernel applies the declared 80-to-70 m
    // power-law adjustment internally. Preserve that source-backed model
    // distinction instead of silently forcing a common preprocessing step.
    const double reference_speed =
        data.wind_speed_mps[static_cast<std::size_t>(state)]
        * (settings.wake_model == WakeModel::bastankhah
            ? std::pow(hub_height_m / reference_height_m, shear_exponent)
            : 1.0);

    if (settings.wake_model == WakeModel::jensen_cosine) {
        Scalar total = scalar_constant<Scalar>(0.0);
        for (int target = 0; target < turbines; ++target) {
            Scalar sum_squares = scalar_constant<Scalar>(0.0);
            for (int source = 0; source < turbines; ++source) {
                if (source == target) continue;
                const Scalar dx =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)]) * flow_x
                    + (coordinates[static_cast<std::size_t>(turbines + target)]
                        - coordinates[static_cast<std::size_t>(turbines + source)]) * flow_y;
                const Scalar dy =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)]) * cross_x
                    + (coordinates[static_cast<std::size_t>(turbines + target)]
                        - coordinates[static_cast<std::size_t>(turbines + source)]) * cross_y;
                const Scalar deficit = jensen_cosine_deficit(
                    dx, dy, settings.wec_factor
                );
                sum_squares += deficit * deficit;
            }
            Scalar effective = reference_speed * (
                scalar_constant<Scalar>(1.0) - sqrt(sum_squares)
            );
            if (numeric_value(effective) < 0.0) {
                effective = scalar_constant<Scalar>(0.0);
            }
            total += turbine_power_mw(effective, data);
        }
        return total;
    }

    std::vector<double> streamwise(static_cast<std::size_t>(turbines));
    std::vector<int> order(static_cast<std::size_t>(turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        streamwise[static_cast<std::size_t>(turbine)] =
            numeric_layout[static_cast<std::size_t>(turbine)].x_m * flow_x
            + numeric_layout[static_cast<std::size_t>(turbine)].y_m * flow_y;
        order[static_cast<std::size_t>(turbine)] = turbine;
    }
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        const double l = streamwise[static_cast<std::size_t>(left)];
        const double r = streamwise[static_cast<std::size_t>(right)];
        return l == r ? left < right : l < r;
    });
    std::vector<Scalar> inflow(static_cast<std::size_t>(turbines));
    std::vector<Scalar> local_ti(static_cast<std::size_t>(turbines));
    for (int position = 0; position < turbines; ++position) {
        const int target = order[static_cast<std::size_t>(position)];
        Scalar additional = scalar_constant<Scalar>(0.0);
        if (settings.turbulence_mode != TurbulenceMode::ambient_only) {
            for (int upstream = 0; upstream < position; ++upstream) {
                const int source = order[static_cast<std::size_t>(upstream)];
                const Scalar dx =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)]) * flow_x
                    + (coordinates[static_cast<std::size_t>(turbines + target)]
                        - coordinates[static_cast<std::size_t>(turbines + source)]) * flow_y;
                const Scalar dy =
                    (coordinates[static_cast<std::size_t>(target)]
                        - coordinates[static_cast<std::size_t>(source)]) * cross_x
                    + (coordinates[static_cast<std::size_t>(turbines + target)]
                        - coordinates[static_cast<std::size_t>(turbines + source)]) * cross_y;
                const Scalar candidate = added_turbulence(
                    dx, dy, local_ti[static_cast<std::size_t>(source)],
                    inflow[static_cast<std::size_t>(source)], data
                );
                if (settings.turbulence_mode == TurbulenceMode::smooth_local) {
                    additional = stable_smooth_max(additional, candidate, 700.0);
                } else if (numeric_value(candidate) > numeric_value(additional)) {
                    additional = candidate;
                }
            }
            local_ti[static_cast<std::size_t>(target)] = sqrt(
                ambient_ti * ambient_ti + additional * additional
            );
        } else {
            local_ti[static_cast<std::size_t>(target)] =
                scalar_constant<Scalar>(ambient_ti);
        }
        Scalar effective = scalar_constant<Scalar>(reference_speed);
        for (int upstream = 0; upstream < position; ++upstream) {
            const int source = order[static_cast<std::size_t>(upstream)];
            const Scalar dx =
                (coordinates[static_cast<std::size_t>(target)]
                    - coordinates[static_cast<std::size_t>(source)]) * flow_x
                + (coordinates[static_cast<std::size_t>(turbines + target)]
                    - coordinates[static_cast<std::size_t>(turbines + source)]) * flow_y;
            const Scalar dy =
                (coordinates[static_cast<std::size_t>(target)]
                    - coordinates[static_cast<std::size_t>(source)]) * cross_x
                + (coordinates[static_cast<std::size_t>(turbines + target)]
                    - coordinates[static_cast<std::size_t>(turbines + source)]) * cross_y;
            effective -= inflow[static_cast<std::size_t>(source)]
                * bastankhah_deficit(
                    dx, dy, local_ti[static_cast<std::size_t>(source)],
                    inflow[static_cast<std::size_t>(source)],
                    settings.wec_factor, data
                );
        }
        if (numeric_value(effective) < 0.0) {
            effective = scalar_constant<Scalar>(0.0);
        }
        inflow[static_cast<std::size_t>(target)] = effective;
    }
    Scalar total = scalar_constant<Scalar>(0.0);
    for (const Scalar& speed : inflow) total += turbine_power_mw(speed, data);
    return total;
}

Evaluation evaluate_serial(
    const Problem::Data& data,
    const std::vector<Point>& layout,
    const EvaluationSettings& settings
) {
    Evaluation evaluation;
    evaluation.directional_power_mw.resize(data.wind_direction_degrees.size());
    const std::vector<double> coordinates = flatten(layout);
    for (std::size_t state = 0; state < data.wind_direction_degrees.size(); ++state) {
        evaluation.directional_power_mw[state] = direction_power_mw(
            coordinates, layout, static_cast<int>(state), settings, data
        );
        const double hub_speed = data.wind_speed_mps[state]
            * (settings.wake_model == WakeModel::bastankhah
                ? std::pow(hub_height_m / reference_height_m, shear_exponent)
                : 1.0);
        const double weight = data.wind_probability[state] * hours_per_year / 1000.0;
        evaluation.aep_gwh += weight * evaluation.directional_power_mw[state];
        evaluation.gross_aep_gwh += weight * static_cast<double>(data.turbines)
            * turbine_power_mw(hub_speed, data);
    }
    if (evaluation.gross_aep_gwh > 0.0) {
        evaluation.wake_loss_percent = 100.0 * (
            1.0 - evaluation.aep_gwh / evaluation.gross_aep_gwh
        );
    }
    return evaluation;
}

struct ObjectiveContext {
    const Problem* problem = nullptr;
    fode::PersistentExecutor* executor = nullptr;
    EvaluationSettings settings;
    int variables = 0;
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
    auto& context = *static_cast<ObjectiveContext*>(opaque);
    if (n != static_cast<unsigned>(context.variables)) {
        return std::numeric_limits<double>::infinity();
    }
    std::vector<double> values(x, x + n);
    context.settings.calculate_gradient = gradient != nullptr;
    const Evaluation evaluation = context.problem->evaluate(
        unflatten(values), context.settings, *context.executor
    );
    ++context.objective_calls;
    if (gradient != nullptr) {
        ++context.gradient_calls;
        for (int index = 0; index < context.variables; ++index) {
            gradient[static_cast<std::size_t>(index)] =
                -evaluation.gradient_gwh_per_m[static_cast<std::size_t>(index)];
        }
    }
    context.observed_workers = std::max(
        context.observed_workers, evaluation.observed_workers
    );
    context.evaluator_seconds += evaluation.seconds;
    return -evaluation.aep_gwh;
}

struct ConstraintContext {
    const Problem* problem = nullptr;
    int variables = 0;
};

void constraint_callback(
    const unsigned m,
    double* result,
    const unsigned n,
    const double* x,
    double* gradient,
    void* opaque
) {
    auto& context = *static_cast<ConstraintContext*>(opaque);
    if (
        m != static_cast<unsigned>(context.problem->constraint_count())
        || n != static_cast<unsigned>(context.variables)
    ) return;
    std::vector<double> coordinates(x, x + n);
    std::vector<double> values;
    std::vector<double> jacobian;
    context.problem->normalized_constraints(
        coordinates, values, gradient == nullptr ? nullptr : &jacobian
    );
    std::copy(values.begin(), values.end(), result);
    if (gradient != nullptr) {
        std::copy(jacobian.begin(), jacobian.end(), gradient);
    }
}

double stage_tolerance(
    const int case_id,
    const bool final_ti,
    const bool use_wec,
    const WakeModel model
) {
    if (model == WakeModel::jensen_cosine) return 1.0e-3;
    if (final_ti) return case_id == 4 && !use_wec ? 1.0e-4 : 1.0e-3;
    if (use_wec) return case_id == 1 ? 1.0e-2 : 9.0e-3;
    if (case_id == 1) return 1.0e-2;
    if (case_id == 4) return 1.0e-3;
    return 9.0e-3;
}

std::vector<std::pair<double, TurbulenceMode>> lifecycle(
    const WakeModel model,
    const bool use_wec
) {
    std::vector<std::pair<double, TurbulenceMode>> stages;
    if (use_wec) {
        for (const double factor : {3.0, 2.6, 2.2, 1.8, 1.4, 1.0}) {
            stages.emplace_back(factor, TurbulenceMode::ambient_only);
        }
        if (model == WakeModel::bastankhah) {
            stages.emplace_back(1.0, TurbulenceMode::smooth_local);
        }
    } else {
        stages.emplace_back(1.0, TurbulenceMode::ambient_only);
        if (model == WakeModel::bastankhah) {
            stages.emplace_back(1.0, TurbulenceMode::smooth_local);
        }
    }
    return stages;
}

struct AlpsoSchedule {
    int inner = 0;
    int outer = 0;
    int paper_calls = 0;
};

AlpsoSchedule alpso_schedule(const int case_id) {
    if (case_id == 1) return {5, 134, 20130};
    if (case_id == 2) return {25, 28, 21030};
    if (case_id == 3) return {15, 45, 20280};
    return {10, 68, 20430};
}

double augmented_score(
    const double aep,
    const double gross,
    const std::vector<double>& constraints,
    const std::vector<double>& multipliers,
    const double penalty
) {
    double score = -aep / std::max(1.0e-12, gross);
    for (std::size_t index = 0; index < constraints.size(); ++index) {
        const double positive = std::max(0.0, constraints[index]);
        score += multipliers[index] * positive
            + 0.5 * penalty * positive * positive;
    }
    return score;
}

}  // namespace

const char* wake_model_name(const WakeModel model) noexcept {
    return model == WakeModel::bastankhah ? "bastankhah" : "jensen_cosine";
}

const char* turbulence_name(const TurbulenceMode mode) noexcept {
    if (mode == TurbulenceMode::ambient_only) return "ambient";
    if (mode == TurbulenceMode::smooth_local) return "smooth";
    return "hard";
}

const char* optimizer_name(const OptimizerFamily optimizer) noexcept {
    return optimizer == OptimizerFamily::slsqp_open_snopt_replacement
        ? "slsqp_open_snopt_replacement" : "augmented_lagrangian_pso";
}

Problem::Problem(const std::string& data_path, const int requested_case)
    : semantic_id_("t84_wec_four_case_author_data_v1") {
    if (requested_case < 1 || requested_case > 4) {
        throw std::invalid_argument("T84 case must be in [1,4]");
    }
    std::ifstream stream(data_path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open T84 public data");
    std::array<char, 8> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (magic != data_magic) throw std::runtime_error("invalid T84 data magic");
    const std::uint32_t power_count = read_binary<std::uint32_t>(stream);
    const std::uint32_t thrust_count = read_binary<std::uint32_t>(stream);
    auto common = std::make_shared<Data>();
    for (std::uint32_t index = 0; index < power_count; ++index) {
        common->power_speed_mps.push_back(read_binary<double>(stream));
        common->power_mw.push_back(read_binary<double>(stream));
    }
    for (std::uint32_t index = 0; index < thrust_count; ++index) {
        common->ct_speed_mps.push_back(read_binary<double>(stream));
        common->ct.push_back(read_binary<double>(stream));
    }
    const std::uint32_t case_count = read_binary<std::uint32_t>(stream);
    if (case_count != 4U) throw std::runtime_error("T84 fixture must have four cases");
    for (std::uint32_t case_number = 0; case_number < case_count; ++case_number) {
        auto candidate = std::make_shared<Data>(*common);
        candidate->case_id = static_cast<int>(read_binary<std::uint32_t>(stream));
        candidate->turbines = static_cast<int>(read_binary<std::uint32_t>(stream));
        const int wind_count = static_cast<int>(read_binary<std::uint32_t>(stream));
        candidate->boundary_kind = static_cast<Data::BoundaryKind>(
            read_binary<std::uint32_t>(stream)
        );
        const int layout_count = static_cast<int>(read_binary<std::uint32_t>(stream));
        for (int state = 0; state < wind_count; ++state) {
            candidate->wind_direction_degrees.push_back(read_binary<double>(stream));
            candidate->wind_speed_mps.push_back(read_binary<double>(stream));
            candidate->wind_probability.push_back(read_binary<double>(stream));
        }
        if (candidate->boundary_kind == Data::BoundaryKind::square) {
            for (double& value : candidate->square) value = read_binary<double>(stream);
        } else if (candidate->boundary_kind == Data::BoundaryKind::circle) {
            for (double& value : candidate->circle) value = read_binary<double>(stream);
        } else if (candidate->boundary_kind == Data::BoundaryKind::polygon) {
            const int vertices = static_cast<int>(read_binary<std::uint32_t>(stream));
            for (int vertex = 0; vertex < vertices; ++vertex) {
                candidate->polygon.push_back({
                    read_binary<double>(stream), read_binary<double>(stream),
                });
            }
        } else {
            throw std::runtime_error("unknown T84 boundary kind");
        }
        candidate->layouts.resize(static_cast<std::size_t>(layout_count));
        for (auto& layout : candidate->layouts) {
            layout.reserve(static_cast<std::size_t>(candidate->turbines));
            for (int turbine = 0; turbine < candidate->turbines; ++turbine) {
                layout.push_back({
                    read_binary<double>(stream), read_binary<double>(stream),
                });
            }
        }
        if (candidate->case_id == requested_case) data_ = std::move(candidate);
    }
    char trailing = '\0';
    if (stream.read(&trailing, 1)) {
        throw std::runtime_error("unexpected trailing T84 fixture data");
    }
    if (!data_ || data_->layouts.size() != 200U) {
        throw std::runtime_error("T84 requested case missing or incomplete");
    }
    if (!std::is_sorted(data_->power_speed_mps.begin(), data_->power_speed_mps.end())
        || !std::is_sorted(data_->ct_speed_mps.begin(), data_->ct_speed_mps.end())) {
        throw std::runtime_error("T84 turbine curves are not sorted");
    }
}

const std::string& Problem::semantic_id() const noexcept { return semantic_id_; }
int Problem::case_id() const noexcept { return data_->case_id; }
int Problem::turbine_count() const noexcept { return data_->turbines; }
int Problem::wind_state_count() const noexcept {
    return static_cast<int>(data_->wind_direction_degrees.size());
}

const std::vector<Point>& Problem::start(const int start_index) const {
    if (start_index < 0 || start_index >= static_cast<int>(data_->layouts.size())) {
        throw std::out_of_range("T84 start index must be in [0,199]");
    }
    return data_->layouts[static_cast<std::size_t>(start_index)];
}

std::vector<double> Problem::lower_bounds() const {
    const int n = data_->turbines;
    double x_min = 0.0;
    double y_min = 0.0;
    if (data_->boundary_kind == Data::BoundaryKind::square) {
        x_min = data_->square[0] + rotor_radius_m;
        y_min = data_->square[2] + rotor_radius_m;
    } else if (data_->boundary_kind == Data::BoundaryKind::circle) {
        x_min = data_->circle[0] - data_->circle[2];
        y_min = data_->circle[1] - data_->circle[2];
    } else {
        x_min = std::min_element(data_->polygon.begin(), data_->polygon.end(),
            [](const Point& l, const Point& r) { return l.x_m < r.x_m; })->x_m;
        y_min = std::min_element(data_->polygon.begin(), data_->polygon.end(),
            [](const Point& l, const Point& r) { return l.y_m < r.y_m; })->y_m;
    }
    std::vector<double> bounds(static_cast<std::size_t>(2 * n));
    std::fill(bounds.begin(), bounds.begin() + n, x_min);
    std::fill(bounds.begin() + n, bounds.end(), y_min);
    return bounds;
}

std::vector<double> Problem::upper_bounds() const {
    const int n = data_->turbines;
    double x_max = 0.0;
    double y_max = 0.0;
    if (data_->boundary_kind == Data::BoundaryKind::square) {
        x_max = data_->square[1] - rotor_radius_m;
        y_max = data_->square[3] - rotor_radius_m;
    } else if (data_->boundary_kind == Data::BoundaryKind::circle) {
        x_max = data_->circle[0] + data_->circle[2];
        y_max = data_->circle[1] + data_->circle[2];
    } else {
        x_max = std::max_element(data_->polygon.begin(), data_->polygon.end(),
            [](const Point& l, const Point& r) { return l.x_m < r.x_m; })->x_m;
        y_max = std::max_element(data_->polygon.begin(), data_->polygon.end(),
            [](const Point& l, const Point& r) { return l.y_m < r.y_m; })->y_m;
    }
    std::vector<double> bounds(static_cast<std::size_t>(2 * n));
    std::fill(bounds.begin(), bounds.begin() + n, x_max);
    std::fill(bounds.begin() + n, bounds.end(), y_max);
    return bounds;
}

int Problem::constraint_count() const noexcept {
    const int pairs = data_->turbines * (data_->turbines - 1) / 2;
    if (data_->boundary_kind == Data::BoundaryKind::square) {
        return pairs + 4 * data_->turbines;
    }
    if (data_->boundary_kind == Data::BoundaryKind::circle) {
        return pairs + data_->turbines;
    }
    return pairs + data_->turbines * static_cast<int>(data_->polygon.size());
}

void Problem::normalized_constraints(
    const std::vector<double>& coordinates,
    std::vector<double>& values,
    std::vector<double>* jacobian
) const {
    const int n = data_->turbines;
    const int variables = 2 * n;
    if (coordinates.size() != static_cast<std::size_t>(variables)) {
        throw std::invalid_argument("T84 coordinate dimension mismatch");
    }
    values.clear();
    values.reserve(static_cast<std::size_t>(constraint_count()));
    if (jacobian != nullptr) {
        jacobian->assign(
            static_cast<std::size_t>(constraint_count() * variables), 0.0
        );
    }
    int row = 0;
    const double spacing_squared = minimum_spacing_m * minimum_spacing_m;
    auto derivative = [&](const int column, const double value) {
        if (jacobian != nullptr) {
            (*jacobian)[static_cast<std::size_t>(row * variables + column)] = value;
        }
    };
    for (int left = 0; left < n; ++left) {
        for (int right = left + 1; right < n; ++right) {
            const double dx = coordinates[static_cast<std::size_t>(left)]
                - coordinates[static_cast<std::size_t>(right)];
            const double dy = coordinates[static_cast<std::size_t>(n + left)]
                - coordinates[static_cast<std::size_t>(n + right)];
            values.push_back(1.0 - (dx * dx + dy * dy) / spacing_squared);
            derivative(left, -2.0 * dx / spacing_squared);
            derivative(right, 2.0 * dx / spacing_squared);
            derivative(n + left, -2.0 * dy / spacing_squared);
            derivative(n + right, 2.0 * dy / spacing_squared);
            ++row;
        }
    }
    if (data_->boundary_kind == Data::BoundaryKind::square) {
        const double x_min = data_->square[0] + rotor_radius_m;
        const double x_max = data_->square[1] - rotor_radius_m;
        const double y_min = data_->square[2] + rotor_radius_m;
        const double y_max = data_->square[3] - rotor_radius_m;
        for (int turbine = 0; turbine < n; ++turbine) {
            const double x = coordinates[static_cast<std::size_t>(turbine)];
            const double y = coordinates[static_cast<std::size_t>(n + turbine)];
            values.push_back((x_min - x) / diameter_m);
            derivative(turbine, -1.0 / diameter_m); ++row;
            values.push_back((x - x_max) / diameter_m);
            derivative(turbine, 1.0 / diameter_m); ++row;
            values.push_back((y_min - y) / diameter_m);
            derivative(n + turbine, -1.0 / diameter_m); ++row;
            values.push_back((y - y_max) / diameter_m);
            derivative(n + turbine, 1.0 / diameter_m); ++row;
        }
    } else if (data_->boundary_kind == Data::BoundaryKind::circle) {
        const double cx = data_->circle[0];
        const double cy = data_->circle[1];
        const double radius = data_->circle[2];
        const double scale = radius * radius;
        for (int turbine = 0; turbine < n; ++turbine) {
            const double dx = coordinates[static_cast<std::size_t>(turbine)] - cx;
            const double dy = coordinates[static_cast<std::size_t>(n + turbine)] - cy;
            values.push_back((dx * dx + dy * dy) / scale - 1.0);
            derivative(turbine, 2.0 * dx / scale);
            derivative(n + turbine, 2.0 * dy / scale);
            ++row;
        }
    } else {
        for (int turbine = 0; turbine < n; ++turbine) {
            const double x = coordinates[static_cast<std::size_t>(turbine)];
            const double y = coordinates[static_cast<std::size_t>(n + turbine)];
            for (std::size_t vertex = 0; vertex < data_->polygon.size(); ++vertex) {
                const Point& a = data_->polygon[vertex];
                const Point& b = data_->polygon[(vertex + 1U) % data_->polygon.size()];
                const double ex = b.x_m - a.x_m;
                const double ey = b.y_m - a.y_m;
                const double length = std::hypot(ex, ey);
                values.push_back(-(
                    ex * (y - a.y_m) - ey * (x - a.x_m)
                ) / (length * diameter_m));
                derivative(turbine, ey / (length * diameter_m));
                derivative(n + turbine, -ex / (length * diameter_m));
                ++row;
            }
        }
    }
}

double Problem::maximum_constraint_violation(
    const std::vector<Point>& layout
) const {
    if (layout.size() != static_cast<std::size_t>(data_->turbines)) {
        return std::numeric_limits<double>::infinity();
    }
    double violation = 0.0;
    for (int left = 0; left < data_->turbines; ++left) {
        for (int right = left + 1; right < data_->turbines; ++right) {
            violation = std::max(violation, std::max(
                0.0, minimum_spacing_m - std::hypot(
                    layout[static_cast<std::size_t>(left)].x_m
                        - layout[static_cast<std::size_t>(right)].x_m,
                    layout[static_cast<std::size_t>(left)].y_m
                        - layout[static_cast<std::size_t>(right)].y_m
                )
            ));
        }
    }
    std::vector<double> constraints;
    normalized_constraints(flatten(layout), constraints, nullptr);
    const int pairs = data_->turbines * (data_->turbines - 1) / 2;
    for (std::size_t row = static_cast<std::size_t>(pairs);
         row < constraints.size(); ++row) {
        violation = std::max(
            violation, std::max(0.0, constraints[row]) * diameter_m
        );
    }
    return violation;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const EvaluationSettings& settings,
    fode::PersistentExecutor& executor
) const {
    if (layout.size() != static_cast<std::size_t>(data_->turbines)) {
        throw std::invalid_argument("T84 layout turbine count mismatch");
    }
    const auto started = std::chrono::steady_clock::now();
    Evaluation evaluation;
    evaluation.requested_workers = executor.thread_count();
    evaluation.maximum_constraint_violation_m = maximum_constraint_violation(layout);
    const int variables = 2 * data_->turbines;
    const int wind_count = wind_state_count();
    const std::vector<double> numeric_coordinates = flatten(layout);
    evaluation.directional_power_mw.resize(static_cast<std::size_t>(wind_count));
    std::vector<double> gradients(
        settings.calculate_gradient
            ? static_cast<std::size_t>(wind_count * variables) : 0U,
        0.0
    );
    executor.reset_work_receipt();
    executor.parallel_for(0, wind_count, [&](const int state) {
        if (settings.calculate_gradient) {
            std::vector<Dual> coordinates;
            coordinates.reserve(static_cast<std::size_t>(variables));
            for (int index = 0; index < variables; ++index) {
                coordinates.push_back(Dual::independent(
                    numeric_coordinates[static_cast<std::size_t>(index)], index
                ));
            }
            const Dual power = direction_power_mw(
                coordinates, layout, state, settings, *data_
            );
            evaluation.directional_power_mw[static_cast<std::size_t>(state)] = power.value;
            for (int index = 0; index < variables; ++index) {
                gradients[static_cast<std::size_t>(state * variables + index)] =
                    power.derivative[static_cast<std::size_t>(index)];
            }
        } else {
            evaluation.directional_power_mw[static_cast<std::size_t>(state)] =
                direction_power_mw(
                    numeric_coordinates, layout, state, settings, *data_
                );
        }
    });
    evaluation.observed_workers = executor.work_receipt().peak_region_participants;
    if (settings.calculate_gradient) {
        evaluation.gradient_gwh_per_m.assign(static_cast<std::size_t>(variables), 0.0);
    }
    for (int state = 0; state < wind_count; ++state) {
        const double hub_speed = data_->wind_speed_mps[static_cast<std::size_t>(state)]
            * (settings.wake_model == WakeModel::bastankhah
                ? std::pow(hub_height_m / reference_height_m, shear_exponent)
                : 1.0);
        const double weight = data_->wind_probability[static_cast<std::size_t>(state)]
            * hours_per_year / 1000.0;
        evaluation.aep_gwh += weight
            * evaluation.directional_power_mw[static_cast<std::size_t>(state)];
        evaluation.gross_aep_gwh += weight * static_cast<double>(data_->turbines)
            * turbine_power_mw(hub_speed, *data_);
        if (settings.calculate_gradient) {
            for (int index = 0; index < variables; ++index) {
                evaluation.gradient_gwh_per_m[static_cast<std::size_t>(index)] +=
                    weight * gradients[static_cast<std::size_t>(state * variables + index)];
            }
        }
    }
    if (evaluation.gross_aep_gwh > 0.0) {
        evaluation.wake_loss_percent = 100.0 * (
            1.0 - evaluation.aep_gwh / evaluation.gross_aep_gwh
        );
    }
    evaluation.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    return evaluation;
}

std::vector<Evaluation> Problem::evaluate_population(
    const std::vector<std::vector<Point>>& layouts,
    const EvaluationSettings& settings,
    fode::PersistentExecutor& executor
) const {
    if (settings.calculate_gradient) {
        throw std::invalid_argument("T84 population gradients are not requested by ALPSO");
    }
    const auto started = std::chrono::steady_clock::now();
    std::vector<Evaluation> output(layouts.size());
    executor.reset_work_receipt();
    executor.parallel_for(0, static_cast<int>(layouts.size()), [&](const int index) {
        output[static_cast<std::size_t>(index)] = evaluate_serial(
            *data_, layouts[static_cast<std::size_t>(index)], settings
        );
        output[static_cast<std::size_t>(index)].maximum_constraint_violation_m =
            maximum_constraint_violation(layouts[static_cast<std::size_t>(index)]);
    });
    const auto receipt = executor.work_receipt();
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    for (Evaluation& evaluation : output) {
        evaluation.requested_workers = executor.thread_count();
        evaluation.observed_workers = receipt.peak_region_participants;
        evaluation.seconds = elapsed / std::max<std::size_t>(1U, output.size());
    }
    return output;
}

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers < 1 || config.start_index < 0 || config.start_index >= 200) {
        throw std::invalid_argument("invalid T84 run configuration");
    }
    if (config.wake_model == WakeModel::jensen_cosine && problem.case_id() != 2) {
        throw std::invalid_argument("paper tests Jensen cosine only on T84 case 2");
    }
    if (config.optimizer == OptimizerFamily::augmented_lagrangian_pso
        && config.use_wec && (
            problem.case_id() != 2 || config.wake_model != WakeModel::bastankhah
        )) {
        throw std::invalid_argument("paper tests ALPSO+WEC only on Bastankhah case 2");
    }
    const auto started = std::chrono::steady_clock::now();
    fode::PersistentExecutor executor(config.workers);
    RunResult result;
    result.problem_semantic_id = problem.semantic_id();
    result.case_id = problem.case_id();
    result.start_index = config.start_index;
    result.seed = config.seed;
    result.wake_model = config.wake_model;
    result.optimizer = config.optimizer;
    result.use_wec = config.use_wec;
    result.requested_workers = config.workers;
    if (config.optimizer == OptimizerFamily::slsqp_open_snopt_replacement) {
        result.method_semantic_id = config.use_wec
            ? "t84_slsqp_wec_v1" : "t84_slsqp_control_v1";
    } else {
        result.method_semantic_id = config.use_wec
            ? "t84_alpso_wec_v1" : "t84_alpso_control_v1";
    }

    EvaluationSettings assessment_settings;
    assessment_settings.wake_model = config.wake_model;
    assessment_settings.wec_factor = 1.0;
    assessment_settings.turbulence_mode =
        config.wake_model == WakeModel::bastankhah
        ? TurbulenceMode::hard_local : TurbulenceMode::ambient_only;
    std::vector<Point> layout = problem.start(config.start_index);
    result.initial_assessment = problem.evaluate(
        layout, assessment_settings, executor
    );
    auto stages = lifecycle(config.wake_model, config.use_wec);
    if (config.optimizer == OptimizerFamily::augmented_lagrangian_pso) {
        stages.clear();
        if (config.use_wec) {
            for (const double factor : {3.0, 2.6, 2.2, 1.8, 1.4, 1.0}) {
                stages.emplace_back(factor, TurbulenceMode::ambient_only);
            }
        }
        // The final author ALPSO drivers restore TI method 4, not the
        // differentiable smooth-maximum method used for SNOPT derivatives.
        stages.emplace_back(1.0, TurbulenceMode::hard_local);
    }

    if (config.optimizer == OptimizerFamily::slsqp_open_snopt_replacement) {
        const int variables = 2 * problem.turbine_count();
        const auto lower = problem.lower_bounds();
        const auto upper = problem.upper_bounds();
        for (const auto& [factor, turbulence] : stages) {
            const auto stage_started = std::chrono::steady_clock::now();
            EvaluationSettings settings;
            settings.wake_model = config.wake_model;
            settings.wec_factor = factor;
            settings.turbulence_mode = turbulence;
            StageReceipt receipt;
            receipt.wec_factor = factor;
            receipt.turbulence_mode = turbulence;
            receipt.start_aep_gwh = problem.evaluate(layout, settings, executor).aep_gwh;
            std::vector<double> x = flatten(layout);
            ObjectiveContext objective;
            objective.problem = &problem;
            objective.executor = &executor;
            objective.settings = settings;
            objective.variables = variables;
            ConstraintContext constraints{&problem, variables};
            const std::vector<double> tolerances(
                static_cast<std::size_t>(problem.constraint_count()), 1.0e-7
            );
            nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, variables);
            if (optimizer == nullptr) throw std::runtime_error("cannot create T84 SLSQP");
            nlopt_result status = NLOPT_FAILURE;
            double minimum = 0.0;
            try {
                if (nlopt_set_lower_bounds(optimizer, lower.data()) < 0
                    || nlopt_set_upper_bounds(optimizer, upper.data()) < 0
                    || nlopt_set_min_objective(
                        optimizer, objective_callback, &objective
                    ) < 0
                    || nlopt_add_inequality_mconstraint(
                        optimizer, problem.constraint_count(),
                        constraint_callback, &constraints, tolerances.data()
                    ) < 0
                    || nlopt_set_xtol_rel(
                        optimizer,
                        stage_tolerance(
                            problem.case_id(),
                            turbulence == TurbulenceMode::smooth_local,
                            config.use_wec, config.wake_model
                        )
                    ) < 0
                    || nlopt_set_maxeval(
                        optimizer,
                        config.smoke ? 4
                            : config.maximum_slsqp_evaluations_per_stage
                    ) < 0) {
                    throw std::runtime_error("cannot configure T84 SLSQP");
                }
                status = nlopt_optimize(optimizer, x.data(), &minimum);
            } catch (...) {
                nlopt_destroy(optimizer);
                throw;
            }
            nlopt_destroy(optimizer);
            const auto candidate = unflatten(x);
            if (problem.maximum_constraint_violation(candidate) <= 1.0e-3
                || problem.maximum_constraint_violation(candidate)
                    < problem.maximum_constraint_violation(layout)) {
                layout = candidate;
            }
            const Evaluation end = problem.evaluate(layout, settings, executor);
            receipt.end_aep_gwh = end.aep_gwh;
            receipt.objective_calls = objective.objective_calls;
            receipt.gradient_calls = objective.gradient_calls;
            receipt.optimizer_status = static_cast<int>(status);
            receipt.optimizer_status_name = nlopt_status_name(status);
            receipt.seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - stage_started
            ).count();
            result.executed_function_calls += objective.objective_calls;
            result.evaluator_seconds += objective.evaluator_seconds;
            result.observed_workers = std::max(
                result.observed_workers,
                std::max(objective.observed_workers, end.observed_workers)
            );
            result.stages.push_back(std::move(receipt));
        }
        result.paper_function_call_budget = 0;
    } else {
        constexpr int population_size = 30;
        const AlpsoSchedule base = alpso_schedule(problem.case_id());
        const int inner_iterations = config.smoke ? 1 : base.inner;
        const int outer_iterations = config.smoke
            ? 1 : (config.use_wec ? 5 : base.outer);
        result.paper_function_call_budget = config.use_wec ? 26460 : base.paper_calls;
        const int variables = 2 * problem.turbine_count();
        const auto lower = problem.lower_bounds();
        const auto upper = problem.upper_bounds();
        std::mt19937_64 generator(
            config.seed ^ (0x9e3779b97f4a7c15ULL
                * static_cast<std::uint64_t>(config.start_index + 1))
        );
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        std::vector<std::vector<double>> position(population_size);
        std::vector<std::vector<double>> velocity(
            population_size, std::vector<double>(static_cast<std::size_t>(variables))
        );
        for (int particle = 0; particle < population_size; ++particle) {
            const int source_index = particle == 0
                ? config.start_index
                : (config.start_index + particle * 37) % 200;
            position[static_cast<std::size_t>(particle)] =
                flatten(problem.start(source_index));
            for (int variable = 0; variable < variables; ++variable) {
                velocity[static_cast<std::size_t>(particle)][static_cast<std::size_t>(variable)] =
                    0.01 * (upper[static_cast<std::size_t>(variable)]
                        - lower[static_cast<std::size_t>(variable)])
                    * (2.0 * uniform(generator) - 1.0);
            }
        }
        std::vector<std::vector<double>> personal_best = position;
        std::vector<double> personal_score(population_size,
            std::numeric_limits<double>::infinity());
        std::vector<double> global_best = position[0];
        double global_score = std::numeric_limits<double>::infinity();
        std::vector<double> best_feasible = position[0];
        double best_feasible_aep = -std::numeric_limits<double>::infinity();
        if (problem.maximum_constraint_violation(unflatten(best_feasible)) <= 1.0e-6) {
            best_feasible_aep = result.initial_assessment.aep_gwh;
        }
        std::vector<double> multipliers(
            static_cast<std::size_t>(problem.constraint_count()), 0.0
        );
        double penalty = 2.0;

        for (const auto& [factor, turbulence] : stages) {
            const auto stage_started = std::chrono::steady_clock::now();
            EvaluationSettings settings;
            settings.wake_model = config.wake_model;
            settings.wec_factor = factor;
            settings.turbulence_mode = turbulence;
            StageReceipt receipt;
            receipt.wec_factor = factor;
            receipt.turbulence_mode = turbulence;
            receipt.start_aep_gwh = problem.evaluate(
                unflatten(global_best), settings, executor
            ).aep_gwh;
            int stage_evaluations = 0;
            for (int outer = 0; outer < outer_iterations; ++outer) {
                for (int inner = 0; inner < inner_iterations; ++inner) {
                    std::vector<std::vector<Point>> layouts;
                    layouts.reserve(population_size);
                    for (const auto& particle : position) {
                        layouts.push_back(unflatten(particle));
                    }
                    const auto evaluated = problem.evaluate_population(
                        layouts, settings, executor
                    );
                    stage_evaluations += population_size;
                    result.evaluator_seconds += std::accumulate(
                        evaluated.begin(), evaluated.end(), 0.0,
                        [](const double sum, const Evaluation& value) {
                            return sum + value.seconds;
                        }
                    );
                    if (!evaluated.empty()) {
                        result.observed_workers = std::max(
                            result.observed_workers, evaluated[0].observed_workers
                        );
                    }
                    for (int particle = 0; particle < population_size; ++particle) {
                        std::vector<double> constraints;
                        problem.normalized_constraints(
                            position[static_cast<std::size_t>(particle)],
                            constraints, nullptr
                        );
                        const double score = augmented_score(
                            evaluated[static_cast<std::size_t>(particle)].aep_gwh,
                            evaluated[static_cast<std::size_t>(particle)].gross_aep_gwh,
                            constraints, multipliers, penalty
                        );
                        if (score < personal_score[static_cast<std::size_t>(particle)]) {
                            personal_score[static_cast<std::size_t>(particle)] = score;
                            personal_best[static_cast<std::size_t>(particle)] =
                                position[static_cast<std::size_t>(particle)];
                        }
                        if (score < global_score) {
                            global_score = score;
                            global_best = position[static_cast<std::size_t>(particle)];
                        }
                        if (
                            evaluated[static_cast<std::size_t>(particle)]
                                .maximum_constraint_violation_m <= 1.0e-6
                            && evaluated[static_cast<std::size_t>(particle)].aep_gwh
                                > best_feasible_aep
                        ) {
                            best_feasible_aep =
                                evaluated[static_cast<std::size_t>(particle)].aep_gwh;
                            best_feasible = position[static_cast<std::size_t>(particle)];
                        }
                    }
                    // Clerc constriction defaults are the declared replacement
                    // for pyOptSparse defaults not fixed by the paper.
                    constexpr double inertia = 0.7298;
                    constexpr double attraction = 1.49618;
                    for (int particle = 0; particle < population_size; ++particle) {
                        for (int variable = 0; variable < variables; ++variable) {
                            const std::size_t p = static_cast<std::size_t>(particle);
                            const std::size_t v = static_cast<std::size_t>(variable);
                            const double r1 = uniform(generator);
                            const double r2 = uniform(generator);
                            velocity[p][v] = inertia * velocity[p][v]
                                + attraction * r1 * (personal_best[p][v] - position[p][v])
                                + attraction * r2 * (global_best[v] - position[p][v]);
                            if (uniform(generator) < 0.01) {
                                velocity[p][v] += 0.01 * (upper[v] - lower[v])
                                    * (2.0 * uniform(generator) - 1.0);
                            }
                            position[p][v] = std::clamp(
                                position[p][v] + velocity[p][v], lower[v], upper[v]
                            );
                        }
                    }
                }
                std::vector<double> constraints;
                problem.normalized_constraints(global_best, constraints, nullptr);
                double maximum = 0.0;
                for (std::size_t index = 0; index < constraints.size(); ++index) {
                    const double positive = std::max(0.0, constraints[index]);
                    maximum = std::max(maximum, positive);
                    multipliers[index] = std::max(
                        0.0, multipliers[index] + penalty * positive
                    );
                }
                if (maximum > 1.0e-3) penalty = std::min(1.0e8, penalty * 1.2);
            }
            // Paper accounting evaluates the complete swarm at each WEC
            // stage transition, including the first and final stages.
            std::vector<std::vector<Point>> transition_layouts;
            for (const auto& particle : position) {
                transition_layouts.push_back(unflatten(particle));
            }
            const auto transition = problem.evaluate_population(
                transition_layouts, settings, executor
            );
            stage_evaluations += population_size;
            result.evaluator_seconds += std::accumulate(
                transition.begin(), transition.end(), 0.0,
                [](const double sum, const Evaluation& value) {
                    return sum + value.seconds;
                }
            );
            receipt.population_evaluations = stage_evaluations;
            receipt.objective_calls = stage_evaluations;
            receipt.optimizer_status = 1;
            receipt.optimizer_status_name = "paper_schedule_completed";
            receipt.end_aep_gwh = problem.evaluate(
                unflatten(global_best), settings, executor
            ).aep_gwh;
            receipt.seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - stage_started
            ).count();
            result.executed_function_calls += stage_evaluations;
            result.stages.push_back(std::move(receipt));
        }
        layout = unflatten(best_feasible);
    }

    result.final_layout = layout;
    result.final_assessment = problem.evaluate(layout, assessment_settings, executor);
    result.observed_workers = std::max(
        result.observed_workers, result.final_assessment.observed_workers
    );
    result.end_to_end_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started
    ).count();
    result.optimizer_seconds = std::max(
        0.0, result.end_to_end_seconds - result.evaluator_seconds
    );
    std::uint64_t hash = 1469598103934665603ULL;
    for (const Point& point : result.final_layout) {
        hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = hash_mix(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    hash = hash_mix(
        hash, std::bit_cast<std::uint64_t>(result.final_assessment.aep_gwh)
    );
    result.scientific_hash = hash;
    return result;
}

}  // namespace core99::t84
