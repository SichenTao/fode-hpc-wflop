/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0245 pure-C++ PC-R/rectangle evaluator and optimizer
Paper/DOI: Padrón et al.; 10.5194/wes-4-211-2019.
Public assets, source/code conflicts, completions, semantic identifiers,
HPC design and claim boundary:
hpc/core99_cpp/include/core99/padron_l0245.hpp.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/padron_l0245.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace core99::l0245 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kVariables = 2 * turbine_count;
constexpr int kBoundaryCount = 14;
constexpr int kPairConstraints = turbine_count * (turbine_count - 1) / 2;
constexpr int kTotalConstraints =
    kPairConstraints + turbine_count * kBoundaryCount;
constexpr int kDualWidth = 8;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kDiameterM = 126.4;
constexpr double kRotorRadiusM = 0.5 * kDiameterM;
constexpr double kMinimumSpacingM = 2.0 * kDiameterM;
constexpr double kAirDensity = 1.1716;
constexpr double kGeneratorEfficiency = 0.944;
constexpr double kHoursPerYear = 8760.0;
constexpr double kWeibullShape = 1.8;
constexpr double kWeibullScale = 12.55;
constexpr double kSpeedMinimum = 3.0;
constexpr double kSpeedMaximum = 25.0;
constexpr double kKd = 0.15;
constexpr double kInitialWakeDisplacementM = -4.5;
constexpr double kBd = -0.01;
constexpr double kKe = 0.065;
constexpr std::array<double, 3> kMe{-0.5, 0.22, 1.0};
constexpr std::array<double, 3> kMu{0.5, 1.0, 5.5};
constexpr double kAuDegrees = 5.0;
constexpr double kCosSpread = 2.0;
constexpr double kAxialInduction = 1.0 / 3.0;
constexpr double kPowerCoefficient =
    (0.7737 / kGeneratorEfficiency) * 4.0 * kAxialInduction
    * (1.0 - kAxialInduction) * (1.0 - kAxialInduction);
constexpr double kSplineShift = 0.0;
constexpr std::array<Point, kBoundaryCount> kBoundaryVertices{{
    {3710.176, 3569.028}, {1683.694, 4889.770},
    {1124.143, 4869.606}, {297.419, 4390.711},
    {20.164, 3911.816}, {0.000, 2948.985},
    {216.763, 1497.177}, {972.913, 10.082},
    {1552.628, 0.000}, {2157.548, 20.164},
    {3135.502, 579.715}, {3483.331, 1103.979},
    {3800.914, 1633.284}, {3780.750, 2611.238},
}};
constexpr std::array<Point, kBoundaryCount> kBoundaryNormals{{
    {0.54601347, 0.83777640}, {-0.03601266, 0.99935133},
    {-0.50124424, 0.86558500}, {-0.86542629, 0.50103627},
    {-0.99978078, 0.02093782}, {-0.98903688, -0.14766870},
    {-0.89138513, -0.45324668}, {-0.01738867, -0.99984881},
    {0.03331483, -0.99944491}, {0.49662068, -0.86796768},
    {0.83328090, -0.55284983}, {0.85749293, -0.51449576},
    {0.99978751, 0.02061418}, {0.99729632, 0.07348499},
}};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

template<int Width>
struct Dual {
    double value = 0.0;
    std::array<double, Width> derivative{};

    Dual() = default;
    Dual(const double scalar) : value(scalar) {}

    static Dual independent(const double scalar, const int lane) {
        Dual result(scalar);
        result.derivative[static_cast<std::size_t>(lane)] = 1.0;
        return result;
    }
};

template<int Width>
Dual<Width> operator+(const Dual<Width>& left, const Dual<Width>& right) {
    Dual<Width> result(left.value + right.value);
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)]
            + right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template<int Width>
Dual<Width> operator-(const Dual<Width>& left, const Dual<Width>& right) {
    Dual<Width> result(left.value - right.value);
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)]
            - right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template<int Width>
Dual<Width> operator-(const Dual<Width>& input) {
    Dual<Width> result(-input.value);
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            -input.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template<int Width>
Dual<Width> operator*(const Dual<Width>& left, const Dual<Width>& right) {
    Dual<Width> result(left.value * right.value);
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            left.derivative[static_cast<std::size_t>(lane)] * right.value
            + left.value * right.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template<int Width>
Dual<Width> operator/(const Dual<Width>& left, const Dual<Width>& right) {
    Dual<Width> result(left.value / right.value);
    const double denominator = right.value * right.value;
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] = (
            left.derivative[static_cast<std::size_t>(lane)] * right.value
            - left.value * right.derivative[static_cast<std::size_t>(lane)]
        ) / denominator;
    }
    return result;
}

template<int Width>
Dual<Width> operator+(const Dual<Width>& left, const double right) {
    return left + Dual<Width>(right);
}

template<int Width>
Dual<Width> operator+(const double left, const Dual<Width>& right) {
    return Dual<Width>(left) + right;
}

template<int Width>
Dual<Width> operator-(const Dual<Width>& left, const double right) {
    return left - Dual<Width>(right);
}

template<int Width>
Dual<Width> operator-(const double left, const Dual<Width>& right) {
    return Dual<Width>(left) - right;
}

template<int Width>
Dual<Width> operator*(const Dual<Width>& left, const double right) {
    return left * Dual<Width>(right);
}

template<int Width>
Dual<Width> operator*(const double left, const Dual<Width>& right) {
    return Dual<Width>(left) * right;
}

template<int Width>
Dual<Width> operator/(const Dual<Width>& left, const double right) {
    return left / Dual<Width>(right);
}

template<int Width>
Dual<Width> operator/(const double left, const Dual<Width>& right) {
    return Dual<Width>(left) / right;
}

template<int Width>
Dual<Width> sqrt(const Dual<Width>& input) {
    const double root = std::sqrt(std::max(0.0, input.value));
    Dual<Width> result(root);
    if (root <= 1.0e-15) return result;
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            0.5 * input.derivative[static_cast<std::size_t>(lane)] / root;
    }
    return result;
}

template<int Width>
Dual<Width> acos(const Dual<Width>& input) {
    const double bounded = std::clamp(input.value, -1.0, 1.0);
    Dual<Width> result(std::acos(bounded));
    const double denominator = std::sqrt(std::max(
        1.0e-30, 1.0 - bounded * bounded
    ));
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            -input.derivative[static_cast<std::size_t>(lane)] / denominator;
    }
    return result;
}

template<int Width>
Dual<Width> cos(const Dual<Width>& input) {
    Dual<Width> result(std::cos(input.value));
    const double multiplier = -std::sin(input.value);
    for (int lane = 0; lane < Width; ++lane) {
        result.derivative[static_cast<std::size_t>(lane)] =
            multiplier * input.derivative[static_cast<std::size_t>(lane)];
    }
    return result;
}

template<typename Scalar>
double numeric_value(const Scalar& value) {
    if constexpr (std::is_same_v<Scalar, double>) return value;
    else return value.value;
}

template<typename Scalar>
Scalar scalar(const double value) { return Scalar(value); }

template<>
double scalar<double>(const double value) { return value; }

template<typename Scalar>
Scalar absolute(const Scalar& value) {
    return numeric_value(value) < 0.0 ? -value : value;
}

template<typename Scalar>
Scalar bounded_acos(const Scalar& input) {
    const double value = numeric_value(input);
    if (value <= -1.0) return scalar<Scalar>(kPi);
    if (value >= 1.0) return scalar<Scalar>(0.0);
    if constexpr (std::is_same_v<Scalar, double>) return std::acos(input);
    else return acos(input);
}

template<typename Scalar>
Scalar cosine(const Scalar& input) {
    if constexpr (std::is_same_v<Scalar, double>) return std::cos(input);
    else return cos(input);
}

template<typename Scalar>
Scalar root(const Scalar& input) {
    if constexpr (std::is_same_v<Scalar, double>) {
        return std::sqrt(std::max(0.0, input));
    } else {
        return sqrt(input);
    }
}

template<typename Scalar>
Scalar hermite(
    const Scalar& x,
    const Scalar& x0,
    const Scalar& x1,
    const Scalar& y0,
    const double dy0,
    const Scalar& y1,
    const double dy1
) {
    const Scalar width = x1 - x0;
    const Scalar t = (x - x0) / width;
    const Scalar t2 = t * t;
    const Scalar t3 = t2 * t;
    const Scalar h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const Scalar h10 = t3 - 2.0 * t2 + t;
    const Scalar h01 = -2.0 * t3 + 3.0 * t2;
    const Scalar h11 = t3 - t2;
    return h00 * y0 + h10 * width * dy0
        + h01 * y1 + h11 * width * dy1;
}

template<typename Scalar>
Scalar wake_center(
    const Scalar& source_x,
    const Scalar& source_y,
    const Scalar& target_x
) {
    constexpr double p0 = -0.25;
    constexpr double p1 = 0.25 + kSplineShift;
    const Scalar x0 = source_x + p0 * kDiameterM;
    const Scalar x1 = source_x + p1 * kDiameterM;
    const Scalar base = source_y - kInitialWakeDisplacementM;
    if (numeric_value(target_x) > numeric_value(x1)) {
        return base + kBd * (target_x - source_x);
    }
    if (numeric_value(target_x) >= numeric_value(x0)) {
        const Scalar y1 = base + kBd * (x1 - source_x);
        return hermite(target_x, x0, x1, base, 0.0, y1, 0.0);
    }
    return base;
}

template<typename Scalar>
std::array<Scalar, 3> wake_diameters(
    const Scalar& source_x,
    const Scalar& target_x
) {
    constexpr double p_unity = -0.25 + kSplineShift;
    constexpr double p_near0 = -1.0 + kSplineShift;
    constexpr double p_near1 = 0.25 + kSplineShift;
    constexpr double p_mix0 = p_unity;
    constexpr double p_mix1 = 0.25 + kSplineShift;
    const Scalar dx = target_x - source_x;
    const Scalar x = target_x;
    const Scalar x1 = source_x + p_unity * kDiameterM;
    const Scalar y1 = kDiameterM
        + 2.0 * kKe * kMe[1] * p_unity * kDiameterM;
    const double dy1 = 2.0 * kKe * kMe[1];
    std::array<Scalar, 3> result{
        scalar<Scalar>(0.0), scalar<Scalar>(0.0), scalar<Scalar>(0.0)
    };

    const double zero_location = numeric_value(source_x)
        - kDiameterM / (2.0 * kKe * kMe[0]);
    if (zero_location + p_near1 * kDiameterM < numeric_value(x)) {
        result[0] = scalar<Scalar>(0.0);
    } else if (zero_location - p_near1 * kDiameterM < numeric_value(x)) {
        const Scalar x2 = scalar<Scalar>(
            zero_location - p_near1 * kDiameterM
        );
        const Scalar y2 = kDiameterM
            + 2.0 * kKe * kMe[0] * (x2 - source_x);
        const Scalar x3 = scalar<Scalar>(
            zero_location + p_near1 * kDiameterM
        );
        result[0] = hermite(x, x2, x3, y2, 2.0 * kKe * kMe[0],
                            scalar<Scalar>(0.0), 0.0);
    } else if (numeric_value(source_x + p_near1 * kDiameterM)
               < numeric_value(x)) {
        result[0] = kDiameterM + 2.0 * kKe * kMe[0] * dx;
    } else if (numeric_value(x) > numeric_value(x1)) {
        const Scalar x2 = source_x + p_near1 * kDiameterM;
        const Scalar y2 = kDiameterM
            + 2.0 * kKe * kMe[0] * (x2 - source_x);
        result[0] = hermite(x, x1, x2, y1, dy1, y2,
                            2.0 * kKe * kMe[0]);
    } else if (numeric_value(x) >= numeric_value(
                   source_x + p_near0 * kDiameterM
               ) && numeric_value(x) <= numeric_value(
                   source_x + kSplineShift * kDiameterM
               )) {
        const Scalar x0 = source_x + p_near0 * kDiameterM;
        result[0] = hermite(x, x0, x1, scalar<Scalar>(0.0), 0.0,
                            y1, dy1);
    }

    if (numeric_value(source_x + p_unity * kDiameterM)
        < numeric_value(x)) {
        result[1] = kDiameterM + 2.0 * kKe * kMe[1] * dx;
    } else {
        result[1] = result[0];
    }

    if (numeric_value(source_x + p_mix1 * kDiameterM)
        < numeric_value(x)) {
        result[2] = kDiameterM + 2.0 * kKe * kMe[2] * dx;
    } else if (numeric_value(x) > numeric_value(
                   source_x + p_mix0 * kDiameterM
               )) {
        const Scalar x2 = source_x + p_mix1 * kDiameterM;
        const Scalar y2 = kDiameterM
            + 2.0 * kKe * kMe[2] * p_mix1 * kDiameterM;
        result[2] = hermite(x, x1, x2, y1, dy1, y2,
                            2.0 * kKe * kMe[2]);
    } else {
        result[2] = result[0];
    }
    for (Scalar& diameter : result) {
        if (numeric_value(diameter) < 0.0) diameter = scalar<Scalar>(0.0);
    }
    return result;
}

template<typename Scalar>
Scalar circle_overlap_fraction(
    const Scalar& centre_distance,
    const Scalar& wake_diameter
) {
    const Scalar d = absolute(centre_distance);
    const Scalar r = 0.5 * wake_diameter;
    const double dn = numeric_value(d);
    const double rn = numeric_value(r);
    if (!(rn > 0.0) || dn >= rn + kRotorRadiusM) {
        return scalar<Scalar>(0.0);
    }
    if (dn <= std::abs(rn - kRotorRadiusM)) {
        const Scalar radius = rn < kRotorRadiusM
            ? r : scalar<Scalar>(kRotorRadiusM);
        return radius * radius / (kRotorRadiusM * kRotorRadiusM);
    }
    const Scalar d_safe = root(d * d + scalar<Scalar>(1.0e-24));
    const Scalar wake_cosine = (
        d_safe * d_safe + r * r - kRotorRadiusM * kRotorRadiusM
    ) / (2.0 * d_safe * r);
    const Scalar rotor_cosine = (
        d_safe * d_safe + kRotorRadiusM * kRotorRadiusM - r * r
    ) / (2.0 * d_safe * kRotorRadiusM);
    const Scalar product =
        (-d_safe + r + kRotorRadiusM)
        * (d_safe + r - kRotorRadiusM)
        * (d_safe - r + kRotorRadiusM)
        * (d_safe + r + kRotorRadiusM);
    const Scalar area = r * r * bounded_acos(wake_cosine)
        + kRotorRadiusM * kRotorRadiusM * bounded_acos(rotor_cosine)
        - 0.5 * root(product);
    return area / (kPi * kRotorRadiusM * kRotorRadiusM);
}

struct Scenario {
    double direction_degrees = 0.0;
    double speed_mps = 0.0;
    double direct_weight = 0.0;
    double direction_coordinate = 0.0;
    double speed_coordinate = 0.0;
};

struct PolynomialRecurrence {
    std::vector<double> alpha;
    std::vector<double> beta;

    [[nodiscard]] std::vector<double> evaluate(
        const double x, const int maximum_degree
    ) const {
        std::vector<double> values(
            static_cast<std::size_t>(maximum_degree + 1), 0.0
        );
        values[0] = 1.0;
        if (maximum_degree == 0) return values;
        values[1] = (x - alpha[0]) / beta[1];
        for (int degree = 1; degree < maximum_degree; ++degree) {
            values[static_cast<std::size_t>(degree + 1)] = (
                (x - alpha[static_cast<std::size_t>(degree)])
                    * values[static_cast<std::size_t>(degree)]
                - beta[static_cast<std::size_t>(degree)]
                    * values[static_cast<std::size_t>(degree - 1)]
            ) / beta[static_cast<std::size_t>(degree + 1)];
        }
        return values;
    }
};

struct SamplePlan {
    std::vector<Scenario> scenarios;
    std::vector<double> weights;
    int selected_degree = 0;
    double regression_seconds = 0.0;
};

struct StateOutput {
    double power_mw = 0.0;
    std::array<double, kVariables> gradient_mw_per_m{};
};

struct StateBatch {
    std::vector<StateOutput> states;
    int observed_workers = 0;
    double seconds = 0.0;
};

double weibull_cdf_raw(const double speed) {
    if (!(speed > 0.0)) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / kWeibullScale, kWeibullShape));
}

double truncated_weibull_cdf(const double speed) {
    if (speed <= kSpeedMinimum) return 0.0;
    if (speed >= kSpeedMaximum) return 1.0;
    const double low = weibull_cdf_raw(kSpeedMinimum);
    const double high = weibull_cdf_raw(kSpeedMaximum);
    return (weibull_cdf_raw(speed) - low) / (high - low);
}

double inverse_truncated_weibull(const double probability) {
    const double low = weibull_cdf_raw(kSpeedMinimum);
    const double high = weibull_cdf_raw(kSpeedMaximum);
    const double target = low + std::clamp(probability, 0.0, 1.0) * (high - low);
    return kWeibullScale * std::pow(-std::log1p(-target), 1.0 / kWeibullShape);
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t quantized(const double value, const double scale = 1.0e7) {
    const auto integer = static_cast<std::int64_t>(std::llround(value * scale));
    return std::bit_cast<std::uint64_t>(integer);
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

struct Problem::Impl {
    std::array<std::vector<Point>, 4> layouts;
    std::array<double, 72> direction_degrees{};
    std::array<double, 72> direction_probability{};
    PolynomialRecurrence direction_polynomial;
    PolynomialRecurrence speed_polynomial;

    explicit Impl(const std::string& data_path) {
        load_public_data(data_path);
        direction_polynomial = make_direction_recurrence(19);
        speed_polynomial = make_speed_recurrence(19);
    }

    void load_public_data(const std::string& data_path) {
        std::ifstream stream(data_path);
        if (!stream) throw std::runtime_error("cannot open L0245 public data");
        std::string section;
        std::string line;
        int wind_index = 0;
        while (std::getline(stream, line)) {
            if (line.empty() || line.front() == '#') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2U);
                continue;
            }
            std::istringstream row(line);
            if (section.rfind("layout_", 0) == 0) {
                Point point;
                if (!(row >> point.x_m >> point.y_m)) {
                    throw std::runtime_error("invalid L0245 layout row");
                }
                const LayoutId id = parse_layout(section.substr(7));
                layouts[static_cast<std::size_t>(id)].push_back(point);
            } else if (section == "wind_direction_distribution") {
                if (wind_index >= 72 || !(row
                        >> direction_degrees[static_cast<std::size_t>(wind_index)]
                        >> direction_probability[static_cast<std::size_t>(wind_index)])) {
                    throw std::runtime_error("invalid L0245 direction row");
                }
                ++wind_index;
            }
        }
        for (const auto& layout : layouts) {
            if (layout.size() != turbine_count) {
                throw std::runtime_error("L0245 layout cardinality differs");
            }
        }
        if (wind_index != 72) {
            throw std::runtime_error("L0245 direction cardinality differs");
        }
        const double mass = std::accumulate(
            direction_probability.begin(), direction_probability.end(), 0.0
        );
        if (std::abs(mass - 1.0) > 1.0e-10) {
            throw std::runtime_error("L0245 direction mass differs");
        }
    }

    [[nodiscard]] PolynomialRecurrence recurrence(
        const std::vector<double>& points,
        const std::vector<double>& weights,
        const int maximum_degree
    ) const {
        PolynomialRecurrence result;
        result.alpha.resize(static_cast<std::size_t>(maximum_degree), 0.0);
        result.beta.resize(static_cast<std::size_t>(maximum_degree + 1), 0.0);
        std::vector<double> previous(points.size(), 0.0);
        std::vector<double> current(points.size(), 1.0);
        for (int degree = 0; degree < maximum_degree; ++degree) {
            double alpha = 0.0;
            for (std::size_t index = 0; index < points.size(); ++index) {
                alpha += weights[index] * points[index]
                    * current[index] * current[index];
            }
            result.alpha[static_cast<std::size_t>(degree)] = alpha;
            std::vector<double> next(points.size(), 0.0);
            double norm = 0.0;
            for (std::size_t index = 0; index < points.size(); ++index) {
                next[index] = (points[index] - alpha) * current[index]
                    - result.beta[static_cast<std::size_t>(degree)]
                        * previous[index];
                norm += weights[index] * next[index] * next[index];
            }
            const double beta = std::sqrt(std::max(norm, 1.0e-28));
            result.beta[static_cast<std::size_t>(degree + 1)] = beta;
            for (double& value : next) value /= beta;
            previous = std::move(current);
            current = std::move(next);
        }
        return result;
    }

    [[nodiscard]] PolynomialRecurrence make_direction_recurrence(
        const int maximum_degree
    ) const {
        // Section 3.2 of the paper first linearly interpolates the measured
        // five-degree probabilities, then represents the result with 50
        // equal-width histogram bins when constructing custom polynomials.
        constexpr int bins = 50;
        constexpr double width_degrees = 360.0 / bins;
        std::vector<double> points(bins);
        std::vector<double> weights(bins);
        for (int index = 0; index < bins; ++index) {
            const double left = index * width_degrees;
            const double right = left + width_degrees;
            const double midpoint = 0.5 * (left + right);
            points[static_cast<std::size_t>(index)] =
                (midpoint - 180.0) / 180.0;
            weights[static_cast<std::size_t>(index)] =
                direction_probability_between(left, right);
        }
        return recurrence(points, weights, maximum_degree);
    }

    [[nodiscard]] PolynomialRecurrence make_speed_recurrence(
        const int maximum_degree
    ) const {
        constexpr int bins = 4096;
        std::vector<double> points(static_cast<std::size_t>(bins));
        std::vector<double> weights(static_cast<std::size_t>(bins), 1.0 / bins);
        for (int index = 0; index < bins; ++index) {
            const double q = (static_cast<double>(index) + 0.5) / bins;
            const double speed = inverse_truncated_weibull(q);
            points[static_cast<std::size_t>(index)] =
                2.0 * (speed - kSpeedMinimum)
                    / (kSpeedMaximum - kSpeedMinimum) - 1.0;
        }
        return recurrence(points, weights, maximum_degree);
    }

    [[nodiscard]] double direction_cdf(const double direction_degrees_value) const {
        if (direction_degrees_value <= 0.0) return 0.0;
        if (direction_degrees_value >= 360.0) return 1.0;
        const int interval = std::min(
            71, static_cast<int>(direction_degrees_value / 5.0)
        );
        double cumulative = 0.0;
        for (int index = 0; index < interval; ++index) {
            const int next = (index + 1) % 72;
            cumulative += 0.5 * (
                direction_probability[static_cast<std::size_t>(index)]
                + direction_probability[static_cast<std::size_t>(next)]
            );
        }
        const double offset = direction_degrees_value - 5.0 * interval;
        const double left =
            direction_probability[static_cast<std::size_t>(interval)];
        const double right = direction_probability[
            static_cast<std::size_t>((interval + 1) % 72)
        ];
        cumulative += left * offset / 5.0
            + (right - left) * offset * offset / 50.0;
        return std::clamp(cumulative, 0.0, 1.0);
    }

    [[nodiscard]] double direction_probability_between(
        const double left_degrees, const double right_degrees
    ) const {
        const auto wrap = [](double value) {
            value = std::fmod(value, 360.0);
            if (value < 0.0) value += 360.0;
            return value;
        };
        const double left = wrap(left_degrees);
        const double right = wrap(right_degrees);
        if (right_degrees - left_degrees >= 360.0) return 1.0;
        if (right > left) return direction_cdf(right) - direction_cdf(left);
        return 1.0 - direction_cdf(left) + direction_cdf(right);
    }

    [[nodiscard]] double inverse_direction(const double probability) const {
        const double target = std::clamp(probability, 0.0, 1.0 - 1.0e-15);
        double cumulative = 0.0;
        for (int index = 0; index < 72; ++index) {
            const int next = (index + 1) % 72;
            const double interval_mass = 0.5 * (
                direction_probability[static_cast<std::size_t>(index)]
                + direction_probability[static_cast<std::size_t>(next)]
            );
            if (target <= cumulative + interval_mass || index == 71) {
                double low = 0.0;
                double high = 5.0;
                for (int iteration = 0; iteration < 48; ++iteration) {
                    const double offset = 0.5 * (low + high);
                    const double left = direction_probability[
                        static_cast<std::size_t>(index)
                    ];
                    const double right = direction_probability[
                        static_cast<std::size_t>(next)
                    ];
                    const double partial = left * offset / 5.0
                        + (right - left) * offset * offset / 50.0;
                    if (cumulative + partial < target) low = offset;
                    else high = offset;
                }
                double direction = 5.0 * index + 0.5 * (low + high);
                if (direction >= 360.0) direction -= 360.0;
                return direction;
            }
            cumulative += interval_mass;
        }
        return 0.0;
    }

    [[nodiscard]] std::vector<Scenario> pcr_scenarios(
        const int count, const std::uint64_t seed
    ) const {
        std::mt19937_64 generator(seed ^ 0x4c30323435504352ULL);
        std::vector<int> direction_order(static_cast<std::size_t>(count));
        std::vector<int> speed_order(static_cast<std::size_t>(count));
        std::iota(direction_order.begin(), direction_order.end(), 0);
        std::iota(speed_order.begin(), speed_order.end(), 0);
        std::shuffle(direction_order.begin(), direction_order.end(), generator);
        std::shuffle(speed_order.begin(), speed_order.end(), generator);
        std::vector<Scenario> result;
        result.reserve(static_cast<std::size_t>(count));
        for (int sample = 0; sample < count; ++sample) {
            const double qd = (
                direction_order[static_cast<std::size_t>(sample)] + 0.5
            ) / count;
            const double qs = (
                speed_order[static_cast<std::size_t>(sample)] + 0.5
            ) / count;
            const double direction = inverse_direction(qd);
            const double speed = inverse_truncated_weibull(qs);
            result.push_back({
                direction, speed, 0.0,
                (direction - 180.0) / 180.0,
                2.0 * (speed - kSpeedMinimum)
                    / (kSpeedMaximum - kSpeedMinimum) - 1.0,
            });
        }
        return result;
    }

    [[nodiscard]] std::vector<Scenario> monte_carlo_scenarios(
        const int count, const std::uint64_t seed
    ) const {
        std::mt19937_64 generator(seed ^ 0x4c303234354d4352ULL);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        std::vector<Scenario> result(static_cast<std::size_t>(count));
        for (Scenario& scenario : result) {
            const double direction_probability = uniform(generator);
            const double speed_probability = uniform(generator);
            scenario.direction_degrees = inverse_direction(direction_probability);
            scenario.speed_mps = inverse_truncated_weibull(speed_probability);
            scenario.direct_weight = 1.0 / static_cast<double>(count);
            scenario.direction_coordinate =
                (scenario.direction_degrees - 180.0) / 180.0;
            scenario.speed_coordinate = 2.0
                * (scenario.speed_mps - kSpeedMinimum)
                / (kSpeedMaximum - kSpeedMinimum) - 1.0;
        }
        return result;
    }

    [[nodiscard]] std::vector<Scenario> rectangle_scenarios(
        const int points, const std::uint64_t seed
    ) const {
        const double direction_offset =
            static_cast<double>(seed % 10ULL) * 360.0 / (10.0 * points);
        std::vector<double> direction_weights(
            static_cast<std::size_t>(points), 0.0
        );
        std::vector<double> direction_points(static_cast<std::size_t>(points));
        for (int index = 0; index < points; ++index) {
            double value = direction_offset
                + (static_cast<double>(index) + 0.5) * 360.0 / points;
            while (value >= 360.0) value -= 360.0;
            direction_points[static_cast<std::size_t>(index)] = value;
        }
        const double direction_width = 360.0 / points;
        for (int index = 0; index < points; ++index) {
            const double midpoint =
                direction_points[static_cast<std::size_t>(index)];
            direction_weights[static_cast<std::size_t>(index)] =
                direction_probability_between(
                    midpoint - 0.5 * direction_width,
                    midpoint + 0.5 * direction_width
                );
        }
        std::vector<double> speed_points(static_cast<std::size_t>(points));
        std::vector<double> speed_weights(static_cast<std::size_t>(points));
        const double width = (kSpeedMaximum - kSpeedMinimum) / points;
        for (int index = 0; index < points; ++index) {
            const double left = kSpeedMinimum + index * width;
            const double right = left + width;
            speed_points[static_cast<std::size_t>(index)] = 0.5 * (left + right);
            speed_weights[static_cast<std::size_t>(index)] =
                truncated_weibull_cdf(right) - truncated_weibull_cdf(left);
        }
        std::vector<Scenario> result;
        result.reserve(static_cast<std::size_t>(points * points));
        for (int direction = 0; direction < points; ++direction) {
            for (int speed = 0; speed < points; ++speed) {
                const double d = direction_points[static_cast<std::size_t>(direction)];
                const double s = speed_points[static_cast<std::size_t>(speed)];
                result.push_back({
                    d, s,
                    direction_weights[static_cast<std::size_t>(direction)]
                        * speed_weights[static_cast<std::size_t>(speed)],
                    (d - 180.0) / 180.0,
                    2.0 * (s - kSpeedMinimum)
                        / (kSpeedMaximum - kSpeedMinimum) - 1.0,
                });
            }
        }
        return result;
    }

    template<typename Scalar>
    [[nodiscard]] Scalar turbine_power_mw(const Scalar& speed) const {
        const double value = numeric_value(speed);
        if (value < kSpeedMinimum || value > kSpeedMaximum) {
            return scalar<Scalar>(0.0);
        }
        const Scalar raw = 0.5 * kAirDensity
            * (kPi * kRotorRadiusM * kRotorRadiusM)
            * kPowerCoefficient * speed * speed * speed
            * kGeneratorEfficiency / 1.0e6;
        if (numeric_value(raw) >= 5.0) return scalar<Scalar>(5.0);
        return raw;
    }

    template<typename Scalar>
    [[nodiscard]] Scalar state_power(
        const std::vector<Scalar>& coordinates,
        const Scenario& scenario
    ) const {
        if (coordinates.size() != kVariables) {
            throw std::invalid_argument("L0245 coordinate cardinality differs");
        }
        double direction = 270.0 - scenario.direction_degrees;
        if (direction < 0.0) direction += 360.0;
        const double angle = -direction * kPi / 180.0;
        const double cosine_angle = std::cos(angle);
        const double sine_angle = std::sin(angle);
        std::vector<Scalar> along(static_cast<std::size_t>(turbine_count));
        std::vector<Scalar> across(static_cast<std::size_t>(turbine_count));
        for (int turbine = 0; turbine < turbine_count; ++turbine) {
            const Scalar& x = coordinates[static_cast<std::size_t>(turbine)];
            const Scalar& y = coordinates[
                static_cast<std::size_t>(turbine_count + turbine)
            ];
            along[static_cast<std::size_t>(turbine)] =
                x * cosine_angle - y * sine_angle;
            across[static_cast<std::size_t>(turbine)] =
                x * sine_angle + y * cosine_angle;
        }
        const double mu_yaw_factor = std::cos(kAuDegrees * kPi / 180.0);
        Scalar farm_power = scalar<Scalar>(0.0);
        for (int target = 0; target < turbine_count; ++target) {
            Scalar squared_wake = scalar<Scalar>(0.0);
            for (int source = 0; source < turbine_count; ++source) {
                if (source == target) continue;
                const Scalar dx = along[static_cast<std::size_t>(target)]
                    - along[static_cast<std::size_t>(source)];
                if (!(numeric_value(dx) > (-1.0 + kSplineShift) * kDiameterM)) {
                    continue;
                }
                const auto diameters = wake_diameters(
                    along[static_cast<std::size_t>(source)],
                    along[static_cast<std::size_t>(target)]
                );
                const Scalar centre = wake_center(
                    along[static_cast<std::size_t>(source)],
                    across[static_cast<std::size_t>(source)],
                    along[static_cast<std::size_t>(target)]
                );
                const Scalar lateral = centre
                    - across[static_cast<std::size_t>(target)];
                Scalar per_zone = scalar<Scalar>(0.0);
                Scalar inner_area = scalar<Scalar>(0.0);
                for (int zone = 0; zone < 3; ++zone) {
                    const Scalar cumulative = circle_overlap_fraction(
                        lateral, diameters[static_cast<std::size_t>(zone)]
                    );
                    Scalar annulus = cumulative - inner_area;
                    if (numeric_value(annulus) < 0.0) {
                        annulus = scalar<Scalar>(0.0);
                    }
                    inner_area = cumulative;
                    const Scalar rmax = 0.5 * kCosSpread * (
                        diameters[2] + kDiameterM
                    );
                    Scalar cosine_factor = scalar<Scalar>(1.0);
                    if (numeric_value(rmax) > 1.0e-12) {
                        cosine_factor = 0.5 * (
                            1.0 + cosine(kPi * absolute(lateral) / rmax)
                        );
                    }
                    const double adjusted_mu =
                        kMu[static_cast<std::size_t>(zone)] / mu_yaw_factor;
                    const Scalar denominator = kDiameterM
                        + 2.0 * kKe * adjusted_mu * dx;
                    if (numeric_value(denominator) <= 1.0e-12) continue;
                    const Scalar coefficient =
                        kDiameterM / denominator * cosine_factor;
                    per_zone = per_zone + coefficient * coefficient * annulus;
                }
                const Scalar wake = kAxialInduction * per_zone;
                squared_wake = squared_wake + wake * wake;
            }
            Scalar speed = scenario.speed_mps * (
                1.0 - 2.0 * root(squared_wake)
            );
            if (numeric_value(speed) < 0.0) speed = scalar<Scalar>(0.0);
            farm_power = farm_power + turbine_power_mw(speed);
        }
        return farm_power;
    }

    [[nodiscard]] StateOutput evaluate_state(
        const std::vector<Point>& layout,
        const Scenario& scenario,
        const bool gradient
    ) const {
        std::vector<double> flat(static_cast<std::size_t>(kVariables));
        for (int turbine = 0; turbine < turbine_count; ++turbine) {
            flat[static_cast<std::size_t>(turbine)] =
                layout[static_cast<std::size_t>(turbine)].x_m;
            flat[static_cast<std::size_t>(turbine_count + turbine)] =
                layout[static_cast<std::size_t>(turbine)].y_m;
        }
        StateOutput result;
        result.power_mw = state_power(flat, scenario);
        if (!gradient) return result;
        using Block = Dual<kDualWidth>;
        for (int base = 0; base < kVariables; base += kDualWidth) {
            std::vector<Block> coordinates;
            coordinates.reserve(static_cast<std::size_t>(kVariables));
            for (int variable = 0; variable < kVariables; ++variable) {
                const int lane = variable - base;
                if (lane >= 0 && lane < kDualWidth) {
                    coordinates.push_back(Block::independent(
                        flat[static_cast<std::size_t>(variable)], lane
                    ));
                } else {
                    coordinates.emplace_back(flat[static_cast<std::size_t>(variable)]);
                }
            }
            const Block power = state_power(coordinates, scenario);
            for (int lane = 0; lane < kDualWidth; ++lane) {
                const int variable = base + lane;
                if (variable >= kVariables) break;
                result.gradient_mw_per_m[static_cast<std::size_t>(variable)] =
                    power.derivative[static_cast<std::size_t>(lane)];
            }
        }
        return result;
    }

    [[nodiscard]] StateBatch evaluate_states(
        const std::vector<Point>& layout,
        const std::vector<Scenario>& scenarios,
        const bool gradient,
        fode::PersistentExecutor& executor
    ) const {
        StateBatch result;
        result.states.resize(scenarios.size());
        executor.reset_work_receipt();
        const auto started = Clock::now();
        executor.parallel_for(0, static_cast<int>(scenarios.size()), [&](const int index) {
            result.states[static_cast<std::size_t>(index)] = evaluate_state(
                layout, scenarios[static_cast<std::size_t>(index)], gradient
            );
        });
        result.seconds = elapsed_seconds(started);
        result.observed_workers = executor.work_receipt().distinct_participants;
        return result;
    }

    [[nodiscard]] std::vector<std::array<int, 2>> terms(
        const int maximum_degree
    ) const {
        std::vector<std::array<int, 2>> result;
        for (int degree = 0; degree <= maximum_degree; ++degree) {
            for (int first = 0; first <= degree; ++first) {
                result.push_back({first, degree - first});
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<double> basis_matrix(
        const std::vector<Scenario>& scenarios,
        const int maximum_degree
    ) const {
        const auto multi = terms(maximum_degree);
        std::vector<double> matrix(
            scenarios.size() * multi.size(), 0.0
        );
        for (std::size_t row = 0; row < scenarios.size(); ++row) {
            const auto direction = direction_polynomial.evaluate(
                scenarios[row].direction_coordinate, maximum_degree
            );
            const auto speed = speed_polynomial.evaluate(
                scenarios[row].speed_coordinate, maximum_degree
            );
            for (std::size_t column = 0; column < multi.size(); ++column) {
                matrix[row * multi.size() + column] =
                    direction[static_cast<std::size_t>(multi[column][0])]
                    * speed[static_cast<std::size_t>(multi[column][1])];
            }
        }
        return matrix;
    }

    [[nodiscard]] std::vector<double> solve_spd(
        std::vector<double> matrix,
        std::vector<double> rhs,
        const int size
    ) const {
        double diagonal_scale = 0.0;
        for (int index = 0; index < size; ++index) {
            diagonal_scale += matrix[
                static_cast<std::size_t>(index * size + index)
            ];
        }
        const double ridge = std::max(
            1.0e-12, 1.0e-10 * diagonal_scale / std::max(1, size)
        );
        for (int index = 0; index < size; ++index) {
            matrix[static_cast<std::size_t>(index * size + index)] += ridge;
        }
        for (int row = 0; row < size; ++row) {
            for (int column = 0; column <= row; ++column) {
                double value = matrix[
                    static_cast<std::size_t>(row * size + column)
                ];
                for (int inner = 0; inner < column; ++inner) {
                    value -= matrix[
                        static_cast<std::size_t>(row * size + inner)
                    ] * matrix[
                        static_cast<std::size_t>(column * size + inner)
                    ];
                }
                if (row == column) {
                    matrix[static_cast<std::size_t>(row * size + column)] =
                        std::sqrt(std::max(value, 1.0e-24));
                } else {
                    matrix[static_cast<std::size_t>(row * size + column)] =
                        value / matrix[
                            static_cast<std::size_t>(column * size + column)
                        ];
                }
            }
        }
        for (int row = 0; row < size; ++row) {
            for (int column = 0; column < row; ++column) {
                rhs[static_cast<std::size_t>(row)] -= matrix[
                    static_cast<std::size_t>(row * size + column)
                ] * rhs[static_cast<std::size_t>(column)];
            }
            rhs[static_cast<std::size_t>(row)] /= matrix[
                static_cast<std::size_t>(row * size + row)
            ];
        }
        for (int row = size - 1; row >= 0; --row) {
            for (int column = row + 1; column < size; ++column) {
                rhs[static_cast<std::size_t>(row)] -= matrix[
                    static_cast<std::size_t>(column * size + row)
                ] * rhs[static_cast<std::size_t>(column)];
            }
            rhs[static_cast<std::size_t>(row)] /= matrix[
                static_cast<std::size_t>(row * size + row)
            ];
        }
        return rhs;
    }

    [[nodiscard]] int select_degree(
        const std::vector<double>& basis,
        const int maximum_degree,
        const std::vector<StateOutput>& states
    ) const {
        const auto all_terms = terms(maximum_degree);
        const int stride = static_cast<int>(all_terms.size());
        const int samples = static_cast<int>(states.size());
        int selected = 1;
        double best_error = std::numeric_limits<double>::infinity();
        for (int degree = 1; degree <= maximum_degree; ++degree) {
            const int columns = (degree + 1) * (degree + 2) / 2;
            double error = 0.0;
            int observations = 0;
            for (int fold = 0; fold < 10; ++fold) {
                std::vector<double> gram(
                    static_cast<std::size_t>(columns * columns), 0.0
                );
                std::vector<double> rhs(static_cast<std::size_t>(columns), 0.0);
                for (int row = 0; row < samples; ++row) {
                    if (row % 10 == fold) continue;
                    const double* values = basis.data()
                        + static_cast<std::size_t>(row * stride);
                    for (int left = 0; left < columns; ++left) {
                        rhs[static_cast<std::size_t>(left)] +=
                            values[left] * states[static_cast<std::size_t>(row)].power_mw;
                        for (int right = 0; right <= left; ++right) {
                            gram[static_cast<std::size_t>(left * columns + right)] +=
                                values[left] * values[right];
                        }
                    }
                }
                for (int left = 0; left < columns; ++left) {
                    for (int right = 0; right < left; ++right) {
                        gram[static_cast<std::size_t>(right * columns + left)] =
                            gram[static_cast<std::size_t>(left * columns + right)];
                    }
                }
                const auto coefficients = solve_spd(
                    std::move(gram), std::move(rhs), columns
                );
                for (int row = fold; row < samples; row += 10) {
                    const double* values = basis.data()
                        + static_cast<std::size_t>(row * stride);
                    double prediction = 0.0;
                    for (int column = 0; column < columns; ++column) {
                        prediction += values[column]
                            * coefficients[static_cast<std::size_t>(column)];
                    }
                    const double difference = prediction
                        - states[static_cast<std::size_t>(row)].power_mw;
                    error += difference * difference;
                    ++observations;
                }
            }
            error /= std::max(1, observations);
            if (error < best_error) {
                best_error = error;
                selected = degree;
            }
        }
        return selected;
    }

    [[nodiscard]] std::vector<double> regression_weights(
        const std::vector<double>& basis,
        const int maximum_degree,
        const int selected_degree,
        const int samples
    ) const {
        const int stride = (maximum_degree + 1) * (maximum_degree + 2) / 2;
        const int columns =
            (selected_degree + 1) * (selected_degree + 2) / 2;
        std::vector<double> gram(
            static_cast<std::size_t>(columns * columns), 0.0
        );
        for (int row = 0; row < samples; ++row) {
            const double* values = basis.data()
                + static_cast<std::size_t>(row * stride);
            for (int left = 0; left < columns; ++left) {
                for (int right = 0; right <= left; ++right) {
                    gram[static_cast<std::size_t>(left * columns + right)] +=
                        values[left] * values[right];
                }
            }
        }
        for (int left = 0; left < columns; ++left) {
            for (int right = 0; right < left; ++right) {
                gram[static_cast<std::size_t>(right * columns + left)] =
                    gram[static_cast<std::size_t>(left * columns + right)];
            }
        }
        std::vector<double> unit(static_cast<std::size_t>(columns), 0.0);
        unit[0] = 1.0;
        const auto dual = solve_spd(std::move(gram), std::move(unit), columns);
        std::vector<double> weights(static_cast<std::size_t>(samples), 0.0);
        for (int row = 0; row < samples; ++row) {
            const double* values = basis.data()
                + static_cast<std::size_t>(row * stride);
            for (int column = 0; column < columns; ++column) {
                weights[static_cast<std::size_t>(row)] +=
                    values[column] * dual[static_cast<std::size_t>(column)];
            }
        }
        const double mass = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (std::abs(mass) < 1.0e-12) {
            throw std::runtime_error("L0245 regression weights have zero mass");
        }
        for (double& weight : weights) weight /= mass;
        return weights;
    }

    [[nodiscard]] SamplePlan make_plan(
        const std::vector<Point>& start,
        const MethodId method_id,
        const std::uint64_t seed,
        fode::PersistentExecutor& executor
    ) const {
        const MethodSpec spec = method(method_id);
        SamplePlan plan;
        if (method_id == MethodId::monte_carlo_reference) {
            plan.scenarios = monte_carlo_scenarios(
                spec.physical_wind_states, seed
            );
            plan.weights.assign(
                plan.scenarios.size(),
                1.0 / static_cast<double>(plan.scenarios.size())
            );
        } else if (spec.maximum_polynomial_degree > 0) {
            plan.scenarios = pcr_scenarios(spec.physical_wind_states, seed);
            const StateBatch states = evaluate_states(
                start, plan.scenarios, false, executor
            );
            const auto regression_started = Clock::now();
            const auto basis = basis_matrix(
                plan.scenarios, spec.maximum_polynomial_degree
            );
            plan.selected_degree = select_degree(
                basis, spec.maximum_polynomial_degree, states.states
            );
            plan.weights = regression_weights(
                basis, spec.maximum_polynomial_degree, plan.selected_degree,
                static_cast<int>(plan.scenarios.size())
            );
            plan.regression_seconds = elapsed_seconds(regression_started);
        } else {
            plan.scenarios = rectangle_scenarios(
                spec.rectangle_points_per_dimension, seed
            );
            plan.weights.reserve(plan.scenarios.size());
            for (const Scenario& scenario : plan.scenarios) {
                plan.weights.push_back(scenario.direct_weight);
            }
        }
        return plan;
    }

    [[nodiscard]] Evaluation aggregate(
        const std::vector<Point>& layout,
        const SamplePlan& plan,
        const StateBatch& batch,
        const bool gradient,
        const int requested_workers
    ) const {
        Evaluation result;
        result.selected_polynomial_degree = plan.selected_degree;
        result.physical_wake_simulations = static_cast<int>(plan.scenarios.size());
        result.requested_workers = requested_workers;
        result.observed_workers = batch.observed_workers;
        result.scenario_seconds = batch.seconds;
        result.regression_seconds = plan.regression_seconds;
        if (gradient) {
            result.gradient_gwh_per_m.assign(
                static_cast<std::size_t>(kVariables), 0.0
            );
        }
        for (std::size_t state = 0; state < batch.states.size(); ++state) {
            result.expected_power_mw += plan.weights[state]
                * batch.states[state].power_mw;
            if (gradient) {
                for (int variable = 0; variable < kVariables; ++variable) {
                    result.gradient_gwh_per_m[
                        static_cast<std::size_t>(variable)
                    ] += plan.weights[state] * batch.states[state].gradient_mw_per_m[
                        static_cast<std::size_t>(variable)
                    ] * kHoursPerYear / 1000.0;
                }
            }
        }
        result.aep_gwh = result.expected_power_mw * kHoursPerYear / 1000.0;
        geometry(layout, result);
        return result;
    }

    void geometry(const std::vector<Point>& layout, Evaluation& result) const {
        result.minimum_spacing_margin_m = std::numeric_limits<double>::infinity();
        result.maximum_boundary_violation_m = 0.0;
        for (int first = 0; first < turbine_count; ++first) {
            for (int second = first + 1; second < turbine_count; ++second) {
                const double separation = std::hypot(
                    layout[static_cast<std::size_t>(first)].x_m
                        - layout[static_cast<std::size_t>(second)].x_m,
                    layout[static_cast<std::size_t>(first)].y_m
                        - layout[static_cast<std::size_t>(second)].y_m
                );
                result.minimum_spacing_margin_m = std::min(
                    result.minimum_spacing_margin_m,
                    separation - kMinimumSpacingM
                );
            }
            const Point& point = layout[static_cast<std::size_t>(first)];
            for (int edge = 0; edge < kBoundaryCount; ++edge) {
                const double inside_distance =
                    (kBoundaryVertices[static_cast<std::size_t>(edge)].x_m
                        - point.x_m)
                        * kBoundaryNormals[static_cast<std::size_t>(edge)].x_m
                    + (kBoundaryVertices[static_cast<std::size_t>(edge)].y_m
                        - point.y_m)
                        * kBoundaryNormals[static_cast<std::size_t>(edge)].y_m;
                result.maximum_boundary_violation_m = std::max(
                    result.maximum_boundary_violation_m,
                    std::max(0.0, -inside_distance)
                );
            }
        }
        result.feasible = result.minimum_spacing_margin_m >= -1.0e-5
            && result.maximum_boundary_violation_m <= 1.0e-5;
    }

    [[nodiscard]] Evaluation evaluate_with_plan(
        const std::vector<Point>& layout,
        const SamplePlan& plan,
        const bool gradient,
        fode::PersistentExecutor& executor
    ) const {
        const StateBatch batch = evaluate_states(
            layout, plan.scenarios, gradient, executor
        );
        return aggregate(
            layout, plan, batch, gradient, executor.thread_count()
        );
    }

    [[nodiscard]] MethodSpec method(const MethodId id) const {
        switch (id) {
            case MethodId::pcr_coarse:
                return {id, "PC-R-coarse", 231, 11, 0};
            case MethodId::pcr_fine:
                return {id, "PC-R-fine", 630, 19, 0};
            case MethodId::rectangle_coarse:
                return {id, "Rect-coarse", 225, 0, 15};
            case MethodId::rectangle_fine:
                return {id, "Rect-fine", 625, 0, 25};
            case MethodId::monte_carlo_reference:
                return {id, "MC-reference", 200000, 0, 0};
        }
        throw std::invalid_argument("unknown L0245 method");
    }
};

namespace {

std::vector<double> flatten(const std::vector<Point>& layout) {
    std::vector<double> result(static_cast<std::size_t>(kVariables));
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        result[static_cast<std::size_t>(turbine)] =
            layout[static_cast<std::size_t>(turbine)].x_m;
        result[static_cast<std::size_t>(turbine_count + turbine)] =
            layout[static_cast<std::size_t>(turbine)].y_m;
    }
    return result;
}

std::vector<Point> unflatten(const double* values) {
    std::vector<Point> result(static_cast<std::size_t>(turbine_count));
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        result[static_cast<std::size_t>(turbine)] = {
            values[static_cast<std::size_t>(turbine)],
            values[static_cast<std::size_t>(turbine_count + turbine)],
        };
    }
    return result;
}

struct ObjectiveContext {
    const Problem::Impl* problem = nullptr;
    const SamplePlan* plan = nullptr;
    fode::PersistentExecutor* executor = nullptr;
    int objective_calls = 0;
    int gradient_calls = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
    double best_aep = -std::numeric_limits<double>::infinity();
    std::vector<double> best_history;
    std::vector<double> best_feasible_values;
};

double objective_callback(
    const unsigned n,
    const double* x,
    double* gradient,
    void* opaque
) {
    if (n != static_cast<unsigned>(kVariables)) return 0.0;
    auto& context = *static_cast<ObjectiveContext*>(opaque);
    const bool need_gradient = gradient != nullptr;
    const auto evaluation = context.problem->evaluate_with_plan(
        unflatten(x), *context.plan, need_gradient, *context.executor
    );
    ++context.objective_calls;
    if (need_gradient) {
        ++context.gradient_calls;
        std::copy(
            evaluation.gradient_gwh_per_m.begin(),
            evaluation.gradient_gwh_per_m.end(), gradient
        );
    }
    context.observed_workers = std::max(
        context.observed_workers, evaluation.observed_workers
    );
    context.evaluator_seconds += evaluation.scenario_seconds;
    if (evaluation.feasible && evaluation.aep_gwh > context.best_aep) {
        context.best_aep = evaluation.aep_gwh;
        context.best_history.push_back(evaluation.aep_gwh);
        context.best_feasible_values.assign(x, x + n);
    }
    return evaluation.aep_gwh;
}

void constraints_callback(
    const unsigned m,
    double* result,
    const unsigned n,
    const double* x,
    double* gradient,
    void*
) {
    if (m != static_cast<unsigned>(kTotalConstraints)
        || n != static_cast<unsigned>(kVariables)) return;
    if (gradient != nullptr) {
        std::fill(
            gradient,
            gradient + static_cast<std::size_t>(m) * kVariables,
            0.0
        );
    }
    int row = 0;
    const double spacing_squared = kMinimumSpacingM * kMinimumSpacingM;
    for (int first = 0; first < turbine_count; ++first) {
        for (int second = first + 1; second < turbine_count; ++second) {
            const double dx = x[static_cast<std::size_t>(first)]
                - x[static_cast<std::size_t>(second)];
            const double dy = x[static_cast<std::size_t>(turbine_count + first)]
                - x[static_cast<std::size_t>(turbine_count + second)];
            result[static_cast<std::size_t>(row)] =
                1.0 - (dx * dx + dy * dy) / spacing_squared;
            if (gradient != nullptr) {
                double* jacobian = gradient
                    + static_cast<std::size_t>(row * kVariables);
                jacobian[static_cast<std::size_t>(first)] =
                    -2.0 * dx / spacing_squared;
                jacobian[static_cast<std::size_t>(second)] =
                    2.0 * dx / spacing_squared;
                jacobian[static_cast<std::size_t>(turbine_count + first)] =
                    -2.0 * dy / spacing_squared;
                jacobian[static_cast<std::size_t>(turbine_count + second)] =
                    2.0 * dy / spacing_squared;
            }
            ++row;
        }
    }
    constexpr double boundary_scale = 5000.0;
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        for (int edge = 0; edge < kBoundaryCount; ++edge) {
            const Point& vertex = kBoundaryVertices[static_cast<std::size_t>(edge)];
            const Point& normal = kBoundaryNormals[static_cast<std::size_t>(edge)];
            const double inside =
                (vertex.x_m - x[static_cast<std::size_t>(turbine)]) * normal.x_m
                + (vertex.y_m - x[
                    static_cast<std::size_t>(turbine_count + turbine)
                ]) * normal.y_m;
            result[static_cast<std::size_t>(row)] = -inside / boundary_scale;
            if (gradient != nullptr) {
                double* jacobian = gradient
                    + static_cast<std::size_t>(row * kVariables);
                jacobian[static_cast<std::size_t>(turbine)] =
                    normal.x_m / boundary_scale;
                jacobian[static_cast<std::size_t>(turbine_count + turbine)] =
                    normal.y_m / boundary_scale;
            }
            ++row;
        }
    }
}

}  // namespace

Problem::Problem(const std::string& data_path)
    : impl_(std::make_unique<Impl>(data_path)) {}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const std::vector<Point>& Problem::layout(const LayoutId id) const {
    return impl_->layouts[static_cast<std::size_t>(id)];
}

MethodSpec Problem::method(const MethodId id) const { return impl_->method(id); }

Evaluation Problem::evaluate(
    const std::vector<Point>& selected_layout,
    const MethodId method_id,
    const std::uint64_t seed,
    const int workers,
    const bool gradient
) const {
    if (selected_layout.size() != turbine_count || workers < 1) {
        throw std::invalid_argument("invalid L0245 evaluation configuration");
    }
    fode::PersistentExecutor executor(workers);
    const SamplePlan plan = impl_->make_plan(
        selected_layout, method_id, seed, executor
    );
    return impl_->evaluate_with_plan(
        selected_layout, plan, gradient, executor
    );
}

RunResult Problem::optimize(const RunConfig& config) const {
    if (config.workers < 1) {
        throw std::invalid_argument("invalid L0245 worker count");
    }
    if (config.method == MethodId::monte_carlo_reference) {
        throw std::invalid_argument(
            "L0245 Monte-Carlo reference is evaluation-only"
        );
    }
    const auto started = Clock::now();
    RunResult result;
    result.starting_layout = to_string(config.starting_layout);
    result.method = to_string(config.method);
    result.seed = config.seed;
    result.requested_workers = config.workers;
    fode::PersistentExecutor executor(config.workers);
    const std::vector<Point> start_layout = layout(config.starting_layout);
    const SamplePlan plan = impl_->make_plan(
        start_layout, config.method, config.seed, executor
    );
    result.regression_seconds = plan.regression_seconds;
    result.initial_evaluation = impl_->evaluate_with_plan(
        start_layout, plan, true, executor
    );
    std::vector<double> values = flatten(start_layout);
    std::vector<double> lower(static_cast<std::size_t>(kVariables), 0.0);
    std::vector<double> upper(static_cast<std::size_t>(kVariables), 0.0);
    for (int turbine = 0; turbine < turbine_count; ++turbine) {
        lower[static_cast<std::size_t>(turbine)] = 0.0;
        upper[static_cast<std::size_t>(turbine)] = 3800.914;
        lower[static_cast<std::size_t>(turbine_count + turbine)] = 0.0;
        upper[static_cast<std::size_t>(turbine_count + turbine)] = 4889.770;
    }
    ObjectiveContext context;
    context.problem = impl_.get();
    context.plan = &plan;
    context.executor = &executor;
    if (result.initial_evaluation.feasible) {
        context.best_aep = result.initial_evaluation.aep_gwh;
        context.best_history.push_back(context.best_aep);
        context.best_feasible_values = values;
    }
    nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, kVariables);
    if (optimizer == nullptr) throw std::runtime_error("L0245 NLopt allocation");
    const int maximum_evaluations = config.smoke
        ? std::min(12, config.maximum_evaluations)
        : config.maximum_evaluations;
    std::vector<double> tolerances(
        static_cast<std::size_t>(kTotalConstraints), 1.0e-8
    );
    const auto configure = [&]() {
        return nlopt_set_lower_bounds(optimizer, lower.data()) >= 0
            && nlopt_set_upper_bounds(optimizer, upper.data()) >= 0
            && nlopt_set_max_objective(
                optimizer, objective_callback, &context
            ) >= 0
            && nlopt_add_inequality_mconstraint(
                optimizer, kTotalConstraints, constraints_callback, nullptr,
                tolerances.data()
            ) >= 0
            && nlopt_set_maxeval(optimizer, maximum_evaluations) >= 0
            && nlopt_set_xtol_rel(
                optimizer, config.relative_x_tolerance
            ) >= 0
            && (config.maximum_seconds <= 0.0
                || nlopt_set_maxtime(optimizer, config.maximum_seconds) >= 0);
    };
    if (!configure()) {
        nlopt_destroy(optimizer);
        throw std::runtime_error("L0245 NLopt configuration");
    }
    const auto optimizer_started = Clock::now();
    double optimum = result.initial_evaluation.aep_gwh;
    const nlopt_result status = nlopt_optimize(
        optimizer, values.data(), &optimum
    );
    result.optimizer_seconds = elapsed_seconds(optimizer_started);
    nlopt_destroy(optimizer);
    result.optimizer_status = static_cast<int>(status);
    result.optimizer_status_name = nlopt_status_name(status);
    result.objective_calls = context.objective_calls;
    result.gradient_calls = context.gradient_calls;
    result.evaluator_seconds = context.evaluator_seconds
        + result.initial_evaluation.scenario_seconds;
    result.observed_workers = std::max(
        context.observed_workers, result.initial_evaluation.observed_workers
    );
    result.final_layout = unflatten(values.data());
    result.final_evaluation = impl_->evaluate_with_plan(
        result.final_layout, plan, false, executor
    );
    result.observed_workers = std::max(
        result.observed_workers, result.final_evaluation.observed_workers
    );
    if (!context.best_feasible_values.empty()
        && (!result.final_evaluation.feasible
            || context.best_aep > result.final_evaluation.aep_gwh + 1.0e-9)) {
        result.final_layout = unflatten(context.best_feasible_values.data());
        result.final_evaluation = impl_->evaluate_with_plan(
            result.final_layout, plan, false, executor
        );
        result.observed_workers = std::max(
            result.observed_workers, result.final_evaluation.observed_workers
        );
    }
    result.best_history_gwh = std::move(context.best_history);
    if (result.best_history_gwh.empty()
        || result.best_history_gwh.back() < result.final_evaluation.aep_gwh) {
        result.best_history_gwh.push_back(result.final_evaluation.aep_gwh);
    }
    if (config.evaluate_monte_carlo_reference) {
        result.reference_seed = 2019024599ULL;
        const SamplePlan reference_plan = impl_->make_plan(
            result.final_layout, MethodId::monte_carlo_reference,
            result.reference_seed, executor
        );
        result.reference_evaluation = impl_->evaluate_with_plan(
            result.final_layout, reference_plan, false, executor
        );
        result.observed_workers = std::max(
            result.observed_workers,
            result.reference_evaluation.observed_workers
        );
    }
    result.end_to_end_seconds = elapsed_seconds(started);
    std::uint64_t hash = 1469598103934665603ULL;
    hash = mix_hash(hash, config.seed);
    hash = mix_hash(hash, static_cast<std::uint64_t>(config.starting_layout));
    hash = mix_hash(hash, static_cast<std::uint64_t>(config.method));
    hash = mix_hash(hash, quantized(result.initial_evaluation.aep_gwh));
    hash = mix_hash(hash, quantized(result.final_evaluation.aep_gwh));
    if (config.evaluate_monte_carlo_reference) {
        hash = mix_hash(hash, quantized(result.reference_evaluation.aep_gwh));
    }
    hash = mix_hash(hash, static_cast<std::uint64_t>(
        result.final_evaluation.selected_polynomial_degree
    ));
    for (const Point& point : result.final_layout) {
        hash = mix_hash(hash, quantized(point.x_m));
        hash = mix_hash(hash, quantized(point.y_m));
    }
    result.scientific_hash = hash;
    return result;
}

ProfileReceipt Problem::profile(
    const LayoutId layout_id,
    const MethodId method_id,
    const std::uint64_t seed,
    const int workers,
    const int repeats
) const {
    if (workers < 1 || repeats < 1) {
        throw std::invalid_argument("invalid L0245 profile configuration");
    }
    ProfileReceipt receipt;
    receipt.method = to_string(method_id);
    receipt.layout = to_string(layout_id);
    receipt.repeats = repeats;
    receipt.requested_workers = workers;
    fode::PersistentExecutor executor(workers);
    const SamplePlan plan = impl_->make_plan(
        layout(layout_id), method_id, seed, executor
    );
    const auto started = Clock::now();
    std::uint64_t hash = 1469598103934665603ULL;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const auto evaluation = impl_->evaluate_with_plan(
            layout(layout_id), plan, true, executor
        );
        receipt.aep_checksum_gwh += evaluation.aep_gwh;
        receipt.physical_wake_simulations += evaluation.physical_wake_simulations;
        receipt.observed_workers = std::max(
            receipt.observed_workers, evaluation.observed_workers
        );
        hash = mix_hash(hash, quantized(evaluation.aep_gwh));
        for (const double value : evaluation.gradient_gwh_per_m) {
            hash = mix_hash(hash, quantized(value));
        }
    }
    receipt.seconds = elapsed_seconds(started);
    receipt.scientific_hash = hash;
    return receipt;
}

std::string to_string(const LayoutId id) {
    switch (id) {
        case LayoutId::grid: return "grid";
        case LayoutId::amalia: return "amalia";
        case LayoutId::optimized: return "optimized";
        case LayoutId::random: return "random";
    }
    return "unknown";
}

std::string to_string(const MethodId id) {
    switch (id) {
        case MethodId::pcr_coarse: return "pcr_coarse";
        case MethodId::pcr_fine: return "pcr_fine";
        case MethodId::rectangle_coarse: return "rectangle_coarse";
        case MethodId::rectangle_fine: return "rectangle_fine";
        case MethodId::monte_carlo_reference:
            return "monte_carlo_reference";
    }
    return "unknown";
}

LayoutId parse_layout(const std::string& value) {
    if (value == "grid") return LayoutId::grid;
    if (value == "amalia") return LayoutId::amalia;
    if (value == "optimized") return LayoutId::optimized;
    if (value == "random") return LayoutId::random;
    throw std::invalid_argument("unknown L0245 layout " + value);
}

MethodId parse_method(const std::string& value) {
    if (value == "pcr_coarse" || value == "PC-R-coarse") {
        return MethodId::pcr_coarse;
    }
    if (value == "pcr_fine" || value == "PC-R-fine") {
        return MethodId::pcr_fine;
    }
    if (value == "rectangle_coarse" || value == "Rect-coarse") {
        return MethodId::rectangle_coarse;
    }
    if (value == "rectangle_fine" || value == "Rect-fine") {
        return MethodId::rectangle_fine;
    }
    if (value == "monte_carlo_reference" || value == "MC-reference") {
        return MethodId::monte_carlo_reference;
    }
    throw std::invalid_argument("unknown L0245 method " + value);
}

}  // namespace core99::l0245
