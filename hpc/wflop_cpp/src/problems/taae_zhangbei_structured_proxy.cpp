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
Backend: deterministic persistent-team CPU evaluation. Each worker evaluates
one complete layout, and every within-layout floating-point reduction has a
fixed order.
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

constexpr double kHubHeightM = 80.0;
constexpr double kRotorRadiusM = 38.5;
constexpr double kThrustCoefficient = 0.8;
constexpr double kTurbulenceIntensity = 0.1;
constexpr double kWindShearExponent = 0.14;
constexpr double kTerrainReferenceM = 50.0;
constexpr double kTurbineCostScale = 10000.0;
constexpr double kLandCostPerSquareMetre = 0.005;
constexpr double kReferenceWindSpeed = 8.0;
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
    const char* manifest_hash;
    double budget;
};

constexpr std::array<ProxyIdentity, 6> kProxyIdentities{{
    {
        "TAAE_Proxy_NC1_Budget600k_tn15",
        "fnv1a64:2869e8628fc4b8ca",
        600000.0
    },
    {
        "TAAE_Proxy_NC1_Budget800k_tn15",
        "fnv1a64:c08eb00a89d64bb4",
        800000.0
    },
    {
        "TAAE_Proxy_NC1_Budget1000k_tn15",
        "fnv1a64:ce544f010e589141",
        1000000.0
    },
    {
        "TAAE_Proxy_NC2_Budget600k_tn15",
        "fnv1a64:acbf3bb1fe9db7e1",
        600000.0
    },
    {
        "TAAE_Proxy_NC2_Budget800k_tn15",
        "fnv1a64:2a66b441931a13b7",
        800000.0
    },
    {
        "TAAE_Proxy_NC2_Budget1000k_tn15",
        "fnv1a64:5719564c01d17bcc",
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
    const double x = static_cast<double>(column) / 19.0;
    const double y = static_cast<double>(row) / 19.0;
    return 50.0
        + 35.0 * std::sin(2.0 * std::numbers::pi * x)
            * std::cos(2.0 * std::numbers::pi * y)
        + 15.0 * (x + y) / 2.0;
}

Point cell_point(const fode::CaseData& data, int cell_1based) {
    const int cell = cell_1based - 1;
    const int row = cell / data.cols;
    const int column = cell - row * data.cols;
    return Point{
        (static_cast<double>(column) + 0.5) * data.cell_width,
        (static_cast<double>(row) + 0.5) * data.cell_width,
        terrain_height(row, column)
    };
}

double turbine_power_kw(double velocity) {
    if (velocity < 2.0) {
        return 0.0;
    }
    if (velocity < 12.8) {
        return 0.3 * velocity * velocity * velocity;
    }
    if (velocity <= 18.0) {
        return 629.1;
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

double wake_velocity_eq7(
    double free_velocity,
    double downstream_distance,
    double crosswind_distance,
    double vertical_displacement
) {
    if (downstream_distance <= 0.0) {
        return free_velocity;
    }
    const double induction = axial_induction(kThrustCoefficient);
    const double radius0 =
        initial_wake_radius(kRotorRadiusM, induction);
    const double kz = 0.243346
        * std::pow(kThrustCoefficient, 0.4297)
        * std::pow(kTurbulenceIntensity, 0.4707);
    const double ky = 0.18265
        * std::pow(kThrustCoefficient, 0.2566)
        * std::pow(kTurbulenceIntensity, 0.2808);
    const double rz = kz * downstream_distance + radius0;
    const double ry = ky * downstream_distance + radius0;
    const double sigma_z = rz / 2.58;
    const double sigma_y = ry / 2.58;
    const double shear = std::pow(
        std::max(
            (vertical_displacement + kHubHeightM) / kHubHeightM,
            1.0e-6
        ),
        kWindShearExponent
    );
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
    return std::max(
        free_velocity * (shear - first_deficit - second_deficit),
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
    double chord,
    double rotor_radius,
    double turbulent_velocity_squared,
    double blade_velocity,
    double distance,
    double sound_speed,
    double spectral_correction
) {
    const double numerator =
        blades * sine_squared_phi * density * density
        * std::pow(chord, 0.7) * rotor_radius * rotor_radius
        * turbulent_velocity_squared
        * std::pow(blade_velocity, 0.7);
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
    double free_velocity
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
            const double pair_velocity =
                wake_velocity_eq7(free_velocity, dx, dy, dz);
            const double deficit = std::clamp(
                1.0 - pair_velocity / free_velocity,
                0.0,
                1.0
            );
            squared_deficit += deficit * deficit;
        }
        const double relative_terrain =
            turbines[static_cast<std::size_t>(downstream)].z
            - kTerrainReferenceM;
        const double shear = std::pow(
            std::max(
                (relative_terrain + kHubHeightM) / kHubHeightM,
                1.0e-6
            ),
            kWindShearExponent
        );
        velocities[static_cast<std::size_t>(downstream)] =
            free_velocity * shear
            * std::max(0.0, 1.0 - std::sqrt(squared_deficit));
    }
    return velocities;
}

double proxy_source_level(int band, double velocity) {
    const double ratio =
        std::max(velocity, 2.0) / kReferenceWindSpeed;
    const double inflow =
        kInflowReferenceDb[static_cast<std::size_t>(band)]
        + 20.0 * std::log10(ratio);
    const double trailing =
        kTrailingReferenceDb[static_cast<std::size_t>(band)]
        + 50.0 * std::log10(ratio);
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
                source.z + kHubHeightM - (monitor.z + 2.0);
            const double distance = std::max(
                std::sqrt(dx * dx + dy * dy + dz * dz),
                1.0
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
    return 0.5 * (convex_area + rectangle);
}

double total_cost(const std::vector<Point>& turbines) {
    const double count = static_cast<double>(turbines.size());
    const double turbine_cost = kTurbineCostScale * count * (
        2.0 / 3.0
        + 1.0 / 3.0 * std::exp(-0.00174 * count * count)
    );
    return turbine_cost
        + kLandCostPerSquareMetre * effective_land_area(turbines);
}

CompleteEvaluation evaluate_layout(
    const int* layout,
    const fode::CaseData& data,
    double budget
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
                data.velocity[static_cast<std::size_t>(speed)]
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

std::string structured_proxy_manifest_hash(const fode::CaseData& data) {
    std::uint64_t hash = 1469598103934665603ULL;
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
    for (const unsigned char byte : data.case_id) {
        mix_byte(byte);
    }
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
    std::ostringstream output;
    output << "fnv1a64:" << std::hex << std::setfill('0')
           << std::setw(16) << hash;
    return output.str();
}

BatchEvaluation evaluate_structured_proxy(
    const std::vector<int>& layouts_1based,
    int batch_size,
    const fode::CaseData& data,
    fode::PersistentExecutor& executor
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
    const std::string observed_hash =
        structured_proxy_manifest_hash(data);
    if (observed_hash != identity.manifest_hash) {
        throw std::invalid_argument(
            "TAAE proxy manifest semantics do not match frozen profile: "
            + observed_hash
        );
    }
    BatchEvaluation result;
    result.values.resize(static_cast<std::size_t>(batch_size));
    result.complete_layout_evaluations =
        static_cast<std::uint64_t>(batch_size);
    result.requested_workers = executor.thread_count();
    result.observed_workers = executor.thread_count();
    result.problem_manifest_hash = observed_hash;
    const auto started = Clock::now();
    executor.parallel_for(0, batch_size, [&](int row) {
        result.values[static_cast<std::size_t>(row)] =
            evaluate_layout(
                layouts_1based.data()
                    + static_cast<std::ptrdiff_t>(
                        row * data.turbine_count
                    ),
                data,
                identity.budget
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
    if (std::abs(inflow - (-56.84831355018201)) > 1.0e-12) {
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
}

}  // namespace wflop::taae
