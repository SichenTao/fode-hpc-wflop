/*
WFLOP IMPLEMENTATION FACT DECLARATION

Implementation unit: TAAE structured 3D energy-noise problem proxy and formula fixture
Paper: Transformer Autoencoder-Assisted Evolutionary Framework for Constrained
Multiobjective 3D Wind Farm Layout Optimization
Paper DOI: 10.1109/JAS.2026.126233
Paper SHA-256: 243dd96dfa94a3d596f375a6c62e58015c735171958778d816b6afdbf99cd35b
Paper provides: 20 by 20 grid, 15 turbines, two noise-monitor
configurations, budgets 0.6e6/0.8e6/1.0e6, reciprocal-power and mean
A-weighted-noise objectives, and supplementary Eqs. (1)-(20).
Public author problem code/data URL: none found by the bounded search recorded
in docs/source-dossiers/Y36.json.
Missing original inputs: Zhangbei elevation array, eight numerical joint-wind
arrays, monitor coordinates, blade spectra and propagation constants, land
unit cost, and reference fronts.
Project completion: use the analytic terrain, frozen joint-wind distribution,
figure-structured monitor cells, explicit three-band acoustic proxy, and cost
scale in shared/contracts/taae_zhangbei_structured_declared_proxy_cases.json.
Problem evidence tier: P3_DECLARED_PROXY.
Formula fixture: taae_formula_fixture_v1 at P4_FORMULA_FIXTURE. It evaluates
supplementary scalar equations using declared fixture constants and is not an
optimization problem.
Problem semantic ID: taae_zhangbei_structured_declared_proxy_v1.
Step 11 multiplicative-wake probing is a sensitivity-only independent problem
semantic. Baseline semantics and semantic hash remain unchanged; distinct
problem semantics are never pooled or used for cross-semantic ranking.
Controlling contracts:
shared/contracts/taae_zhangbei_structured_declared_proxy_contract.json and
shared/contracts/taae_formula_fixture_contract.json.
Backend: a configured deterministic persistent CPU team assigns independent
complete layouts; actual distinct worker participation is not reported. Every
within-layout floating-point reduction has a fixed order.
Multi-wake completion: apply target-point terrain shear once, combine
paper-equation pairwise absolute wake deficits by fixed-order root-sum-square,
then subtract that absolute deficit from the target ambient velocity.
Full problem-semantic hash: versioned FNV-1a over every objective/feasibility
constant, rule tag, budget, and case array; changing a scientific constant
requires deliberate regeneration of all six frozen hashes.
Physical FES: one complete evaluation of reciprocal expected power,
multi-state average A-weighted noise, total cost, and normalized cost violation.
Claim boundary: neither the proxy nor its fixture reproduces the unavailable
Zhangbei numerical cases or reported Pareto fronts.
END WFLOP IMPLEMENTATION FACT DECLARATION
*/

#include "wflop/taae_problem.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace wflop::taae {
namespace {

using Clock = std::chrono::steady_clock;

constexpr char kSemanticHashVersion[] =
    "taae_proxy_full_problem_semantics_hash_v2";
constexpr double kTerrainBaseM = 50.0;
constexpr double kTerrainWaveAmplitudeM = 35.0;
constexpr double kTerrainTrendAmplitudeM = 15.0;
constexpr double kTerrainGridSpan = 19.0;
constexpr double kTerrainTrendDivisor = 2.0;
constexpr double kCellCenterOffset = 0.5;
constexpr double kHubHeightM = 80.0;
constexpr double kRotorRadiusM = 38.5;
constexpr double kThrustCoefficient = 0.8;
constexpr double kTurbulenceIntensity = 0.1;
constexpr double kWindShearExponent = 0.14;
constexpr double kMinimumShearBase = 1.0e-6;
constexpr double kTerrainReferenceM = 50.0;
constexpr double kWakeVerticalCoefficient = 0.243346;
constexpr double kWakeVerticalCtExponent = 0.4297;
constexpr double kWakeVerticalTiExponent = 0.4707;
constexpr double kWakeHorizontalCoefficient = 0.18265;
constexpr double kWakeHorizontalCtExponent = 0.2566;
constexpr double kWakeHorizontalTiExponent = 0.2808;
constexpr double kGaussianRadiusDivisor = 2.58;
constexpr double kPowerCutInMps = 2.0;
constexpr double kPowerRatedStartMps = 12.8;
constexpr double kPowerCutOutMps = 18.0;
constexpr double kPowerCubicCoefficient = 0.3;
constexpr double kPowerRatedKw = 629.1;
constexpr double kTurbineCostScale = 10000.0;
constexpr double kLandCostPerSquareMetre = 0.005;
constexpr double kTurbineCostExponent = 0.00174;
constexpr double kTurbineCostFixedFraction = 2.0 / 3.0;
constexpr double kTurbineCostVariableFraction = 1.0 / 3.0;
constexpr double kEffectiveAreaBlend = 0.5;
constexpr double kReferenceWindSpeed = 8.0;
constexpr double kInflowVelocityDbExponent = 20.0;
constexpr double kTrailingVelocityDbExponent = 50.0;
constexpr double kMonitorHeightM = 2.0;
constexpr double kMinimumAcousticDistanceM = 1.0;
constexpr std::array<double, 3> kAcousticBandsHz{
    250.0, 1000.0, 4000.0
};
constexpr std::array<double, 3> kAWeightingDb{-8.6, 0.0, 1.0};
constexpr std::array<double, 3> kAirAbsorptionDbPerM{
    0.0001, 0.0004, 0.0012
};
constexpr std::array<double, 3> kInflowReferenceDb{
    94.0, 91.0, 84.0
};
constexpr std::array<double, 3> kTrailingReferenceDb{
    88.0, 94.0, 89.0
};

struct Point {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ProxyIdentity {
    const char* case_id;
    const char* semantic_hash;
    double budget;
};

constexpr std::array<ProxyIdentity, 6> kProxyIdentities{{
    {
        "TAAE_Proxy_NC1_Budget600k_tn15",
        "fnv1a64:ffdc187163fb6d97",
        600000.0
    },
    {
        "TAAE_Proxy_NC1_Budget800k_tn15",
        "fnv1a64:d2e0cb41e02ce9c4",
        800000.0
    },
    {
        "TAAE_Proxy_NC1_Budget1000k_tn15",
        "fnv1a64:6e9505ccedf0a578",
        1000000.0
    },
    {
        "TAAE_Proxy_NC2_Budget600k_tn15",
        "fnv1a64:369f421c373bedec",
        600000.0
    },
    {
        "TAAE_Proxy_NC2_Budget800k_tn15",
        "fnv1a64:25c369241a20016f",
        800000.0
    },
    {
        "TAAE_Proxy_NC2_Budget1000k_tn15",
        "fnv1a64:47a7700e18311e81",
        1000000.0
    }
}};

const ProxyIdentity& identity_for(const std::string& case_id) {
    const auto found = std::find_if(
        kProxyIdentities.begin(),
        kProxyIdentities.end(),
        [&](const ProxyIdentity& identity) {
            return case_id == identity.case_id;
        }
    );
    if (found == kProxyIdentities.end()) {
        throw std::invalid_argument(
            "case is not in taae_zhangbei_structured_declared_proxy_v1: "
            + case_id
        );
    }
    return *found;
}

double terrain_height(int row, int column) {
    const double x =
        static_cast<double>(column) / kTerrainGridSpan;
    const double y =
        static_cast<double>(row) / kTerrainGridSpan;
    return kTerrainBaseM
        + kTerrainWaveAmplitudeM
            * std::sin(2.0 * std::numbers::pi * x)
            * std::cos(2.0 * std::numbers::pi * y)
        + kTerrainTrendAmplitudeM
            * (x + y) / kTerrainTrendDivisor;
}

Point cell_point(const fode::CaseData& data, int cell_1based) {
    const int cell = cell_1based - 1;
    const int row = cell / data.cols;
    const int column = cell - row * data.cols;
    return Point{
        (static_cast<double>(column) + kCellCenterOffset)
            * data.cell_width,
        (static_cast<double>(row) + kCellCenterOffset)
            * data.cell_width,
        terrain_height(row, column)
    };
}

double turbine_power_kw(double velocity) {
    if (velocity < kPowerCutInMps) {
        return 0.0;
    }
    if (velocity < kPowerRatedStartMps) {
        return kPowerCubicCoefficient
            * velocity * velocity * velocity;
    }
    if (velocity <= kPowerCutOutMps) {
        return kPowerRatedKw;
    }
    return 0.0;
}

double axial_induction(double thrust_coefficient) {
    return (
        1.0 - std::sqrt(1.0 - thrust_coefficient)
    ) / 2.0;
}

double initial_wake_radius(double rotor_radius, double induction) {
    return rotor_radius * std::sqrt(
        (1.0 - induction) / (1.0 - 2.0 * induction)
    );
}

double ambient_shear(double vertical_displacement) {
    return std::pow(
        std::max(
            (vertical_displacement + kHubHeightM) / kHubHeightM,
            kMinimumShearBase
        ),
        kWindShearExponent
    );
}

struct PairWakeTerms {
    double shear = 1.0;
    double fractional_wake_deficit = 0.0;
};

PairWakeTerms pair_wake_terms_eq7(
    double downstream_distance,
    double crosswind_distance,
    double vertical_displacement
) {
    if (downstream_distance <= 0.0) {
        return PairWakeTerms{
            ambient_shear(vertical_displacement),
            0.0
        };
    }
    const double induction = axial_induction(kThrustCoefficient);
    const double radius0 =
        initial_wake_radius(kRotorRadiusM, induction);
    const double kz = kWakeVerticalCoefficient
        * std::pow(
            kThrustCoefficient,
            kWakeVerticalCtExponent
        )
        * std::pow(
            kTurbulenceIntensity,
            kWakeVerticalTiExponent
        );
    const double ky = kWakeHorizontalCoefficient
        * std::pow(
            kThrustCoefficient,
            kWakeHorizontalCtExponent
        )
        * std::pow(
            kTurbulenceIntensity,
            kWakeHorizontalTiExponent
        );
    const double rz = kz * downstream_distance + radius0;
    const double ry = ky * downstream_distance + radius0;
    const double sigma_z = rz / kGaussianRadiusDivisor;
    const double sigma_y = ry / kGaussianRadiusDivisor;
    const double shear = ambient_shear(vertical_displacement);
    const double gaussian = std::exp(
        -vertical_displacement * vertical_displacement
            / (2.0 * sigma_z * sigma_z)
        -crosswind_distance * crosswind_distance
            / (2.0 * sigma_y * sigma_y)
    );
    const double first_deficit =
        4.0 * induction * radius0 * radius0
        / (
            sigma_z * rz * std::sqrt(2.0 * std::numbers::pi)
        ) * gaussian;
    const double shear_integral =
        kHubHeightM / (kWindShearExponent + 1.0)
        * (
            std::pow(
                (kHubHeightM + radius0) / kHubHeightM,
                kWindShearExponent + 1.0
            )
            - std::pow(
                (kHubHeightM - radius0) / kHubHeightM,
                kWindShearExponent + 1.0
            )
        )
        - 2.0 * radius0;
    const double second_deficit =
        induction / rz
        * std::exp(
            -crosswind_distance * crosswind_distance
                / (2.0 * sigma_y * sigma_y)
        )
        * shear_integral;
    return PairWakeTerms{
        shear,
        std::max(first_deficit + second_deficit, 0.0)
    };
}

double wake_velocity_eq7(
    double free_velocity,
    double downstream_distance,
    double crosswind_distance,
    double vertical_displacement
) {
    const PairWakeTerms terms = pair_wake_terms_eq7(
        downstream_distance,
        crosswind_distance,
        vertical_displacement
    );
    return std::max(
        free_velocity
            * (terms.shear - terms.fractional_wake_deficit),
        0.0
    );
}

double combine_decibels(double first, double second) {
    return 10.0 * std::log10(
        std::pow(10.0, first / 10.0)
        + std::pow(10.0, second / 10.0)
    );
}

double inflow_spl_eq9(
    double blades,
    double sine_squared_phi,
    double density,
    double chord_at_07_radius,
    double rotor_radius,
    double turbulent_velocity_squared,
    double blade_forward_speed_at_07_radius,
    double distance,
    double sound_speed,
    double spectral_correction
) {
    const double numerator =
        blades * sine_squared_phi * density * density
        * chord_at_07_radius * rotor_radius
        * turbulent_velocity_squared
        * std::pow(blade_forward_speed_at_07_radius, 4.0);
    const double denominator =
        distance * distance * sound_speed * sound_speed;
    return 10.0 * std::log10(numerator / denominator)
        + spectral_correction;
}

double trailing_spl_eq10(
    double local_velocity,
    double blades,
    double directivity,
    double boundary_layer_thickness,
    double span,
    double distance,
    double strouhal,
    double strouhal_maximum,
    double scaling_db
) {
    const double ratio = strouhal / strouhal_maximum;
    const double spectrum =
        std::pow(ratio, 4.0)
        * std::pow(std::pow(ratio, 1.5) + 0.5, -4.0);
    const double energy =
        std::pow(local_velocity, 5.0)
        * blades * directivity * boundary_layer_thickness * span
        / (distance * distance) * spectrum;
    return 10.0 * std::log10(energy) + scaling_db;
}

std::vector<double> state_velocities(
    const std::vector<Point>& turbines,
    double direction,
    double free_velocity,
    WakeCombination wake_combination =
        WakeCombination::root_sum_square
) {
    const int count = static_cast<int>(turbines.size());
    std::vector<double> along(static_cast<std::size_t>(count));
    std::vector<double> cross(static_cast<std::size_t>(count));
    std::vector<int> order(static_cast<std::size_t>(count));
    const double cosine = std::cos(direction);
    const double sine = std::sin(direction);
    for (int index = 0; index < count; ++index) {
        const Point& point = turbines[static_cast<std::size_t>(index)];
        along[static_cast<std::size_t>(index)] =
            cosine * point.x + sine * point.y;
        cross[static_cast<std::size_t>(index)] =
            -sine * point.x + cosine * point.y;
        order[static_cast<std::size_t>(index)] = index;
    }
    std::stable_sort(order.begin(), order.end(), [&](int left, int right) {
        return along[static_cast<std::size_t>(left)]
            < along[static_cast<std::size_t>(right)];
    });
    std::vector<double> velocities(
        static_cast<std::size_t>(count),
        free_velocity
    );
    for (int downstream_position = 0;
         downstream_position < count;
         ++downstream_position) {
        const int downstream =
            order[static_cast<std::size_t>(downstream_position)];
        double squared_deficit = 0.0;
        double multiplicative_factor = 1.0;
        for (int upstream_position = 0;
             upstream_position < downstream_position;
             ++upstream_position) {
            const int upstream =
                order[static_cast<std::size_t>(upstream_position)];
            const double dx =
                along[static_cast<std::size_t>(downstream)]
                - along[static_cast<std::size_t>(upstream)];
            const double dy =
                cross[static_cast<std::size_t>(downstream)]
                - cross[static_cast<std::size_t>(upstream)];
            const double dz =
                turbines[static_cast<std::size_t>(downstream)].z
                - turbines[static_cast<std::size_t>(upstream)].z;
            const PairWakeTerms pair =
                pair_wake_terms_eq7(dx, dy, dz);
            const double deficit = std::max(
                pair.fractional_wake_deficit,
                0.0
            );
            squared_deficit += deficit * deficit;
            multiplicative_factor *= std::max(0.0, 1.0 - deficit);
        }
        const double relative_terrain =
            turbines[static_cast<std::size_t>(downstream)].z
            - kTerrainReferenceM;
        const double shear = ambient_shear(relative_terrain);
        velocities[static_cast<std::size_t>(downstream)] =
            wake_combination == WakeCombination::root_sum_square
            ? std::max(
                free_velocity
                    * (shear - std::sqrt(squared_deficit)),
                0.0
            )
            : free_velocity * shear * multiplicative_factor;
    }
    return velocities;
}

double proxy_source_level(int band, double velocity) {
    const double ratio =
        std::max(velocity, kPowerCutInMps) / kReferenceWindSpeed;
    const double inflow =
        kInflowReferenceDb[static_cast<std::size_t>(band)]
        + kInflowVelocityDbExponent * std::log10(ratio);
    const double trailing =
        kTrailingReferenceDb[static_cast<std::size_t>(band)]
        + kTrailingVelocityDbExponent * std::log10(ratio);
    return combine_decibels(inflow, trailing);
}

double state_noise(
    const std::vector<Point>& turbines,
    const std::vector<Point>& monitors,
    const std::vector<double>& velocities
) {
    double average = 0.0;
    for (const Point& monitor : monitors) {
        double energy = 0.0;
        for (std::size_t turbine = 0;
             turbine < turbines.size();
             ++turbine) {
            const Point& source = turbines[turbine];
            const double dx = source.x - monitor.x;
            const double dy = source.y - monitor.y;
            const double dz =
                source.z + kHubHeightM
                - (monitor.z + kMonitorHeightM);
            const double distance = std::max(
                std::sqrt(dx * dx + dy * dy + dz * dz),
                kMinimumAcousticDistanceM
            );
            for (int band = 0; band < 3; ++band) {
                const double attenuation =
                    20.0 * std::log10(distance)
                    + kAirAbsorptionDbPerM[
                        static_cast<std::size_t>(band)
                    ] * distance;
                const double weighted =
                    proxy_source_level(
                        band,
                        velocities[turbine]
                    )
                    - attenuation
                    + kAWeightingDb[static_cast<std::size_t>(band)];
                energy += std::pow(10.0, weighted / 10.0);
            }
        }
        average += 10.0 * std::log10(
            std::max(energy, std::numeric_limits<double>::min())
        );
    }
    return average / static_cast<double>(monitors.size());
}

double cross_2d(const Point& origin, const Point& a, const Point& b) {
    return (a.x - origin.x) * (b.y - origin.y)
        - (a.y - origin.y) * (b.x - origin.x);
}

double effective_land_area(const std::vector<Point>& points) {
    std::vector<Point> sorted = points;
    std::stable_sort(
        sorted.begin(),
        sorted.end(),
        [](const Point& left, const Point& right) {
            if (left.x != right.x) {
                return left.x < right.x;
            }
            return left.y < right.y;
        }
    );
    std::vector<Point> hull;
    for (const Point& point : sorted) {
        while (hull.size() >= 2
               && cross_2d(
                   hull[hull.size() - 2],
                   hull.back(),
                   point
               ) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const std::size_t lower_size = hull.size();
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        while (hull.size() > lower_size
               && cross_2d(
                   hull[hull.size() - 2],
                   hull.back(),
                   *it
               ) <= 0.0) {
            hull.pop_back();
        }
        hull.push_back(*it);
    }
    if (!hull.empty()) {
        hull.pop_back();
    }
    double shoelace = 0.0;
    for (std::size_t index = 0; index < hull.size(); ++index) {
        const Point& current = hull[index];
        const Point& next = hull[(index + 1) % hull.size()];
        shoelace += current.x * next.y - next.x * current.y;
    }
    const double convex_area = 0.5 * std::abs(shoelace);
    const auto [min_x, max_x] = std::minmax_element(
        points.begin(),
        points.end(),
        [](const Point& left, const Point& right) {
            return left.x < right.x;
        }
    );
    const auto [min_y, max_y] = std::minmax_element(
        points.begin(),
        points.end(),
        [](const Point& left, const Point& right) {
            return left.y < right.y;
        }
    );
    const double rectangle =
        (max_x->x - min_x->x) * (max_y->y - min_y->y);
    return kEffectiveAreaBlend * (convex_area + rectangle);
}

double total_cost(const std::vector<Point>& turbines) {
    const double count = static_cast<double>(turbines.size());
    const double turbine_cost = kTurbineCostScale * count * (
        kTurbineCostFixedFraction
        + kTurbineCostVariableFraction
            * std::exp(-kTurbineCostExponent * count * count)
    );
    return turbine_cost
        + kLandCostPerSquareMetre * effective_land_area(turbines);
}

CompleteEvaluation evaluate_layout(
    const int* layout,
    const fode::CaseData& data,
    double budget,
    WakeCombination wake_combination
) {
    const int dimension = data.turbine_count;
    std::vector<char> occupied(
        static_cast<std::size_t>(data.rows * data.cols),
        0
    );
    for (const int blocked : data.unavailable_cells_1based) {
        occupied[static_cast<std::size_t>(blocked - 1)] = 1;
    }
    std::vector<Point> turbines;
    turbines.reserve(static_cast<std::size_t>(dimension));
    int previous = 0;
    for (int coordinate = 0; coordinate < dimension; ++coordinate) {
        const int cell = layout[coordinate];
        if (cell <= previous
            || cell < 1
            || cell > data.rows * data.cols
            || occupied[static_cast<std::size_t>(cell - 1)] != 0) {
            throw std::invalid_argument(
                "TAAE proxy requires sorted unique feasible layouts"
            );
        }
        occupied[static_cast<std::size_t>(cell - 1)] = 1;
        turbines.push_back(cell_point(data, cell));
        previous = cell;
    }
    std::vector<Point> monitors;
    monitors.reserve(data.unavailable_cells_1based.size());
    for (const int cell : data.unavailable_cells_1based) {
        monitors.push_back(cell_point(data, cell));
    }
    double expected_power = 0.0;
    double expected_noise = 0.0;
    const int speed_count = static_cast<int>(data.velocity.size());
    for (std::size_t direction = 0;
         direction < data.theta.size();
         ++direction) {
        for (int speed = 0; speed < speed_count; ++speed) {
            const std::size_t state =
                direction * static_cast<std::size_t>(speed_count)
                + static_cast<std::size_t>(speed);
            const double probability = data.probability[state];
            const auto velocities = state_velocities(
                turbines,
                data.theta[direction],
                data.velocity[static_cast<std::size_t>(speed)],
                wake_combination
            );
            for (const double velocity : velocities) {
                expected_power += probability
                    * turbine_power_kw(velocity);
            }
            expected_noise += probability
                * state_noise(turbines, monitors, velocities);
        }
    }
    const double cost = total_cost(turbines);
    return CompleteEvaluation{
        1.0 / std::max(
            expected_power,
            std::numeric_limits<double>::min()
        ),
        expected_power,
        expected_noise,
        cost,
        std::max(0.0, cost - budget) / budget
    };
}

}  // namespace

std::string structured_proxy_semantic_hash(
    const fode::CaseData& data,
    WakeCombination wake_combination
) {
    std::uint64_t hash = 14695981039346656037ULL;
    auto mix_byte = [&](std::uint8_t value) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ULL;
    };
    auto mix_u64 = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            mix_byte(static_cast<std::uint8_t>(value & 0xffULL));
            value >>= 8;
        }
    };
    auto mix_double = [&](double value) {
        mix_u64(std::bit_cast<std::uint64_t>(value));
    };
    auto mix_string = [&](const std::string& value) {
        mix_u64(static_cast<std::uint64_t>(value.size()));
        for (const unsigned char byte : value) {
            mix_byte(byte);
        }
    };
    mix_string(kSemanticHashVersion);
    mix_string(
        "terrain=base+wave*sin(2pi*x)*cos(2pi*y)"
        "+trend*(x+y)/divisor;cell=grid_center"
    );
    mix_double(kTerrainBaseM);
    mix_double(kTerrainWaveAmplitudeM);
    mix_double(kTerrainTrendAmplitudeM);
    mix_double(kTerrainGridSpan);
    mix_double(kTerrainTrendDivisor);
    mix_double(kCellCenterOffset);
    mix_double(kHubHeightM);
    mix_double(kRotorRadiusM);
    mix_double(kThrustCoefficient);
    mix_double(kTurbulenceIntensity);
    mix_double(kWindShearExponent);
    mix_double(kMinimumShearBase);
    mix_double(kTerrainReferenceM);
    mix_double(kWakeVerticalCoefficient);
    mix_double(kWakeVerticalCtExponent);
    mix_double(kWakeVerticalTiExponent);
    mix_double(kWakeHorizontalCoefficient);
    mix_double(kWakeHorizontalCtExponent);
    mix_double(kWakeHorizontalTiExponent);
    mix_double(kGaussianRadiusDivisor);
    mix_string(
        "multiwake=ambient_target_shear_once_minus_"
        "rss_absolute_pair_deficits;ordered_reduction"
    );
    mix_double(kPowerCutInMps);
    mix_double(kPowerRatedStartMps);
    mix_double(kPowerCutOutMps);
    mix_double(kPowerCubicCoefficient);
    mix_double(kPowerRatedKw);
    mix_string(
        "noise=probability_weighted_monitor_mean_"
        "three_band_energy_A_weighting"
    );
    for (const double value : kAcousticBandsHz) {
        mix_double(value);
    }
    for (const double value : kAWeightingDb) {
        mix_double(value);
    }
    for (const double value : kAirAbsorptionDbPerM) {
        mix_double(value);
    }
    for (const double value : kInflowReferenceDb) {
        mix_double(value);
    }
    for (const double value : kTrailingReferenceDb) {
        mix_double(value);
    }
    mix_double(kReferenceWindSpeed);
    mix_double(kInflowVelocityDbExponent);
    mix_double(kTrailingVelocityDbExponent);
    mix_double(kMonitorHeightM);
    mix_double(kMinimumAcousticDistanceM);
    mix_string(
        "cost=turbine_scale*count*(fixed+variable*"
        "exp(-exponent*count^2))+land_scale*"
        "blend*(convex_hull+axis_rectangle)"
    );
    mix_double(kTurbineCostScale);
    mix_double(kLandCostPerSquareMetre);
    mix_double(kTurbineCostExponent);
    mix_double(kTurbineCostFixedFraction);
    mix_double(kTurbineCostVariableFraction);
    mix_double(kEffectiveAreaBlend);
    mix_string(
        "constraint=max(0,cost-budget)/budget;"
        "fes=complete_energy_noise_cost_constraint_layout"
    );
    mix_string(data.case_id);
    mix_double(identity_for(data.case_id).budget);
    mix_u64(static_cast<std::uint64_t>(data.rows));
    mix_u64(static_cast<std::uint64_t>(data.cols));
    mix_u64(static_cast<std::uint64_t>(data.turbine_count));
    mix_double(data.cell_width);
    mix_u64(static_cast<std::uint64_t>(data.theta.size()));
    for (const double value : data.theta) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(data.velocity.size()));
    for (const double value : data.velocity) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(data.probability.size()));
    for (const double value : data.probability) {
        mix_double(value);
    }
    mix_u64(static_cast<std::uint64_t>(
        data.unavailable_cells_1based.size()
    ));
    for (const int value : data.unavailable_cells_1based) {
        mix_u64(static_cast<std::uint64_t>(value));
    }
    if (wake_combination == WakeCombination::multiplicative) {
        mix_string(
            "sensitivity_problem_semantics=ambient_target_shear_once_"
            "times_product_one_minus_pair_fractional_wake_deficit"
        );
    }
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

BatchEvaluation evaluate_structured_proxy(
    const std::vector<int>& layouts_1based,
    int batch_size,
    const fode::CaseData& data,
    fode::PersistentExecutor& executor,
    WakeCombination wake_combination
) {
    if (batch_size <= 0
        || layouts_1based.size()
            != static_cast<std::size_t>(
                batch_size * data.turbine_count
            )) {
        throw std::invalid_argument(
            "invalid TAAE proxy population dimensions"
        );
    }
    if (data.rows != 20
        || data.cols != 20
        || data.turbine_count != 15
        || data.unavailable_cells_1based.size() != 4) {
        throw std::invalid_argument(
            "TAAE proxy manifest does not preserve 20x20/15/4 structure"
        );
    }
    const ProxyIdentity& identity = identity_for(data.case_id);
    const std::string observed_manifest_hash =
        structured_proxy_semantic_hash(
            data,
            WakeCombination::root_sum_square
        );
    if (observed_manifest_hash != identity.semantic_hash) {
        throw std::invalid_argument(
            "TAAE proxy full problem semantics do not match frozen hash: "
            + observed_manifest_hash
        );
    }
    BatchEvaluation result;
    result.values.resize(static_cast<std::size_t>(batch_size));
    result.complete_layout_evaluations =
        static_cast<std::uint64_t>(batch_size);
    result.configured_workers = executor.thread_count();
    result.problem_semantic_hash =
        structured_proxy_semantic_hash(data, wake_combination);
    const auto started = Clock::now();
    executor.parallel_for(0, batch_size, [&](int row) {
        result.values[static_cast<std::size_t>(row)] =
            evaluate_layout(
                layouts_1based.data()
                    + static_cast<std::ptrdiff_t>(
                        row * data.turbine_count
                    ),
                data,
                identity.budget,
                wake_combination
            );
    });
    result.elapsed_seconds =
        std::chrono::duration<double>(Clock::now() - started).count();
    return result;
}

void check_formula_fixture() {
    const double induction = axial_induction(0.8);
    if (std::abs(induction - 0.27639320225002106) > 1.0e-14) {
        throw std::runtime_error("TAAE Eq. 2 fixture failed");
    }
    if (std::abs(turbine_power_kw(8.0) - 153.6) > 1.0e-12) {
        throw std::runtime_error("TAAE main-text Eq. 3 fixture failed");
    }
    const double equal_sources = combine_decibels(40.0, 40.0);
    if (std::abs(equal_sources - 43.01029995663981) > 1.0e-12) {
        throw std::runtime_error("TAAE Eq. 11 fixture failed");
    }
    const double inflow = inflow_spl_eq9(
        3.0, 1.0, 1.225, 0.1, 38.5, 1.0,
        40.0, 100.0, 343.0, -8.6
    );
    if (std::abs(inflow - (-22.834941131444253)) > 1.0e-12) {
        throw std::runtime_error("TAAE Eq. 9 fixture failed");
    }
    const double trailing = trailing_spl_eq10(
        40.0, 3.0, 1.0, 0.01, 20.0,
        100.0, 0.2, 0.2, 0.0
    );
    if (std::abs(trailing - 30.840861708007306) > 1.0e-12) {
        throw std::runtime_error("TAAE Eq. 10 fixture failed");
    }
    const double count = 15.0;
    const double turbine_cost = count * (
        2.0 / 3.0
        + 1.0 / 3.0 * std::exp(-0.00174 * count * count)
    );
    if (std::abs(turbine_cost - 13.380210252473496) > 1.0e-12) {
        throw std::runtime_error("TAAE Eq. 14 fixture failed");
    }
    const double wake = wake_velocity_eq7(
        8.0, 500.0, 0.0, 0.0
    );
    if (!std::isfinite(wake) || wake < 0.0 || wake >= 8.0) {
        throw std::runtime_error("TAAE Eqs. 1-8 fixture failed");
    }
    const Point zero_wake_point{0.0, 0.0, 65.0};
    const std::vector<double> zero_wake = state_velocities(
        {zero_wake_point}, 0.0, 8.0
    );
    const double expected_terrain_speed =
        8.0 * ambient_shear(15.0);
    if (std::abs(zero_wake.front() - expected_terrain_speed)
        > 1.0e-13) {
        throw std::runtime_error(
            "TAAE zero-wake single-shear fixture failed"
        );
    }
    const PairWakeTerms far_crosswind =
        pair_wake_terms_eq7(500.0, 1.0e7, 15.0);
    if (far_crosswind.fractional_wake_deficit != 0.0) {
        throw std::runtime_error(
            "TAAE far-crosswind zero-deficit fixture failed"
        );
    }
    const Point upstream{0.0, 0.0, 50.0};
    const Point downstream{500.0, 0.0, 65.0};
    const std::vector<double> one_pair = state_velocities(
        {upstream, downstream}, 0.0, 8.0
    );
    const PairWakeTerms pair =
        pair_wake_terms_eq7(500.0, 0.0, 15.0);
    const double expected_pair_speed = std::max(
        8.0 * (
            ambient_shear(15.0)
            - pair.fractional_wake_deficit
        ),
        0.0
    );
    if (std::abs(one_pair.at(1) - expected_pair_speed)
        > 1.0e-13
        || std::abs(
            wake_velocity_eq7(8.0, 500.0, 0.0, 15.0)
            - expected_pair_speed
        ) > 1.0e-13) {
        throw std::runtime_error(
            "TAAE one-upstream single-shear fixture failed"
        );
    }
}

}  // namespace wflop::taae
