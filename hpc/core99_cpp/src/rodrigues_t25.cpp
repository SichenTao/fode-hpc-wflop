/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T25 pure-C++ exact-gradient, flow-parallel and
incremental-SMAST implementation
Paper/DOI: Speeding Up Large-Wind-Farm Layout Optimization Using Gradients,
Parallelization, and a Heuristic Algorithm for the Initial Layout;
10.5194/wes-9-321-2024
Public source, omissions, conflicts, resolutions, semantic identities, HPC
analysis and claim boundary:
hpc/core99_cpp/include/core99/rodrigues_t25.hpp
Controlling contract: shared/contracts/core99_t25_rodrigues_2024.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "core99/rodrigues_t25.hpp"

#include <nlopt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core99::t25 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kHoursPerYear = 8760.0;
constexpr double kWakeExpansion = 0.0324555;
constexpr double kIeaDiameterM = 130.0;
constexpr double kHornsDiameterM = 80.0;
constexpr double kMinimumSpacingDiameters = 2.0;

constexpr std::array<double, 16> kIeaDirectionFrequency{
    .025, .024, .029, .036, .063, .065, .100, .122,
    .063, .038, .039, .083, .213, .046, .032, .022,
};

constexpr std::array<double, 12> kHornsDirectionFrequency{
    3.597152, 3.948682, 5.167395, 7.000154,
    8.364547, 6.434850, 8.643194, 11.770510,
    15.157570, 14.737920, 10.012050, 5.165975,
};
constexpr std::array<double, 12> kHornsWeibullScale{
    9.176929, 9.782334, 9.531809, 9.909545,
    10.042690, 9.593921, 9.584007, 10.514990,
    11.398950, 11.687460, 11.637320, 10.088030,
};
constexpr std::array<double, 12> kHornsWeibullShape{
    2.392578, 2.447266, 2.412109, 2.591797,
    2.755859, 2.595703, 2.583984, 2.548828,
    2.470703, 2.607422, 2.626953, 2.326172,
};
constexpr std::array<double, 23> kV80Speed{
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25,
};
constexpr std::array<double, 23> kV80PowerKw{
    0, 66.6, 154, 282, 460, 696, 996, 1341, 1661, 1866, 1958,
    1988, 1997, 1999, 2000, 2000, 2000, 2000, 2000, 2000, 2000,
    2000, 2000,
};
constexpr std::array<double, 23> kV80Ct{
    0, .818, .806, .804, .805, .806, .807, .793, .739, .709,
    .409, .314, .249, .202, .167, .140, .119, .102, .088, .077,
    .067, .060, .053,
};

constexpr std::array<double, 80> kHornsX{
    423974,424042,424111,424179,424247,424315,424384,424452,
    424534,424602,424671,424739,424807,424875,424944,425012,
    425094,425162,425231,425299,425367,425435,425504,425572,
    425654,425722,425791,425859,425927,425995,426064,426132,
    426214,426282,426351,426419,426487,426555,426624,426692,
    426774,426842,426911,426979,427047,427115,427184,427252,
    427334,427402,427471,427539,427607,427675,427744,427812,
    427894,427962,428031,428099,428167,428235,428304,428372,
    428454,428522,428591,428659,428727,428795,428864,428932,
    429014,429082,429151,429219,429287,429355,429424,429492,
};
constexpr std::array<double, 80> kHornsY{
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
    6151447,6150891,6150335,6149779,6149224,6148668,6148112,6147556,
};

struct FlowState {
    double sine = 0.0;
    double cosine = 1.0;
    double speed_mps = 0.0;
    double probability = 0.0;
    double ct = 0.0;
};

struct PowerAndSlope {
    double power_mw = 0.0;
    double slope_mw_per_mps = 0.0;
};

struct CtAndSlope {
    double ct = 0.0;
    double slope_per_mps = 0.0;
};

struct PairTerms {
    double deficit_mps = 0.0;
    double derivative_downstream = 0.0;
    double derivative_crosswind = 0.0;
    double derivative_ct = 0.0;
};

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

double weibull_cdf(
    const double speed,
    const double scale,
    const double shape
) noexcept {
    if (!(speed > 0.0)) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

template<std::size_t N>
double linear_interpolation(
    const std::array<double, N>& x,
    const std::array<double, N>& y,
    const double value
) noexcept {
    if (value < x.front() || value > x.back()) return 0.0;
    const auto upper = std::lower_bound(x.begin(), x.end(), value);
    if (upper == x.begin()) return y.front();
    if (upper == x.end()) return y.back();
    const std::size_t hi = static_cast<std::size_t>(upper - x.begin());
    const std::size_t lo = hi - 1U;
    const double width = x[hi] - x[lo];
    return y[lo] + (y[hi] - y[lo]) * (value - x[lo]) / width;
}

PowerAndSlope iea_power(const double speed) noexcept {
    if (!(speed > 4.0) || speed > 25.0) return {};
    if (speed >= 9.8) return {3.35, 0.0};
    constexpr double width = 9.8 - 4.0;
    const double fraction = (speed - 4.0) / width;
    return {
        3.35 * fraction * fraction * fraction,
        3.0 * 3.35 * fraction * fraction / width,
    };
}

PowerAndSlope v80_power(const double speed) noexcept {
    if (speed < kV80Speed.front() || speed > kV80Speed.back()) return {};
    const auto upper = std::lower_bound(
        kV80Speed.begin(), kV80Speed.end(), speed
    );
    if (upper == kV80Speed.begin()) return {0.0, 0.0666};
    if (upper == kV80Speed.end()) return {2.0, 0.0};
    const std::size_t hi = static_cast<std::size_t>(
        upper - kV80Speed.begin()
    );
    const std::size_t lo = hi - 1U;
    const double slope = (kV80PowerKw[hi] - kV80PowerKw[lo])
        / (kV80Speed[hi] - kV80Speed[lo]) / 1000.0;
    const double power = (
        kV80PowerKw[lo]
        + slope * 1000.0 * (speed - kV80Speed[lo])
    ) / 1000.0;
    return {power, slope};
}

CtAndSlope v80_ct(const double speed) noexcept {
    if (speed < kV80Speed.front()) return {};
    if (speed > kV80Speed.back()) return {kV80Ct.back(), 0.0};
    const auto upper = std::lower_bound(
        kV80Speed.begin(), kV80Speed.end(), speed
    );
    if (upper == kV80Speed.begin()) return {kV80Ct.front(), 0.818};
    if (upper == kV80Speed.end()) return {kV80Ct.back(), 0.0};
    const std::size_t hi = static_cast<std::size_t>(
        upper - kV80Speed.begin()
    );
    const std::size_t lo = hi - 1U;
    const double slope = (kV80Ct[hi] - kV80Ct[lo])
        / (kV80Speed[hi] - kV80Speed[lo]);
    return {
        kV80Ct[lo] + slope * (speed - kV80Speed[lo]),
        slope,
    };
}

PairTerms pair_terms(
    const double downstream,
    const double crosswind,
    const double speed,
    const double ct,
    const double diameter,
    const bool simplified_iea
) noexcept {
    if (!(downstream > 1.0e-10) || !(ct > 0.0)) return {};
    double sigma0 = diameter / std::sqrt(8.0);
    double derivative_sigma_ct = 0.0;
    if (!simplified_iea) {
        const double root_one_minus_ct = std::sqrt(
            1.0 - std::min(.999, ct)
        );
        const double beta = 0.5 * (1.0 + root_one_minus_ct)
            / root_one_minus_ct;
        const double root_beta = std::sqrt(beta);
        sigma0 = 0.2 * diameter * root_beta;
        if (ct < .999) {
            derivative_sigma_ct = 0.2 * diameter
                / (8.0 * root_beta
                    * root_one_minus_ct * root_one_minus_ct
                    * root_one_minus_ct);
        }
    }
    const double sigma = kWakeExpansion * downstream + sigma0;
    const double ratio = ct * diameter * diameter
        / (8.0 * sigma * sigma);
    const double radical = std::max(1.0e-14, 1.0 - ratio);
    const double root = std::sqrt(radical);
    const double centre = 1.0 - root;
    const double exponential = std::exp(
        -0.5 * crosswind * crosswind / (sigma * sigma)
    );
    const double deficit = speed * centre * exponential;
    const double centre_sigma = -ratio / (sigma * root);
    const double deficit_sigma = speed * exponential * (
        centre_sigma
        + centre * crosswind * crosswind / (sigma * sigma * sigma)
    );
    const double direct_centre_ct = diameter * diameter
        / (16.0 * sigma * sigma * root);
    return {
        deficit,
        deficit_sigma * kWakeExpansion,
        -speed * centre * exponential * crosswind / (sigma * sigma),
        deficit_sigma * derivative_sigma_ct
            + speed * exponential * direct_centre_ct,
    };
}

std::vector<Point> circular_layout(
    const std::vector<int>& orbit_counts,
    const double radius
) {
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(
        std::accumulate(orbit_counts.begin(), orbit_counts.end(), 0)
    ));
    const int outer_count = orbit_counts.back();
    for (std::size_t orbit = 0; orbit < orbit_counts.size(); ++orbit) {
        const int count = orbit_counts[orbit];
        if (count == 1) {
            layout.push_back({0.0, 0.0});
            continue;
        }
        const double orbit_radius = radius
            * static_cast<double>(count) / static_cast<double>(outer_count);
        for (int index = 0; index < count; ++index) {
            const double angle = 2.0 * kPi * static_cast<double>(index)
                / static_cast<double>(count);
            layout.push_back({
                orbit_radius * std::cos(angle),
                orbit_radius * std::sin(angle),
            });
        }
    }
    return layout;
}

std::pair<std::vector<Point>, double> make_iea_layout(const int count) {
    switch (count) {
        // These coordinates are the public IEA-37 YAML values consumed by
        // PyWake v2.5.0. Retaining their published decimal precision matters:
        // idealized trigonometric reconstructions change wake ordering near
        // symmetry axes and therefore do not reproduce the source oracle.
        case 16: return {std::vector<Point>{
            {0.0, 0.0}, {650.0, 0.0}, {200.861, 618.1867},
            {-525.861, 382.0604}, {-525.861, -382.0604},
            {200.861, -618.1867}, {1300.0, 0.0},
            {1051.7221, 764.1208}, {401.7221, 1236.3735},
            {-401.7221, 1236.3735}, {-1051.7221, 764.1208},
            {-1300.0, 0.0}, {-1051.7221, -764.1208},
            {-401.7221, -1236.3735}, {401.7221, -1236.3735},
            {1051.7221, -764.1208},
        }, 1300.0};
        case 36: return {std::vector<Point>{
            {0.0, 0.0}, {666.6667, 0.0}, {206.0113, 634.0377},
            {-539.3447, 391.8568}, {-539.3447, -391.8568},
            {206.0113, -634.0377}, {1333.3333, 0.0},
            {1154.7005, 666.6667}, {666.6667, 1154.7005},
            {0.0, 1333.3333}, {-666.6667, 1154.7005},
            {-1154.7005, 666.6667}, {-1333.3333, 0.0},
            {-1154.7005, -666.6667}, {-666.6667, -1154.7005},
            {0.0, -1333.3333}, {666.6667, -1154.7005},
            {1154.7005, -666.6667}, {2000.0, 0.0},
            {1879.3852, 684.0403}, {1532.0889, 1285.5752},
            {1000.0, 1732.0508}, {347.2964, 1969.6155},
            {-347.2964, 1969.6155}, {-1000.0, 1732.0508},
            {-1532.0889, 1285.5752}, {-1879.3852, 684.0403},
            {-2000.0, 0.0}, {-1879.3852, -684.0403},
            {-1532.0889, -1285.5752}, {-1000.0, -1732.0508},
            {-347.2964, -1969.6155}, {347.2964, -1969.6155},
            {1000.0, -1732.0508}, {1532.0889, -1285.5752},
            {1879.3852, -684.0403},
        }, 2000.0};
        case 64: return {std::vector<Point>{
            {0.0, 0.0}, {750.0, 0.0}, {231.7627, 713.2924},
            {-606.7627, 440.8389}, {-606.7627, -440.8389},
            {231.7627, -713.2924}, {1500.0, 0.0},
            {1299.0381, 750.0}, {750.0, 1299.0381}, {0.0, 1500.0},
            {-750.0, 1299.0381}, {-1299.0381, 750.0},
            {-1500.0, 0.0}, {-1299.0381, -750.0},
            {-750.0, -1299.0381}, {0.0, -1500.0},
            {750.0, -1299.0381}, {1299.0381, -750.0},
            {2250.0, 0.0}, {2114.3084, 769.5453},
            {1723.6, 1446.2721}, {1125.0, 1948.5572},
            {390.7084, 2215.8174}, {-390.7084, 2215.8174},
            {-1125.0, 1948.5572}, {-1723.6, 1446.2721},
            {-2114.3084, 769.5453}, {-2250.0, 0.0},
            {-2114.3084, -769.5453}, {-1723.6, -1446.2721},
            {-1125.0, -1948.5572}, {-390.7084, -2215.8174},
            {390.7084, -2215.8174}, {1125.0, -1948.5572},
            {1723.6, -1446.2721}, {2114.3084, -769.5453},
            {3000.0, 0.0}, {2924.7837, 667.5628},
            {2702.9066, 1301.6512}, {2345.4944, 1870.4694},
            {1870.4694, 2345.4944}, {1301.6512, 2702.9066},
            {667.5628, 2924.7837}, {0.0, 3000.0},
            {-667.5628, 2924.7837}, {-1301.6512, 2702.9066},
            {-1870.4694, 2345.4944}, {-2345.4944, 1870.4694},
            {-2702.9066, 1301.6512}, {-2924.7837, 667.5628},
            {-3000.0, 0.0}, {-2924.7837, -667.5628},
            {-2702.9066, -1301.6512}, {-2345.4944, -1870.4694},
            {-1870.4694, -2345.4944}, {-1301.6512, -2702.9066},
            {-667.5628, -2924.7837}, {0.0, -3000.0},
            {667.5628, -2924.7837}, {1301.6512, -2702.9066},
            {1870.4694, -2345.4944}, {2345.4944, -1870.4694},
            {2702.9066, -1301.6512}, {2924.7837, -667.5628},
        }, 3000.0};
        case 130: return {
            circular_layout({1, 6, 12, 18, 25, 31, 37}, 4500.0), 4500.0
        };
        case 279: return {
            circular_layout({1,6,12,18,25,31,37,43,50,56}, 6750.0),
            6750.0
        };
        case 566: return {
            circular_layout(
                {1,6,12,18,25,31,37,43,50,56,62,69,75,81}, 9750.0
            ),
            9750.0
        };
        default:
            throw std::invalid_argument("unsupported T25 IEA-37 turbine count");
    }
}

std::pair<std::vector<Point>, std::array<Point, 4>> make_horns_layout(
    const int count
) {
    int extra_rows = 0;
    int extra_columns = 0;
    switch (count) {
        case 100: extra_rows = 2; break;
        case 200: extra_rows = 12; break;
        case 300: extra_rows = 12; extra_columns = 5; break;
        case 400: extra_rows = 12; extra_columns = 10; break;
        case 500: extra_rows = 12; extra_columns = 15; break;
        default:
            throw std::invalid_argument("unsupported T25 Horns Rev turbine count");
    }
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(count));
    for (std::size_t index = 0; index < kHornsX.size(); ++index) {
        layout.push_back({kHornsX[index], kHornsY[index]});
    }
    constexpr double column_offset = 560.0;
    for (int column = 1; column <= extra_columns; ++column) {
        for (int row = 0; row < 8; ++row) {
            layout.push_back({
                kHornsX[72U + static_cast<std::size_t>(row)]
                    + column_offset * static_cast<double>(column),
                kHornsY[72U + static_cast<std::size_t>(row)],
            });
        }
    }
    const int columns = 10 + extra_columns;
    const double vertical_offset = 556.0;
    const double diagonal_offset = std::abs(
        (kHornsY.back() - kHornsY.front())
        / static_cast<double>(kHornsY.size() - 1U)
    );
    for (int row = 1; row <= extra_rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const Point top = layout[static_cast<std::size_t>(column * 8)];
            layout.push_back({
                top.x_m - diagonal_offset * static_cast<double>(row),
                top.y_m + vertical_offset * static_cast<double>(row),
            });
        }
    }
    if (static_cast<int>(layout.size()) != count) {
        throw std::runtime_error("T25 Horns Rev extension count mismatch");
    }
    const double mean_x = std::accumulate(
        layout.begin(), layout.end(), 0.0,
        [](const double total, const Point& point) {
            return total + point.x_m;
        }
    ) / static_cast<double>(layout.size());
    const double mean_y = std::accumulate(
        layout.begin(), layout.end(), 0.0,
        [](const double total, const Point& point) {
            return total + point.y_m;
        }
    ) / static_cast<double>(layout.size());
    for (Point& point : layout) {
        point.x_m -= mean_x;
        point.y_m -= mean_y;
    }
    double min_y = std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (const Point& point : layout) {
        min_y = std::min(min_y, point.y_m);
        max_y = std::max(max_y, point.y_m);
    }
    Point lower_left{std::numeric_limits<double>::infinity(), min_y};
    Point lower_right{-std::numeric_limits<double>::infinity(), min_y};
    Point upper_left{std::numeric_limits<double>::infinity(), max_y};
    Point upper_right{-std::numeric_limits<double>::infinity(), max_y};
    for (const Point& point : layout) {
        if (std::abs(point.y_m - min_y) < .1) {
            lower_left.x_m = std::min(lower_left.x_m, point.x_m);
            lower_right.x_m = std::max(lower_right.x_m, point.x_m);
        }
        if (std::abs(point.y_m - max_y) < .1) {
            upper_left.x_m = std::min(upper_left.x_m, point.x_m);
            upper_right.x_m = std::max(upper_right.x_m, point.x_m);
        }
    }
    constexpr double padding = column_offset;
    lower_left.x_m -= padding;
    lower_left.y_m -= padding;
    lower_right.x_m += padding;
    lower_right.y_m -= padding;
    upper_left.x_m -= padding;
    upper_left.y_m += padding;
    upper_right.x_m += padding;
    upper_right.y_m += padding;
    return {
        std::move(layout),
        {lower_left, lower_right, upper_right, upper_left},
    };
}

std::vector<FlowState> make_states(const ProblemConfig& config) {
    std::vector<FlowState> states;
    const double direction_width = 360.0
        / static_cast<double>(config.direction_count);
    if (config.family == ProblemFamily::iea37) {
        states.reserve(static_cast<std::size_t>(config.direction_count));
        for (int direction = 0; direction < config.direction_count; ++direction) {
            const double degrees = direction_width * direction;
            const int sector = static_cast<int>(std::floor(
                (degrees + 11.25) / 22.5
            )) % 16;
            const double angle = degrees * kPi / 180.0;
            states.push_back({
                std::sin(angle), std::cos(angle), 9.8,
                kIeaDirectionFrequency[static_cast<std::size_t>(sector)]
                    * direction_width / 22.5,
                8.0 / 9.0,
            });
        }
        return states;
    }
    const double frequency_sum = std::accumulate(
        kHornsDirectionFrequency.begin(),
        kHornsDirectionFrequency.end(),
        0.0
    );
    states.reserve(static_cast<std::size_t>(
        config.direction_count * config.speed_count
    ));
    for (int direction = 0; direction < config.direction_count; ++direction) {
        const double degrees = direction_width * direction;
        // xarray/PyWake nearest-neighbour interpolation resolves an exact
        // half-sector tie to the lower sector (wd=15 remains in sector 0).
        const int sector = static_cast<int>(std::floor(
            std::nextafter(degrees + 15.0, -std::numeric_limits<double>::infinity())
                / 30.0
        )) % 12;
        const double angle = degrees * kPi / 180.0;
        for (int speed_index = 0; speed_index < config.speed_count; ++speed_index) {
            const int source_index = config.speed_count == 1
                ? 7
                : static_cast<int>(std::llround(
                    static_cast<double>(speed_index) * 22.0
                    / static_cast<double>(config.speed_count - 1)
                ));
            const double speed = kV80Speed[static_cast<std::size_t>(source_index)];
            const double low = speed - .5;
            const double high = speed + .5;
            const double speed_probability = weibull_cdf(
                high,
                kHornsWeibullScale[static_cast<std::size_t>(sector)],
                kHornsWeibullShape[static_cast<std::size_t>(sector)]
            ) - weibull_cdf(
                low,
                kHornsWeibullScale[static_cast<std::size_t>(sector)],
                kHornsWeibullShape[static_cast<std::size_t>(sector)]
            );
            states.push_back({
                std::sin(angle), std::cos(angle), speed,
                kHornsDirectionFrequency[static_cast<std::size_t>(sector)]
                    / frequency_sum * direction_width / 30.0
                    * speed_probability,
                linear_interpolation(kV80Speed, kV80Ct, speed),
            });
        }
    }
    return states;
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

std::vector<double> flatten(const std::vector<Point>& layout) {
    std::vector<double> values(2U * layout.size());
    for (std::size_t index = 0; index < layout.size(); ++index) {
        values[2U * index] = layout[index].x_m;
        values[2U * index + 1U] = layout[index].y_m;
    }
    return values;
}

std::vector<Point> unflatten(const double* values, const int count) {
    std::vector<Point> layout(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        layout[static_cast<std::size_t>(index)] = {
            values[2 * index], values[2 * index + 1]
        };
    }
    return layout;
}

std::uint64_t scientific_hash(
    const std::vector<Point>& layout,
    const double aep_gwh
) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&hash](const double value) {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (bits >> (8 * byte)) & 0xffULL;
            hash *= 1099511628211ULL;
        }
    };
    add(aep_gwh);
    for (const Point& point : layout) {
        add(point.x_m);
        add(point.y_m);
    }
    return hash;
}

}  // namespace

struct Problem::Data {
    ProblemConfig config;
    std::vector<Point> reference_layout;
    std::vector<FlowState> states;
    double diameter_m = 0.0;
    double boundary_radius_m = 0.0;
    std::array<Point, 4> polygon{};
};

const char* family_name(const ProblemFamily family) noexcept {
    return family == ProblemFamily::iea37 ? "iea37" : "horns_rev";
}

const char* gradient_name(const GradientMode mode) noexcept {
    switch (mode) {
        case GradientMode::none: return "none";
        case GradientMode::exact_reverse: return "exact_reverse";
        case GradientMode::central_finite_difference: return "central_fd";
    }
    return "unknown";
}

Problem::Problem(ProblemConfig config) {
    if (config.direction_count < 1 || config.direction_count > 360) {
        throw std::invalid_argument("T25 direction count must be in [1,360]");
    }
    if (config.family == ProblemFamily::iea37) config.speed_count = 1;
    if (config.speed_count < 1 || config.speed_count > 23) {
        throw std::invalid_argument("T25 speed count must be in [1,23]");
    }
    auto data = std::make_shared<Data>();
    data->config = config;
    if (config.family == ProblemFamily::iea37) {
        auto [layout, radius] = make_iea_layout(config.turbine_count);
        data->reference_layout = std::move(layout);
        data->boundary_radius_m = radius;
        data->diameter_m = kIeaDiameterM;
    } else {
        auto [layout, polygon] = make_horns_layout(config.turbine_count);
        data->reference_layout = std::move(layout);
        data->polygon = polygon;
        data->diameter_m = kHornsDiameterM;
    }
    data->states = make_states(config);
    data_ = std::move(data);
}

const ProblemConfig& Problem::config() const noexcept { return data_->config; }

std::string Problem::semantic_id() const {
    return data_->config.family == ProblemFamily::iea37
        ? "t25_iea37_scaled_sbg_v1"
        : "t25_hornsrev_scaled_bg_v80_v1";
}

double Problem::rotor_diameter_m() const noexcept { return data_->diameter_m; }
double Problem::boundary_radius_m() const noexcept {
    return data_->boundary_radius_m;
}
const std::vector<Point>& Problem::reference_layout() const noexcept {
    return data_->reference_layout;
}

double Problem::minimum_spacing(const std::vector<Point>& layout) const {
    if (layout.size() < 2U) return std::numeric_limits<double>::infinity();
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t left = 0; left < layout.size(); ++left) {
        for (std::size_t right = left + 1U; right < layout.size(); ++right) {
            minimum = std::min(minimum, std::hypot(
                layout[left].x_m - layout[right].x_m,
                layout[left].y_m - layout[right].y_m
            ));
        }
    }
    return minimum;
}

double Problem::maximum_boundary_violation(
    const std::vector<Point>& layout
) const {
    double violation = 0.0;
    if (data_->config.family == ProblemFamily::iea37) {
        for (const Point& point : layout) {
            violation = std::max(
                violation,
                std::hypot(point.x_m, point.y_m) - data_->boundary_radius_m
            );
        }
        return violation;
    }
    const auto& polygon = data_->polygon;
    for (const Point& point : layout) {
        for (std::size_t edge = 0; edge < polygon.size(); ++edge) {
            const Point& a = polygon[edge];
            const Point& b = polygon[(edge + 1U) % polygon.size()];
            const double cross = (b.x_m - a.x_m) * (point.y_m - a.y_m)
                - (b.y_m - a.y_m) * (point.x_m - a.x_m);
            const double length = std::hypot(b.x_m - a.x_m, b.y_m - a.y_m);
            violation = std::max(violation, -cross / length);
        }
    }
    return violation;
}

std::vector<Point> Problem::random_feasible_layout(
    const std::uint64_t seed,
    const int start_index
) const {
    if (data_->config.family != ProblemFamily::iea37) {
        return data_->reference_layout;
    }
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(data_->config.turbine_count));
    const double spacing = kMinimumSpacingDiameters * data_->diameter_m;
    std::uint64_t attempt = 0;
    while (static_cast<int>(layout.size()) < data_->config.turbine_count) {
        if (++attempt > 100000000ULL) {
            throw std::runtime_error("T25 random feasible layout exhausted");
        }
        const double radius = data_->boundary_radius_m * std::sqrt(uniform01(
            seed, static_cast<std::uint64_t>(start_index), 2U * attempt
        ));
        const double angle = 2.0 * kPi * uniform01(
            seed, static_cast<std::uint64_t>(start_index), 2U * attempt + 1U
        );
        const Point candidate{radius * std::cos(angle), radius * std::sin(angle)};
        const bool feasible = std::all_of(
            layout.begin(), layout.end(),
            [&](const Point& point) {
                return std::hypot(
                    point.x_m - candidate.x_m,
                    point.y_m - candidate.y_m
                ) >= spacing;
            }
        );
        if (feasible) layout.push_back(candidate);
    }
    return layout;
}

Evaluation Problem::evaluate(
    const std::vector<Point>& layout,
    const GradientMode mode,
    fode::PersistentExecutor& executor
) const {
    if (layout.size() != data_->reference_layout.size()) {
        throw std::invalid_argument("T25 layout size mismatch");
    }
    const auto started = Clock::now();
    const int count = static_cast<int>(layout.size());
    const int variables = 2 * count;
    const int state_count = static_cast<int>(data_->states.size());
    const bool simplified = data_->config.family == ProblemFamily::iea37;
    const auto evaluate_once = [&](
        const std::vector<Point>& points,
        const bool exact_gradient
    ) {
        std::vector<double> state_aep(static_cast<std::size_t>(state_count), 0.0);
        std::vector<double> state_gradient;
        if (exact_gradient) {
            state_gradient.assign(
                static_cast<std::size_t>(state_count * variables), 0.0
            );
        }
        executor.parallel_for(0, state_count, [&](const int state_index) {
            const FlowState& state = data_->states[static_cast<std::size_t>(state_index)];
            thread_local std::vector<double> downstream;
            thread_local std::vector<double> crosswind;
            thread_local std::vector<double> sum_squares;
            thread_local std::vector<double> effective_speed;
            thread_local std::vector<double> turbine_ct;
            thread_local std::vector<double> turbine_ct_slope;
            thread_local std::vector<double> power_slope;
            thread_local std::vector<double> adjoint_ct;
            thread_local std::vector<double> adjoint_effective_speed;
            thread_local std::vector<int> downstream_order;
            thread_local std::vector<PairTerms> pairs;
            downstream.resize(static_cast<std::size_t>(count));
            crosswind.resize(static_cast<std::size_t>(count));
            sum_squares.assign(static_cast<std::size_t>(count), 0.0);
            effective_speed.resize(static_cast<std::size_t>(count));
            turbine_ct.resize(static_cast<std::size_t>(count));
            turbine_ct_slope.resize(static_cast<std::size_t>(count));
            power_slope.resize(static_cast<std::size_t>(count));
            downstream_order.resize(static_cast<std::size_t>(count));
            if (exact_gradient) {
                pairs.assign(static_cast<std::size_t>(count * count), {});
                adjoint_ct.assign(static_cast<std::size_t>(count), 0.0);
                adjoint_effective_speed.resize(static_cast<std::size_t>(count));
            }
            for (int turbine = 0; turbine < count; ++turbine) {
                const Point& point = points[static_cast<std::size_t>(turbine)];
                // PyWake StraightDistance uses theta = 90 deg - wd and the
                // meteorological convention that wind comes from wd:
                //   dw  = -sin(wd) * dx - cos(wd) * dy
                //   hcw =  cos(wd) * dx - sin(wd) * dy
                // Store the corresponding per-point coordinates so pairwise
                // differences reproduce that public-source convention.
                downstream[static_cast<std::size_t>(turbine)] =
                    -point.x_m * state.sine - point.y_m * state.cosine;
                crosswind[static_cast<std::size_t>(turbine)] =
                    point.x_m * state.cosine - point.y_m * state.sine;
                downstream_order[static_cast<std::size_t>(turbine)] = turbine;
            }
            std::stable_sort(
                downstream_order.begin(), downstream_order.end(),
                [&](const int left, const int right) {
                    return downstream[static_cast<std::size_t>(left)]
                        < downstream[static_cast<std::size_t>(right)];
                }
            );
            const double annual_weight = kHoursPerYear
                * state.probability / 1000.0;
            double aep = 0.0;
            for (int target_position = 0;
                 target_position < count;
                 ++target_position) {
                const int target = downstream_order[
                    static_cast<std::size_t>(target_position)
                ];
                for (int source_position = 0;
                     source_position < target_position;
                     ++source_position) {
                    const int source = downstream_order[
                        static_cast<std::size_t>(source_position)
                    ];
                    const PairTerms terms = pair_terms(
                        downstream[static_cast<std::size_t>(target)]
                            - downstream[static_cast<std::size_t>(source)],
                        crosswind[static_cast<std::size_t>(target)]
                            - crosswind[static_cast<std::size_t>(source)],
                        state.speed_mps,
                        turbine_ct[static_cast<std::size_t>(source)],
                        data_->diameter_m,
                        simplified
                    );
                    sum_squares[static_cast<std::size_t>(target)] +=
                        terms.deficit_mps * terms.deficit_mps;
                    if (exact_gradient) {
                        pairs[static_cast<std::size_t>(target * count + source)] = terms;
                    }
                }
                const double total_deficit = std::sqrt(
                    sum_squares[static_cast<std::size_t>(target)]
                );
                const double effective = std::max(
                    0.0, state.speed_mps - total_deficit
                );
                effective_speed[static_cast<std::size_t>(target)] = effective;
                if (simplified) {
                    turbine_ct[static_cast<std::size_t>(target)] = state.ct;
                    turbine_ct_slope[static_cast<std::size_t>(target)] = 0.0;
                } else {
                    const CtAndSlope ct = v80_ct(effective);
                    turbine_ct[static_cast<std::size_t>(target)] = ct.ct;
                    turbine_ct_slope[static_cast<std::size_t>(target)] =
                        ct.slope_per_mps;
                }
                const PowerAndSlope power = simplified
                    ? iea_power(effective)
                    : v80_power(effective);
                power_slope[static_cast<std::size_t>(target)] =
                    power.slope_mw_per_mps;
                aep += power.power_mw * annual_weight;
            }
            if (exact_gradient) {
                double* gradient = state_gradient.data()
                    + static_cast<std::size_t>(state_index * variables);
                for (int turbine = 0; turbine < count; ++turbine) {
                    adjoint_effective_speed[static_cast<std::size_t>(turbine)] =
                        annual_weight
                        * power_slope[static_cast<std::size_t>(turbine)];
                }
                // Reverse accumulation follows the opposite physical wake
                // order. This includes the otherwise easily missed path
                // position -> effective source speed -> V80 CT -> downstream
                // wake, matching PyWake PropagateDownwind semantics.
                for (int target_position = count - 1;
                     target_position >= 0;
                     --target_position) {
                    const int target = downstream_order[
                        static_cast<std::size_t>(target_position)
                    ];
                    adjoint_effective_speed[static_cast<std::size_t>(target)] +=
                        adjoint_ct[static_cast<std::size_t>(target)]
                        * turbine_ct_slope[static_cast<std::size_t>(target)];
                    const double total_deficit = std::sqrt(
                        sum_squares[static_cast<std::size_t>(target)]
                    );
                    if (!(total_deficit > 1.0e-15)) continue;
                    const double adjoint_sum_squares =
                        -adjoint_effective_speed[static_cast<std::size_t>(target)]
                        / (2.0 * total_deficit);
                    for (int source_position = 0;
                         source_position < target_position;
                         ++source_position) {
                        const int source = downstream_order[
                            static_cast<std::size_t>(source_position)
                        ];
                    const PairTerms& terms = pairs[
                        static_cast<std::size_t>(target * count + source)
                    ];
                    if (!(terms.deficit_mps > 0.0)) continue;
                    const double multiplier = 2.0 * terms.deficit_mps
                        * adjoint_sum_squares;
                    const double derivative_x = multiplier * (
                        -terms.derivative_downstream * state.sine
                        + terms.derivative_crosswind * state.cosine
                    );
                    const double derivative_y = multiplier * (
                        -terms.derivative_downstream * state.cosine
                        - terms.derivative_crosswind * state.sine
                    );
                    gradient[2 * target] += derivative_x;
                    gradient[2 * target + 1] += derivative_y;
                    gradient[2 * source] -= derivative_x;
                    gradient[2 * source + 1] -= derivative_y;
                        adjoint_ct[static_cast<std::size_t>(source)] +=
                            multiplier * terms.derivative_ct;
                    }
                }
            }
            state_aep[static_cast<std::size_t>(state_index)] = aep;
        });
        Evaluation value;
        value.aep_gwh = std::accumulate(state_aep.begin(), state_aep.end(), 0.0);
        if (exact_gradient) {
            value.gradient_gwh_per_m.assign(static_cast<std::size_t>(variables), 0.0);
            for (int state = 0; state < state_count; ++state) {
                const double* gradient = state_gradient.data()
                    + static_cast<std::size_t>(state * variables);
                for (int variable = 0; variable < variables; ++variable) {
                    value.gradient_gwh_per_m[static_cast<std::size_t>(variable)] +=
                        gradient[variable];
                }
            }
        }
        return value;
    };

    executor.reset_work_receipt();
    Evaluation result;
    if (mode == GradientMode::none) {
        result = evaluate_once(layout, false);
        result.physical_layout_evaluations = 1;
    } else if (mode == GradientMode::exact_reverse) {
        result = evaluate_once(layout, true);
        result.physical_layout_evaluations = 1;
    } else {
        result = evaluate_once(layout, false);
        result.gradient_gwh_per_m.assign(static_cast<std::size_t>(variables), 0.0);
        constexpr double step_m = .05;
        std::vector<Point> perturbed = layout;
        for (int variable = 0; variable < variables; ++variable) {
            double& coordinate = (variable % 2 == 0)
                ? perturbed[static_cast<std::size_t>(variable / 2)].x_m
                : perturbed[static_cast<std::size_t>(variable / 2)].y_m;
            coordinate += step_m;
            const double plus = evaluate_once(perturbed, false).aep_gwh;
            coordinate -= 2.0 * step_m;
            const double minus = evaluate_once(perturbed, false).aep_gwh;
            coordinate += step_m;
            result.gradient_gwh_per_m[static_cast<std::size_t>(variable)] =
                (plus - minus) / (2.0 * step_m);
        }
        result.physical_layout_evaluations = static_cast<std::uint64_t>(
            2 * variables + 1
        );
    }
    const fode::ExecutorWorkReceipt work = executor.work_receipt();
    result.flow_cases = static_cast<std::uint64_t>(state_count)
        * result.physical_layout_evaluations;
    result.pair_interactions = result.flow_cases
        * static_cast<std::uint64_t>(count)
        * static_cast<std::uint64_t>(count - 1) / 2U;
    result.requested_workers = executor.thread_count();
    result.observed_workers = work.distinct_participants;
    result.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}

SmartStartReceipt Problem::smart_start(
    const int random_percent,
    const double grid_resolution_rotor_radii,
    const std::uint64_t seed,
    const int start_index,
    fode::PersistentExecutor& executor
) const {
    if (data_->config.family != ProblemFamily::iea37) {
        throw std::invalid_argument("T25 SMAST is defined for IEA-37 cases");
    }
    if (random_percent < 0 || random_percent > 100
        || !(grid_resolution_rotor_radii > 0.0)) {
        throw std::invalid_argument("invalid T25 SMAST configuration");
    }
    const auto started = Clock::now();
    const double step = .5 * data_->diameter_m
        * grid_resolution_rotor_radii;
    std::vector<Point> candidates;
    for (double x = -data_->boundary_radius_m;
         x <= data_->boundary_radius_m + .5 * step; x += step) {
        for (double y = -data_->boundary_radius_m;
             y <= data_->boundary_radius_m + .5 * step; y += step) {
            if (std::hypot(x, y) <= data_->boundary_radius_m + 1.0e-9) {
                candidates.push_back({x, y});
            }
        }
    }
    const int initial_grid_count = static_cast<int>(candidates.size());
    const int state_count = static_cast<int>(data_->states.size());
    std::vector<unsigned char> active(candidates.size(), 1U);
    std::vector<double> sum_squares(
        candidates.size() * static_cast<std::size_t>(state_count), 0.0
    );
    std::vector<double> scores(candidates.size(), 0.0);
    std::vector<Point> layout;
    layout.reserve(static_cast<std::size_t>(data_->config.turbine_count));
    std::uint64_t updates = 0;
    executor.reset_work_receipt();
    for (int placed = 0; placed < data_->config.turbine_count; ++placed) {
        if (placed > 0 && random_percent < 100) {
            const Point source = layout.back();
            executor.parallel_for(
                0, static_cast<int>(candidates.size()),
                [&](const int candidate_index) {
                    if (!active[static_cast<std::size_t>(candidate_index)]) return;
                    const Point target = candidates[static_cast<std::size_t>(candidate_index)];
                    double score = 0.0;
                    for (int state_index = 0; state_index < state_count; ++state_index) {
                        const FlowState& state = data_->states[
                            static_cast<std::size_t>(state_index)
                        ];
                        const double dx = target.x_m - source.x_m;
                        const double dy = target.y_m - source.y_m;
                        const PairTerms terms = pair_terms(
                            -dx * state.sine - dy * state.cosine,
                            dx * state.cosine - dy * state.sine,
                            state.speed_mps,
                            state.ct,
                            data_->diameter_m,
                            true
                        );
                        double& sum = sum_squares[
                            static_cast<std::size_t>(candidate_index * state_count + state_index)
                        ];
                        sum += terms.deficit_mps * terms.deficit_mps;
                        const double effective = std::max(
                            0.0, state.speed_mps - std::sqrt(sum)
                        );
                        score += iea_power(effective).power_mw
                            * kHoursPerYear * state.probability / 1000.0;
                    }
                    scores[static_cast<std::size_t>(candidate_index)] = score;
                }
            );
            updates += static_cast<std::uint64_t>(state_count)
                * static_cast<std::uint64_t>(std::count(
                    active.begin(), active.end(), static_cast<unsigned char>(1U)
                ));
        }
        std::vector<int> eligible;
        eligible.reserve(candidates.size());
        if (random_percent == 100 || placed == 0) {
            for (std::size_t index = 0; index < active.size(); ++index) {
                if (active[index]) eligible.push_back(static_cast<int>(index));
            }
        } else {
            std::vector<double> valid_scores;
            for (std::size_t index = 0; index < active.size(); ++index) {
                if (active[index]) valid_scores.push_back(scores[index]);
            }
            if (valid_scores.empty()) {
                throw std::runtime_error("T25 SMAST exhausted candidate grid");
            }
            std::sort(valid_scores.begin(), valid_scores.end());
            const double percentile = 100.0 - static_cast<double>(random_percent);
            const double rank = percentile / 100.0
                * static_cast<double>(valid_scores.size() - 1U);
            const std::size_t lower = static_cast<std::size_t>(std::floor(rank));
            const std::size_t upper = static_cast<std::size_t>(std::ceil(rank));
            const double threshold = std::lerp(
                valid_scores[lower], valid_scores[upper], rank - std::floor(rank)
            );
            for (std::size_t index = 0; index < active.size(); ++index) {
                if (active[index] && scores[index] >= threshold - 1.0e-12) {
                    eligible.push_back(static_cast<int>(index));
                }
            }
        }
        if (eligible.empty()) {
            throw std::runtime_error("T25 SMAST has no eligible point");
        }
        const std::uint64_t choice_bits = mix64(
            seed ^ mix64(static_cast<std::uint64_t>(start_index))
            ^ mix64(static_cast<std::uint64_t>(placed))
        );
        const int selected = eligible[
            static_cast<std::size_t>(choice_bits % eligible.size())
        ];
        const Point point = candidates[static_cast<std::size_t>(selected)];
        layout.push_back(point);
        const double minimum_spacing = kMinimumSpacingDiameters
            * data_->diameter_m;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            if (active[index] && std::hypot(
                candidates[index].x_m - point.x_m,
                candidates[index].y_m - point.y_m
            ) < minimum_spacing - 1.0e-9) {
                active[index] = 0U;
            }
        }
    }
    const Evaluation final_evaluation = evaluate(
        layout, GradientMode::none, executor
    );
    const fode::ExecutorWorkReceipt work = executor.work_receipt();
    SmartStartReceipt receipt;
    receipt.layout = std::move(layout);
    receipt.aep_gwh = final_evaluation.aep_gwh;
    receipt.minimum_spacing_m = minimum_spacing(receipt.layout);
    receipt.grid_points_initial = initial_grid_count;
    receipt.grid_points_remaining = static_cast<int>(std::count(
        active.begin(), active.end(), static_cast<unsigned char>(1U)
    ));
    receipt.random_percent = random_percent;
    receipt.grid_resolution_rotor_radii = grid_resolution_rotor_radii;
    receipt.candidate_flow_updates = updates;
    receipt.requested_workers = executor.thread_count();
    receipt.observed_workers = std::max(
        work.distinct_participants, final_evaluation.observed_workers
    );
    receipt.seconds = std::chrono::duration<double>(Clock::now() - started).count();
    return receipt;
}

namespace {

struct ObjectiveContext {
    const Problem* problem = nullptr;
    fode::PersistentExecutor* executor = nullptr;
    int objective_calls = 0;
    int gradient_calls = 0;
    std::uint64_t physical_layout_evaluations = 0;
    int observed_workers = 0;
    double evaluator_seconds = 0.0;
};

double objective_callback(
    const unsigned variables,
    const double* values,
    double* gradient,
    void* raw_context
) {
    auto& context = *static_cast<ObjectiveContext*>(raw_context);
    const int count = static_cast<int>(variables / 2U);
    const std::vector<Point> layout = unflatten(values, count);
    const GradientMode mode = gradient == nullptr
        ? GradientMode::none : GradientMode::exact_reverse;
    const Evaluation evaluation = context.problem->evaluate(
        layout, mode, *context.executor
    );
    ++context.objective_calls;
    context.gradient_calls += gradient == nullptr ? 0 : 1;
    context.physical_layout_evaluations += evaluation.physical_layout_evaluations;
    context.observed_workers = std::max(
        context.observed_workers, evaluation.observed_workers
    );
    context.evaluator_seconds += evaluation.seconds;
    if (gradient != nullptr) {
        for (unsigned index = 0; index < variables; ++index) {
            gradient[index] = -evaluation.gradient_gwh_per_m[index] / 10.0;
        }
    }
    return -evaluation.aep_gwh / 10.0;
}

struct CircleConstraintContext {
    int calls = 0;
    int count = 0;
    double radius_m = 0.0;
};

void circle_constraint_callback(
    const unsigned constraints,
    double* result,
    const unsigned variables,
    const double* values,
    double* gradient,
    void* raw_context
) {
    auto& context = *static_cast<CircleConstraintContext*>(raw_context);
    ++context.calls;
    if (constraints != static_cast<unsigned>(context.count)
        || variables != static_cast<unsigned>(2 * context.count)) {
        throw std::runtime_error("T25 circle constraint dimension mismatch");
    }
    if (gradient != nullptr) {
        std::fill(
            gradient,
            gradient + static_cast<std::size_t>(constraints * variables),
            0.0
        );
    }
    for (int turbine = 0; turbine < context.count; ++turbine) {
        const double x = values[2 * turbine];
        const double y = values[2 * turbine + 1];
        const double radius = std::hypot(x, y);
        result[turbine] = radius - context.radius_m;
        if (gradient != nullptr && radius > 1.0e-15) {
            double* row = gradient
                + static_cast<std::size_t>(turbine * variables);
            row[2 * turbine] = x / radius;
            row[2 * turbine + 1] = y / radius;
        }
    }
}

}  // namespace

OptimizationReceipt optimize(
    const Problem& problem,
    const OptimizationConfig& config
) {
    if (problem.config().family != ProblemFamily::iea37) {
        throw std::invalid_argument("T25 target optimization is IEA-37 SMAST-SLSQP");
    }
    if (config.workers < 1 || config.maximum_evaluations < 1) {
        throw std::invalid_argument("invalid T25 optimization configuration");
    }
    const auto run_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    const auto initialization_started = Clock::now();
    std::vector<Point> layout;
    if (config.use_smart_start) {
        layout = problem.smart_start(
            config.random_percent,
            config.grid_resolution_rotor_radii,
            config.seed,
            config.start_index,
            executor
        ).layout;
    } else {
        layout = problem.random_feasible_layout(config.seed, config.start_index);
    }
    const double initialization_seconds = std::chrono::duration<double>(
        Clock::now() - initialization_started
    ).count();
    const Evaluation initial = problem.evaluate(
        layout, GradientMode::none, executor
    );
    std::vector<double> values = flatten(layout);
    const unsigned variables = static_cast<unsigned>(values.size());
    const int count = static_cast<int>(layout.size());
    std::vector<double> lower(values.size(), -problem.boundary_radius_m());
    std::vector<double> upper(values.size(), problem.boundary_radius_m());
    std::vector<double> tolerances(static_cast<std::size_t>(count), 1.0e-7);
    ObjectiveContext objective_context;
    objective_context.problem = &problem;
    objective_context.executor = &executor;
    CircleConstraintContext constraint_context;
    constraint_context.count = count;
    constraint_context.radius_m = problem.boundary_radius_m();
    nlopt_opt optimizer = nlopt_create(NLOPT_LD_SLSQP, variables);
    if (optimizer == nullptr) throw std::runtime_error("failed to create T25 SLSQP");
    nlopt_result status = NLOPT_FAILURE;
    double minimum = 0.0;
    const auto optimizer_started = Clock::now();
    try {
        if (nlopt_set_lower_bounds(optimizer, lower.data()) < 0
            || nlopt_set_upper_bounds(optimizer, upper.data()) < 0
            || nlopt_set_min_objective(
                optimizer, objective_callback, &objective_context
            ) < 0
            || nlopt_add_inequality_mconstraint(
                optimizer,
                static_cast<unsigned>(count),
                circle_constraint_callback,
                &constraint_context,
                tolerances.data()
            ) < 0
            || nlopt_set_xtol_rel(optimizer, config.relative_x_tolerance) < 0
            || nlopt_set_maxeval(optimizer, config.maximum_evaluations) < 0) {
            throw std::runtime_error("failed to configure T25 SLSQP");
        }
        status = nlopt_optimize(optimizer, values.data(), &minimum);
    } catch (...) {
        nlopt_destroy(optimizer);
        throw;
    }
    nlopt_destroy(optimizer);
    const double optimizer_seconds = std::chrono::duration<double>(
        Clock::now() - optimizer_started
    ).count();
    std::vector<Point> candidate = unflatten(values.data(), count);
    if (problem.maximum_boundary_violation(candidate) <= 1.0e-3) {
        layout = std::move(candidate);
    }
    const Evaluation final = problem.evaluate(
        layout, GradientMode::none, executor
    );
    OptimizationReceipt receipt;
    receipt.problem_semantic_id = problem.semantic_id();
    receipt.method_semantic_id = "t25_smast_slsqp_exact_reverse_v1";
    receipt.turbine_count = count;
    receipt.direction_count = problem.config().direction_count;
    receipt.speed_count = problem.config().speed_count;
    receipt.requested_workers = config.workers;
    receipt.observed_workers = std::max(
        objective_context.observed_workers,
        std::max(initial.observed_workers, final.observed_workers)
    );
    receipt.seed = config.seed;
    receipt.start_index = config.start_index;
    receipt.random_percent = config.random_percent;
    receipt.optimizer_status = static_cast<int>(status);
    receipt.optimizer_status_name = nlopt_status_name(status);
    receipt.objective_calls = objective_context.objective_calls;
    receipt.gradient_calls = objective_context.gradient_calls;
    receipt.constraint_calls = constraint_context.calls;
    receipt.physical_layout_evaluations =
        objective_context.physical_layout_evaluations + 2U;
    receipt.initial_aep_gwh = initial.aep_gwh;
    receipt.final_aep_gwh = final.aep_gwh;
    receipt.minimum_spacing_m = problem.minimum_spacing(layout);
    receipt.maximum_boundary_violation_m =
        problem.maximum_boundary_violation(layout);
    receipt.initialization_seconds = initialization_seconds;
    receipt.evaluator_seconds = objective_context.evaluator_seconds
        + initial.seconds + final.seconds;
    receipt.optimizer_seconds = optimizer_seconds;
    receipt.end_to_end_seconds = std::chrono::duration<double>(
        Clock::now() - run_started
    ).count();
    receipt.scientific_hash = scientific_hash(layout, final.aep_gwh);
    receipt.final_layout = std::move(layout);
    return receipt;
}

}  // namespace core99::t25
