/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0581 Cumulative-Curl-lineage evaluator, coloured
forward automatic differentiation and paired projected optimizer.
Paper/DOI, public source, missing assets, conflicts, reconstruction,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/varela_l0581.hpp.
Controlling contract: shared/contracts/core99_l0581_sparse_gradient_2023.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/varela_l0581.hpp"

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
#include <utility>
#include <vector>

namespace core99::l0581 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int dual_width = 8;
constexpr double rotor_diameter_m = 126.0;
constexpr double ambient_speed = 8.0;
constexpr double ambient_ti = 0.06;
constexpr double minimum_spacing_d = 2.0;
constexpr double ring_radius_step_d = 5.1;
constexpr double initial_threshold = 1.0e-8;
constexpr double minimum_threshold = 1.0e-16;

template <int Width>
struct Dual {
    double value = 0.0;
    std::array<double, Width> derivative{};

    Dual() = default;
    Dual(const double input) : value(input) {}
};

template <int W>
Dual<W> operator+(const Dual<W>& left, const Dual<W>& right) {
    Dual<W> result(left.value + right.value);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)]
            + right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template <int W>
Dual<W> operator-(const Dual<W>& left, const Dual<W>& right) {
    Dual<W> result(left.value - right.value);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)]
            - right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template <int W>
Dual<W> operator-(const Dual<W>& value) {
    Dual<W> result(-value.value);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            -value.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template <int W>
Dual<W> operator*(const Dual<W>& left, const Dual<W>& right) {
    Dual<W> result(left.value * right.value);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)] * right.value
            + left.value * right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template <int W>
Dual<W> operator/(const Dual<W>& left, const Dual<W>& right) {
    const double inverse = 1.0 / right.value;
    Dual<W> result(left.value * inverse);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            (left.derivative[static_cast<std::size_t>(lane)]
             - result.value * right.derivative[static_cast<std::size_t>(lane)])
            * inverse;
    }
    return result;
}

template <int W>
Dual<W>& operator+=(Dual<W>& left, const Dual<W>& right) {
    left = left + right;
    return left;
}

template <typename T>
double primal(const T& value) {
    return static_cast<double>(value);
}

template <int W>
double primal(const Dual<W>& value) {
    return value.value;
}

double d_exp(const double value) { return std::exp(value); }

template <int W>
Dual<W> d_exp(const Dual<W>& value) {
    const double exponential = std::exp(value.value);
    Dual<W> result(exponential);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] = exponential
            * value.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

double d_sqrt(const double value) { return std::sqrt(value); }

template <int W>
Dual<W> d_sqrt(const Dual<W>& value) {
    const double root = std::sqrt(std::max(value.value, 0.0));
    Dual<W> result(root);
    const double factor = root > 0.0 ? 0.5 / root : 0.0;
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] = factor
            * value.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

double d_pow(const double base, const double exponent) {
    return std::pow(base, exponent);
}

template <int W>
Dual<W> d_pow(const Dual<W>& base, const Dual<W>& exponent) {
    const double safe_base = std::max(base.value, 1.0e-300);
    const double value = std::pow(safe_base, exponent.value);
    Dual<W> result(value);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] = value * (
            exponent.derivative[static_cast<std::size_t>(lane)]
                * std::log(safe_base)
            + exponent.value
                * base.derivative[static_cast<std::size_t>(lane)] / safe_base
        );
    }
    return result;
}

template <int W>
Dual<W> d_pow(const Dual<W>& base, const double exponent) {
    return d_pow(base, Dual<W>(exponent));
}

double digamma(double value) {
    double result = 0.0;
    while (value < 8.0) {
        result -= 1.0 / value;
        value += 1.0;
    }
    const double inverse = 1.0 / value;
    const double inverse2 = inverse * inverse;
    result += std::log(value) - 0.5 * inverse
        - inverse2 * (1.0 / 12.0
        - inverse2 * (1.0 / 120.0
        - inverse2 * (1.0 / 252.0
        - inverse2 * (1.0 / 240.0))));
    return result;
}

double d_gamma(const double value) { return std::tgamma(value); }

template <int W>
Dual<W> d_gamma(const Dual<W>& value) {
    const double gamma = std::tgamma(value.value);
    const double factor = gamma * digamma(value.value);
    Dual<W> result(gamma);
    for (int lane = 0; lane < W; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] = factor
            * value.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template <typename T>
T smooth_absolute(const T& value, const double smoothing) {
    return d_sqrt(value * value + T(smoothing * smoothing));
}

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

template <typename T>
T interpolate_curve(
    const T& velocity,
    const std::array<double, 15>& values
) {
    static constexpr std::array<double, 15> speeds{
        0.0, 2.0, 2.5, 3.0, 3.5, 4.0, 4.5, 5.0,
        5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5,
    };
    const double query = primal(velocity);
    if (query <= speeds.front()) return T(values.front());
    if (query >= speeds.back()) return T(values.back());
    const auto upper = std::upper_bound(speeds.begin(), speeds.end(), query);
    const std::size_t right = static_cast<std::size_t>(
        std::distance(speeds.begin(), upper)
    );
    const std::size_t left = right - 1U;
    const double width = speeds[right] - speeds[left];
    const T fraction = (velocity - T(speeds[left])) / T(width);
    return T(values[left]) + fraction * T(values[right] - values[left]);
}

template <typename T>
T thrust_coefficient(const T& velocity) {
    static constexpr std::array<double, 15> ct{
        0.0, 0.0, 0.0, 0.99, 0.99, 0.97373036, 0.92826162,
        0.89210543, 0.86100905, 0.835423, 0.81237673, 0.79225789,
        0.77584769, 0.7629228, 0.76156073,
    };
    T result = interpolate_curve(velocity, ct);
    if (primal(result) >= 1.0) result = T(1.0 - 1.0e-9);
    return result;
}

template <typename T>
T normalized_power(const T& velocity) {
    static constexpr std::array<double, 15> cp{
        0.0, 0.0, 0.0, 0.178085, 0.289075, 0.349022, 0.384728,
        0.406059, 0.420228, 0.428823, 0.433873, 0.436223,
        0.436845, 0.436575, 0.436511,
    };
    if (primal(velocity) < 3.0) return T(0.0);
    const T coefficient = interpolate_curve(velocity, cp);
    return coefficient * d_pow(velocity, 3.0)
        / T(cp[13] * ambient_speed * ambient_speed * ambient_speed);
}

int rings_for_turbines(const int turbines) {
    int total = 1;
    for (int ring = 1; ring <= 10; ++ring) {
        total += static_cast<int>(std::floor(6.4 * static_cast<double>(ring)));
        if (total == turbines) return ring;
        if (total > turbines) break;
    }
    throw std::invalid_argument("L0581 unsupported paper farm size");
}

double minimum_spacing(const Layout& layout) {
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            const double dx = layout[left].x_d - layout[right].x_d;
            const double dy = layout[left].y_d - layout[right].y_d;
            minimum = std::min(minimum, std::hypot(dx, dy));
        }
    }
    return minimum;
}

template <typename T>
std::vector<T> turbine_power_state(
    const std::vector<T>& variables,
    const double direction_degrees
) {
    const int turbines = static_cast<int>(variables.size() / 2U);
    const double direction = direction_degrees * std::numbers::pi / 180.0;
    double cartesian = 1.5 * std::numbers::pi - direction;
    if (cartesian < 0.0) cartesian += 2.0 * std::numbers::pi;
    const double cosine = std::cos(-cartesian);
    const double sine = std::sin(-cartesian);

    std::vector<T> x(static_cast<std::size_t>(turbines));
    std::vector<T> y(static_cast<std::size_t>(turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        const T& global_x = variables[static_cast<std::size_t>(turbine)];
        const T& global_y = variables[static_cast<std::size_t>(turbines + turbine)];
        x[static_cast<std::size_t>(turbine)] =
            global_x * T(cosine) - global_y * T(sine);
        y[static_cast<std::size_t>(turbine)] =
            global_x * T(sine) + global_y * T(cosine);
    }
    std::vector<int> order(static_cast<std::size_t>(turbines));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        const double lx = primal(x[static_cast<std::size_t>(left)]);
        const double rx = primal(x[static_cast<std::size_t>(right)]);
        return lx == rx ? left < right : lx < rx;
    });

    std::vector<T> velocities(static_cast<std::size_t>(turbines), T(ambient_speed));
    std::vector<T> ct(static_cast<std::size_t>(turbines), T(0.0));
    std::vector<T> contribution(
        static_cast<std::size_t>(turbines * turbines), T(0.0)
    );
    std::vector<T> sigma_squared(
        static_cast<std::size_t>(turbines * turbines), T(0.0)
    );

    for (int downstream_position = 0;
         downstream_position < turbines; ++downstream_position) {
        const int downstream = order[static_cast<std::size_t>(downstream_position)];
        T deficit_sum(0.0);
        for (int upstream_position = 0;
             upstream_position < downstream_position; ++upstream_position) {
            const int upstream = order[static_cast<std::size_t>(upstream_position)];
            const T dx = x[static_cast<std::size_t>(downstream)]
                - x[static_cast<std::size_t>(upstream)];
            if (primal(dx) <= 1.0e-6 / rotor_diameter_m) continue;
            const T x_tilde = dx;
            const T m = T(3.11) * d_exp(T(-0.68) * x_tilde) + T(2.41);
            const T a1 = d_pow(T(2.0), T(2.0) / m - T(1.0));
            const T a2 = a1 * a1;
            const T upstream_ct = ct[static_cast<std::size_t>(upstream)];
            const T one_minus_ct = T(1.0) - upstream_ct;
            const T beta = T(0.5) * (T(1.0) + d_sqrt(one_minus_ct))
                / d_sqrt(one_minus_ct);
            const T epsilon = (T(0.0563691592) * upstream_ct
                               + T(0.13290157)) * d_sqrt(beta);
            const T sigma = (T(0.179367259 * ambient_ti + 0.0118889215)
                             * x_tilde) + epsilon;
            const T sigma_n = sigma * sigma;
            const std::size_t pair = static_cast<std::size_t>(
                downstream * turbines + upstream
            );
            sigma_squared[pair] = sigma_n;
            const T lateral = y[static_cast<std::size_t>(upstream)]
                - y[static_cast<std::size_t>(downstream)];
            const T r_tilde = d_sqrt(lateral * lateral);
            const T exponent = -d_pow(r_tilde, m) / (T(2.0) * sigma_n);
            if (primal(exponent) <= -750.0) continue;
            T sum_c(0.0);
            for (int prior_position = 0;
                 prior_position < upstream_position; ++prior_position) {
                const int prior = order[static_cast<std::size_t>(prior_position)];
                const std::size_t prior_pair = static_cast<std::size_t>(
                    downstream * turbines + prior
                );
                if (primal(contribution[prior_pair]) == 0.0) continue;
                const T sigma_i = sigma_squared[prior_pair];
                const T inverse_sum = T(1.0) / (sigma_n + sigma_i);
                const T delta_y = y[static_cast<std::size_t>(upstream)]
                    - y[static_cast<std::size_t>(prior)];
                const T overlap_exponent = T(-0.5) * inverse_sum
                    * delta_y * delta_y;
                if (primal(overlap_exponent) <= -750.0) continue;
                const T lambda = sigma_n * inverse_sum
                    * d_exp(overlap_exponent);
                sum_c += lambda * contribution[prior_pair];
            }
            const T remaining = T(1.0) - sum_c / T(ambient_speed);
            const T gamma_term = d_gamma(T(2.0) / m);
            const T calc = smooth_absolute(
                a2 - (m * upstream_ct)
                    / (T(16.0) * gamma_term
                       * d_pow(sigma_n, T(2.0) / m)
                       * remaining * remaining),
                0.1
            );
            contribution[pair] = remaining * (a1 - d_sqrt(calc));
            const T deficit = contribution[pair] * d_exp(exponent);
            deficit_sum += velocities[static_cast<std::size_t>(upstream)]
                * deficit;
        }
        T velocity = T(ambient_speed) - deficit_sum;
        if (primal(velocity) < 0.0) velocity = T(0.0);
        velocities[static_cast<std::size_t>(downstream)] = velocity;
        ct[static_cast<std::size_t>(downstream)] = thrust_coefficient(velocity);
    }

    std::vector<T> powers(static_cast<std::size_t>(turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        powers[static_cast<std::size_t>(turbine)] = normalized_power(
            velocities[static_cast<std::size_t>(turbine)]
        );
    }
    return powers;
}

std::vector<double> flatten(const Layout& layout) {
    const int turbines = static_cast<int>(layout.size());
    std::vector<double> result(static_cast<std::size_t>(2 * turbines));
    for (int turbine = 0; turbine < turbines; ++turbine) {
        result[static_cast<std::size_t>(turbine)] =
            layout[static_cast<std::size_t>(turbine)].x_d;
        result[static_cast<std::size_t>(turbines + turbine)] =
            layout[static_cast<std::size_t>(turbine)].y_d;
    }
    return result;
}

using Pattern = std::vector<unsigned char>;

struct JacobianWork {
    int colors = 0;
    int sweeps = 0;
    double normalized_aep = 0.0;
    std::vector<double> jacobian;
    Pattern pattern;
    std::vector<int> color;
};

std::vector<int> color_pattern(
    const Pattern& pattern,
    const int outputs,
    const int variables
) {
    std::vector<unsigned char> conflicts(
        static_cast<std::size_t>(variables * variables), 0U
    );
    std::vector<int> active;
    for (int row = 0; row < outputs; ++row) {
        active.clear();
        for (int column = 0; column < variables; ++column) {
            if (pattern[static_cast<std::size_t>(row * variables + column)]) {
                active.push_back(column);
            }
        }
        for (std::size_t left = 0; left < active.size(); ++left) {
            for (std::size_t right = left + 1U; right < active.size(); ++right) {
                conflicts[static_cast<std::size_t>(
                    active[left] * variables + active[right]
                )] = 1U;
                conflicts[static_cast<std::size_t>(
                    active[right] * variables + active[left]
                )] = 1U;
            }
        }
    }
    std::vector<int> degree(static_cast<std::size_t>(variables), 0);
    std::vector<int> order(static_cast<std::size_t>(variables));
    std::iota(order.begin(), order.end(), 0);
    for (int column = 0; column < variables; ++column) {
        bool present = false;
        for (int row = 0; row < outputs; ++row) {
            present = present || pattern[static_cast<std::size_t>(
                row * variables + column
            )] != 0U;
        }
        if (!present) {
            degree[static_cast<std::size_t>(column)] = -1;
            continue;
        }
        for (int other = 0; other < variables; ++other) {
            degree[static_cast<std::size_t>(column)] += conflicts[
                static_cast<std::size_t>(column * variables + other)
            ] != 0U ? 1 : 0;
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](const int left, const int right) {
        const int dl = degree[static_cast<std::size_t>(left)];
        const int dr = degree[static_cast<std::size_t>(right)];
        return dl == dr ? left < right : dl > dr;
    });
    std::vector<int> colors(static_cast<std::size_t>(variables), -1);
    std::vector<unsigned char> unavailable(static_cast<std::size_t>(variables), 0U);
    for (const int column : order) {
        if (degree[static_cast<std::size_t>(column)] < 0) continue;
        std::fill(unavailable.begin(), unavailable.end(), 0U);
        for (int other = 0; other < variables; ++other) {
            if (conflicts[static_cast<std::size_t>(column * variables + other)]
                && colors[static_cast<std::size_t>(other)] >= 0) {
                unavailable[static_cast<std::size_t>(
                    colors[static_cast<std::size_t>(other)]
                )] = 1U;
            }
        }
        int color = 0;
        while (unavailable[static_cast<std::size_t>(color)]) ++color;
        colors[static_cast<std::size_t>(column)] = color;
    }
    return colors;
}

JacobianWork calculate_with_colors(
    const Layout& layout,
    const double direction,
    const Pattern& pattern,
    const std::vector<int>& colors,
    fode::PersistentExecutor& executor
) {
    const int turbines = static_cast<int>(layout.size());
    const int variables = 2 * turbines;
    JacobianWork result;
    result.pattern = pattern;
    result.color = colors;
    for (const int color : colors) result.colors = std::max(result.colors, color + 1);
    result.sweeps = (result.colors + dual_width - 1) / dual_width;
    result.jacobian.assign(static_cast<std::size_t>(turbines * variables), 0.0);
    const std::vector<double> base = flatten(layout);
    const auto base_outputs = turbine_power_state(base, direction);
    result.normalized_aep = std::accumulate(
        base_outputs.begin(), base_outputs.end(), 0.0
    );
    executor.parallel_for(0, result.sweeps, [&](const int sweep) {
        std::vector<Dual<dual_width>> seeded(static_cast<std::size_t>(variables));
        for (int column = 0; column < variables; ++column) {
            seeded[static_cast<std::size_t>(column)].value =
                base[static_cast<std::size_t>(column)];
            const int color = colors[static_cast<std::size_t>(column)];
            const int lane = color - sweep * dual_width;
            if (lane >= 0 && lane < dual_width) {
                seeded[static_cast<std::size_t>(column)]
                    .derivative[static_cast<std::size_t>(lane)] = 1.0;
            }
        }
        const auto outputs = turbine_power_state(seeded, direction);
        for (int row = 0; row < turbines; ++row) {
            for (int column = 0; column < variables; ++column) {
                const int color = colors[static_cast<std::size_t>(column)];
                const int lane = color - sweep * dual_width;
                const std::size_t entry = static_cast<std::size_t>(
                    row * variables + column
                );
                if (lane >= 0 && lane < dual_width && pattern[entry]) {
                    result.jacobian[entry] = outputs[static_cast<std::size_t>(row)]
                        .derivative[static_cast<std::size_t>(lane)];
                }
            }
        }
    });
    return result;
}

Pattern pattern_from_jacobian(
    const std::vector<double>& jacobian,
    const double threshold
) {
    Pattern pattern(jacobian.size(), 0U);
    for (std::size_t index = 0; index < jacobian.size(); ++index) {
        pattern[index] = std::abs(jacobian[index]) > threshold ? 1U : 0U;
    }
    return pattern;
}

JacobianWork dense_jacobian(
    const Layout& layout,
    const double direction,
    fode::PersistentExecutor& executor
) {
    const int turbines = static_cast<int>(layout.size());
    const int variables = 2 * turbines;
    Pattern pattern(static_cast<std::size_t>(turbines * variables), 1U);
    std::vector<int> colors(static_cast<std::size_t>(variables));
    std::iota(colors.begin(), colors.end(), 0);
    return calculate_with_colors(layout, direction, pattern, colors, executor);
}

JacobianWork sparse_jacobian(
    const Layout& layout,
    const double direction,
    const Pattern& pattern,
    fode::PersistentExecutor& executor
) {
    const int turbines = static_cast<int>(layout.size());
    const int variables = 2 * turbines;
    return calculate_with_colors(
        layout, direction, pattern,
        color_pattern(pattern, turbines, variables), executor
    );
}

std::vector<double> summed_gradient(const JacobianWork& work, const int turbines) {
    const int variables = 2 * turbines;
    std::vector<double> result(static_cast<std::size_t>(variables), 0.0);
    for (int column = 0; column < variables; ++column) {
        for (int row = 0; row < turbines; ++row) {
            result[static_cast<std::size_t>(column)] += work.jacobian[
                static_cast<std::size_t>(row * variables + column)
            ];
        }
    }
    return result;
}

std::uint64_t hash_gradient(
    const std::vector<double>& gradient,
    const double objective,
    const int colors
) {
    std::uint64_t hash = 0x0581c0110aedULL;
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(objective));
    hash = mix_hash(hash, static_cast<std::uint64_t>(colors));
    for (const double value : gradient) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    return hash;
}

struct ResourceState {
    double direction = 0.0;
    double probability = 0.0;
};

constexpr std::array<ResourceState, 12> nantucket{{
    {10.0, 0.077}, {40.0, 0.067}, {70.0, 0.044},
    {100.0, 0.046}, {130.0, 0.055}, {160.0, 0.067},
    {190.0, 0.091}, {220.0, 0.129}, {250.0, 0.117},
    {280.0, 0.095}, {310.0, 0.092}, {340.0, 0.082},
}};

double weighted_objective(const Layout& layout) {
    const auto variables = flatten(layout);
    double total = 0.0;
    for (const ResourceState& state : nantucket) {
        const auto powers = turbine_power_state(variables, state.direction);
        total += state.probability
            * std::accumulate(powers.begin(), powers.end(), 0.0);
    }
    return total;
}

void project_layout(Layout& layout, const double boundary_radius) {
    for (Point& point : layout) {
        const double radius = std::hypot(point.x_d, point.y_d);
        if (radius > boundary_radius && radius > 0.0) {
            const double scale = boundary_radius / radius;
            point.x_d *= scale;
            point.y_d *= scale;
        }
    }
    for (int pass = 0; pass < 12; ++pass) {
        bool changed = false;
        for (std::size_t left = 0; left < layout.size(); ++left) {
            for (std::size_t right = left + 1U; right < layout.size(); ++right) {
                double dx = layout[right].x_d - layout[left].x_d;
                double dy = layout[right].y_d - layout[left].y_d;
                double distance = std::hypot(dx, dy);
                if (distance + 1.0e-12 >= minimum_spacing_d) continue;
                if (distance < 1.0e-12) {
                    const double angle = 2.0 * std::numbers::pi
                        * static_cast<double>((left * 131U + right * 17U) % 997U)
                        / 997.0;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    distance = 1.0;
                }
                const double shift = 0.5 * (minimum_spacing_d - distance);
                const double ux = dx / distance;
                const double uy = dy / distance;
                layout[left].x_d -= shift * ux;
                layout[left].y_d -= shift * uy;
                layout[right].x_d += shift * ux;
                layout[right].y_d += shift * uy;
                changed = true;
            }
        }
        for (Point& point : layout) {
            const double radius = std::hypot(point.x_d, point.y_d);
            if (radius > boundary_radius && radius > 0.0) {
                const double scale = boundary_radius / radius;
                point.x_d *= scale;
                point.y_d *= scale;
            }
        }
        if (!changed) break;
    }
}

}  // namespace

std::vector<int> paper_accuracy_sizes() {
    return {38, 63, 95, 133, 177, 228, 285, 349};
}

FarmSpec farm_spec(const int turbines) {
    const int rings = rings_for_turbines(turbines);
    const Layout layout = round_layout(turbines);
    return {
        turbines,
        rings,
        ring_radius_step_d * static_cast<double>(rings),
        minimum_spacing(layout),
    };
}

Layout round_layout(const int turbines) {
    const int rings = rings_for_turbines(turbines);
    Layout layout;
    layout.reserve(static_cast<std::size_t>(turbines));
    layout.push_back({0.0, 0.0});
    for (int ring = 1; ring <= rings; ++ring) {
        const int count = static_cast<int>(
            std::floor(6.4 * static_cast<double>(ring))
        );
        const double radius = ring_radius_step_d * static_cast<double>(ring);
        for (int index = 0; index < count; ++index) {
            const double angle = 2.0 * std::numbers::pi
                * static_cast<double>(index) / static_cast<double>(count);
            layout.push_back({radius * std::cos(angle), radius * std::sin(angle)});
        }
    }
    if (static_cast<int>(layout.size()) != turbines) {
        throw std::logic_error("L0581 ring sequence does not match paper size");
    }
    return layout;
}

Layout randomized_start(const int turbines, const std::uint64_t seed) {
    Layout result = round_layout(turbines);
    const fode::CounterRng random(seed);
    for (int turbine = 0; turbine < turbines; ++turbine) {
        result[static_cast<std::size_t>(turbine)].x_d += 4.0 * random.uniform(
            0, 581, static_cast<std::uint64_t>(turbine), 0, 0
        ) - 2.0;
        result[static_cast<std::size_t>(turbine)].y_d += 4.0 * random.uniform(
            0, 581, static_cast<std::uint64_t>(turbine), 1, 0
        ) - 2.0;
    }
    project_layout(result, farm_spec(turbines).boundary_radius_d);
    return result;
}

GradientResult calculate_gradient(
    const Layout& layout,
    const double direction_degrees,
    const GradientMode mode,
    const double threshold,
    const int workers
) {
    if (layout.empty() || workers <= 0 || threshold < 0.0) {
        throw std::invalid_argument("L0581 invalid gradient request");
    }
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const auto start = Clock::now();
    JacobianWork work;
    if (mode == GradientMode::dense) {
        work = dense_jacobian(layout, direction_degrees, executor);
    } else {
        const auto dense = dense_jacobian(layout, direction_degrees, executor);
        const Pattern pattern = pattern_from_jacobian(dense.jacobian, threshold);
        work = sparse_jacobian(layout, direction_degrees, pattern, executor);
    }
    GradientResult result;
    result.turbines = static_cast<int>(layout.size());
    result.variables = 2 * result.turbines;
    result.colors = work.colors;
    result.dual_sweeps = work.sweeps;
    result.requested_workers = workers;
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.threshold = threshold;
    result.normalized_aep = work.normalized_aep;
    result.seconds = elapsed_seconds(start);
    result.gradient = summed_gradient(work, result.turbines);
    result.scientific_hash = hash_gradient(
        result.gradient, result.normalized_aep, result.colors
    );
    return result;
}

AccuracyResult compare_accuracy(
    const int turbines,
    const double threshold,
    const int workers
) {
    const Layout layout = round_layout(turbines);
    fode::PersistentExecutor executor(workers);
    executor.reset_work_receipt();
    const auto dense_start = Clock::now();
    const JacobianWork dense = dense_jacobian(layout, 270.0, executor);
    const double dense_seconds = elapsed_seconds(dense_start);
    const Pattern pattern = pattern_from_jacobian(dense.jacobian, threshold);
    const auto sparse_start = Clock::now();
    const JacobianWork sparse = sparse_jacobian(layout, 270.0, pattern, executor);
    const double sparse_seconds = elapsed_seconds(sparse_start);
    const auto dense_gradient = summed_gradient(dense, turbines);
    const auto sparse_gradient = summed_gradient(sparse, turbines);
    double scale = 0.0;
    double maximum_error = 0.0;
    for (std::size_t index = 0; index < dense_gradient.size(); ++index) {
        scale = std::max(scale, std::abs(dense_gradient[index]));
        maximum_error = std::max(
            maximum_error,
            std::abs(dense_gradient[index] - sparse_gradient[index])
        );
    }
    if (scale > 0.0) maximum_error /= scale;
    AccuracyResult result;
    result.turbines = turbines;
    result.threshold = threshold;
    result.dense_colors = dense.colors;
    result.sparse_colors = sparse.colors;
    result.requested_workers = workers;
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.color_fraction = static_cast<double>(sparse.colors)
        / static_cast<double>(dense.colors);
    result.maximum_scaled_error = maximum_error;
    result.dense_seconds = dense_seconds;
    result.sparse_seconds = sparse_seconds;
    result.scientific_hash = hash_gradient(
        sparse_gradient, sparse.normalized_aep, sparse.colors
    );
    return result;
}

OptimizationResult optimize(const OptimizationConfig& config) {
    if (config.workers <= 0 || config.maximum_iterations <= 0) {
        throw std::invalid_argument("L0581 invalid optimization config");
    }
    const auto total_start = Clock::now();
    const int turbines = 95;
    const int variables = 2 * turbines;
    const double boundary = farm_spec(turbines).boundary_radius_d;
    Layout layout = randomized_start(turbines, config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    std::array<Pattern, 12> patterns;
    std::array<bool, 12> rebuild{};
    rebuild.fill(true);
    int updates_since_threshold = 0;
    double threshold = initial_threshold;
    int pattern_rebuilds = 0;
    int final_colors = 2 * turbines;
    double gradient_seconds = 0.0;
    const double initial_objective = weighted_objective(layout);
    double current_objective = initial_objective;
    OptimizationResult result;
    result.mode = config.mode;
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.best_history.push_back(current_objective);
    const int iteration_limit = config.smoke
        ? std::min(2, config.maximum_iterations)
        : config.maximum_iterations;

    const auto algorithm_start = Clock::now();
    for (int iteration = 0; iteration < iteration_limit; ++iteration) {
        if (config.mode == GradientMode::sparse
            && updates_since_threshold >= 10
            && threshold > minimum_threshold) {
            threshold = std::max(minimum_threshold, threshold * 0.01);
            updates_since_threshold = 0;
            rebuild.fill(true);
        }
        const auto gradient_start = Clock::now();
        std::vector<double> gradient(static_cast<std::size_t>(variables), 0.0);
        for (std::size_t state = 0; state < nantucket.size(); ++state) {
            JacobianWork work;
            if (config.mode == GradientMode::dense) {
                work = dense_jacobian(layout, nantucket[state].direction, executor);
            } else {
                if (rebuild[state]) {
                    const auto full = dense_jacobian(
                        layout, nantucket[state].direction, executor
                    );
                    patterns[state] = pattern_from_jacobian(
                        full.jacobian, threshold
                    );
                    rebuild[state] = false;
                    ++updates_since_threshold;
                    ++pattern_rebuilds;
                }
                work = sparse_jacobian(
                    layout, nantucket[state].direction,
                    patterns[state], executor
                );
                int retained = 0;
                int fallen = 0;
                for (std::size_t entry = 0; entry < work.pattern.size(); ++entry) {
                    if (!work.pattern[entry]) continue;
                    ++retained;
                    if (std::abs(work.jacobian[entry]) <= threshold * 0.01) {
                        ++fallen;
                    }
                }
                if (retained > 0 && 10 * fallen >= retained) {
                    rebuild[state] = true;
                }
            }
            final_colors = work.colors;
            const auto state_gradient = summed_gradient(work, turbines);
            for (int variable = 0; variable < variables; ++variable) {
                gradient[static_cast<std::size_t>(variable)] +=
                    nantucket[state].probability
                    * state_gradient[static_cast<std::size_t>(variable)];
            }
        }
        gradient_seconds += elapsed_seconds(gradient_start);
        double maximum = 0.0;
        for (const double value : gradient) maximum = std::max(maximum, std::abs(value));
        if (maximum <= 1.0e-14) break;
        for (double& value : gradient) value /= maximum;
        double step = 0.5;
        bool accepted = false;
        Layout candidate = layout;
        double candidate_objective = current_objective;
        while (step >= 1.0e-4) {
            candidate = layout;
            for (int turbine = 0; turbine < turbines; ++turbine) {
                candidate[static_cast<std::size_t>(turbine)].x_d += step
                    * gradient[static_cast<std::size_t>(turbine)];
                candidate[static_cast<std::size_t>(turbine)].y_d += step
                    * gradient[static_cast<std::size_t>(turbines + turbine)];
            }
            project_layout(candidate, boundary);
            candidate_objective = weighted_objective(candidate);
            if (candidate_objective > current_objective + 1.0e-10) {
                accepted = true;
                break;
            }
            step *= 0.5;
        }
        if (!accepted) break;
        layout = std::move(candidate);
        current_objective = candidate_objective;
        result.best_history.push_back(current_objective);
        result.iterations = iteration + 1;
    }
    result.algorithm_seconds = elapsed_seconds(algorithm_start);
    result.gradient_seconds = gradient_seconds;
    result.pattern_rebuilds = pattern_rebuilds;
    result.final_colors = final_colors;
    result.final_threshold = config.mode == GradientMode::sparse ? threshold : 0.0;
    result.initial_wake_loss_percent = 100.0
        * (1.0 - initial_objective / static_cast<double>(turbines));
    result.final_wake_loss_percent = 100.0
        * (1.0 - current_objective / static_cast<double>(turbines));
    result.wake_loss_reduction_points = result.initial_wake_loss_percent
        - result.final_wake_loss_percent;
    result.end_to_end_seconds = elapsed_seconds(total_start);
    result.observed_workers = executor.work_receipt().distinct_participants;
    result.final_layout = layout;
    std::uint64_t hash = 0x05810f710123ULL;
    for (const Point& point : layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_d));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_d));
    }
    for (const double value : result.best_history) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(value));
    }
    result.scientific_hash = hash;
    return result;
}

const char* gradient_mode_name(const GradientMode mode) noexcept {
    return mode == GradientMode::dense ? "dense" : "sparse";
}

}  // namespace core99::l0581
