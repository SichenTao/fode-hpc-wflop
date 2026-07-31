/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0298 pure-C++ Gaussian-wake, radial-cable BPSO,
RTS-24 convex dispatch and Yarpiz-semantic NSGA-III implementation
Paper/DOI: Tao et al., 10.1109/TSG.2020.3022378.
Public source designated by the paper: Yarpiz YPEA126 NSGA-III,
BSD-2-Clause, commit
a6e206086cdf1e29c0ae29c2699bef85df728181.
Public problem-data lineage: MATPOWER case24_ieee_rts, commit
5f1b70611a573f5455de7a2e5786aed12adfbaf8.
Public-source search, missing fields, conflicts, reconstruction, semantic IDs,
HPC design, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/tao_l0298.hpp
HPC realization: complete outer candidates, dominance rows and reference
association are fixed-index tasks on one persistent all-core executor. Every
candidate owns its serial BPSO/wake/dispatch work, so one optimization uses
all cores without nested teams or oversubscription. Geometry, profiles,
turbine/cable/generator data and reference directions are immutable.
Counter-keyed events make candidate work independent of scheduling.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/tao_l0298.hpp"

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
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace core99::l0298 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int grid_side = 12;
constexpr int grid_cells = grid_side * grid_side;
constexpr double cell_m = 500.0;
constexpr double farm_side_m = grid_side * cell_m;
constexpr double rated_power_mw = 3.0;
constexpr double substation_coordinate_m = 3000.0;
constexpr int reference_divisions = 14;
constexpr double discount_rate = 0.0521;
constexpr double cable_voltage_v = 33000.0;
constexpr double cable_installation_eur_per_km = 2.0e5;
constexpr double cable_om_fraction = 0.005;
constexpr double energy_loss_eur_per_kwh = 0.08;
constexpr double copper_resistivity_ohm_m = 1.724e-8;

struct TurbineSpec {
    const char* id;
    double cut_in_mps;
    double rated_mps;
    double cut_out_mps;
    double hub_height_m;
    double rotor_radius_m;
};

constexpr std::array<TurbineSpec, 3> turbines{{
    {"E-82", 3.0, 16.0, 34.0, 78.0, 41.0},
    {"E-115", 2.5, 12.8, 34.0, 135.0, 58.0},
    {"LTW101", 3.0, 15.0, 25.0, 93.5, 50.5},
}};

struct CableSpec {
    int type;
    double area_mm2;
    double diameter_mm;
    double weight_kg_m;
    double capacity_mva;
};

constexpr std::array<CableSpec, 5> cables{{
    {1, 95.0, 89.0, 12.2, 18.0},
    {2, 240.0, 104.0, 18.6, 29.0},
    {3, 400.0, 127.0, 38.0, 36.0},
    {4, 630.0, 143.0, 49.0, 44.0},
    {5, 800.0, 153.0, 59.0, 48.0},
}};

struct Generator {
    int bus;
    double maximum_mw;
    double quadratic;
    double linear;
    double constant;
};

struct CableSolution {
    double objective = std::numeric_limits<double>::infinity();
    double daily_cost_eur = 0.0;
    double length_m = 0.0;
    double violation_mw = 0.0;
    std::vector<CableEdge> edges;
    std::uint64_t evaluations = 0;
};

struct DispatchResult {
    bool feasible = true;
    double cost_eur = 0.0;
    double emission_eur = 0.0;
    double reserve_violation_mw = 0.0;
};

struct Genome {
    std::vector<double> genes;
    std::vector<int> cells;
    Evaluation evaluation;
    int rank = 0;
    int reference = 0;
    double reference_distance = 0.0;
};

struct NormalizationState {
    std::array<double, 3> ideal{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<std::array<double, 3>, 3> extremes{};
    std::array<double, 3> scalar_min{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
};

struct Context {
    std::string model;
    int fixed_turbines = 0;
    bool only_profit = false;
};

double elapsed_seconds(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double capital_recovery_daily(const int years) {
    const double growth = std::pow(1.0 + discount_rate, years);
    return discount_rate * growth / (365.0 * (growth - 1.0));
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t event_seed(
    const std::uint64_t seed,
    const std::uint64_t generation,
    const std::uint64_t individual,
    const std::uint64_t context
) {
    std::uint64_t result = seed;
    result = mix_hash(result, generation);
    result = mix_hash(result, individual);
    return mix_hash(result, context);
}

Point cell_point(const int cell) {
    if (cell < 0 || cell >= grid_cells) {
        throw std::invalid_argument("L0298 invalid active cell");
    }
    return {
        (static_cast<double>(cell % grid_side) + 0.5) * cell_m,
        (static_cast<double>(cell / grid_side) + 0.5) * cell_m,
    };
}

double turbine_power_mw(const TurbineSpec& turbine, const double speed) {
    if (speed < turbine.cut_in_mps || speed > turbine.cut_out_mps) {
        return 0.0;
    }
    if (speed >= turbine.rated_mps) return rated_power_mw;
    return rated_power_mw * (speed - turbine.cut_in_mps)
        / (turbine.rated_mps - turbine.cut_in_mps);
}

double thrust_coefficient(const TurbineSpec& turbine, const double speed) {
    if (speed < turbine.cut_in_mps || speed > turbine.cut_out_mps) {
        return 0.0;
    }
    if (speed < turbine.rated_mps) {
        return std::clamp(
            0.84 - 0.012 * (speed - turbine.cut_in_mps), 0.58, 0.84
        );
    }
    return std::clamp(
        0.26 - 0.008 * (speed - turbine.rated_mps), 0.08, 0.26
    );
}

const std::array<double, 24>& winter_speeds() {
    static const std::array<double, 24> values{
        16.4, 16.1, 16.7, 15.8, 16.5, 16.0, 16.8, 15.6,
        14.5, 12.9, 11.1, 12.8, 10.2, 12.2, 10.8, 12.7,
        11.8, 13.1, 13.8, 14.7, 12.7, 15.4, 14.6, 15.2,
    };
    return values;
}

const std::array<double, 24>& winter_directions() {
    static const std::array<double, 24> values{
        188, 190, 192, 195, 198, 200, 202, 204,
        206, 208, 210, 207, 203, 198, 194, 191,
        188, 190, 193, 197, 205, 215, 210, 207,
    };
    return values;
}

const std::array<double, 24>& summer_speeds() {
    static const std::array<double, 24> values{
        8.0, 7.4, 6.8, 6.2, 5.3, 5.0, 4.2, 5.8,
        7.1, 8.0, 8.3, 8.8, 8.2, 9.0, 9.8, 8.9,
        9.6, 10.1, 11.8, 12.2, 11.5, 10.4, 9.3, 9.0,
    };
    return values;
}

const std::array<double, 24>& summer_directions() {
    static const std::array<double, 24> values{
        180, 179, 178, 174, 168, 160, 150, 145,
        142, 140, 141, 144, 146, 149, 151, 153,
        155, 158, 160, 164, 167, 168, 168, 170,
    };
    return values;
}

const std::array<double, 24>& winter_load() {
    static const std::array<double, 24> values{
        .70, .66, .62, .59, .59, .60, .69, .82,
        .93, .96, .96, .96, .95, .94, .93, .93,
        .95, .98, 1.00, 1.00, .96, .90, .80, .65,
    };
    return values;
}

const std::array<double, 24>& summer_load() {
    static const std::array<double, 24> values{
        .74, .70, .67, .65, .64, .62, .62, .66,
        .82, .88, .92, .94, .94, .93, .92, .91,
        .91, .92, .94, .95, .93, .90, .87, .80,
    };
    return values;
}

std::vector<Generator> rts_generators() {
    std::vector<Generator> result;
    auto add = [&](const int count, const int bus, const double maximum,
                   const double quadratic, const double linear,
                   const double constant) {
        for (int index = 0; index < count; ++index) {
            result.push_back({bus, maximum, quadratic, linear, constant});
        }
    };
    add(2, 1, 20, 0.0, 130.0, 400.6849);
    add(2, 1, 76, 0.014142, 16.0811, 212.3076);
    add(2, 2, 20, 0.0, 130.0, 400.6849);
    add(2, 2, 76, 0.014142, 16.0811, 212.3076);
    add(3, 7, 100, 0.052672, 43.6615, 781.5210);
    add(3, 13, 197, 0.007170, 48.5804, 832.7575);
    add(5, 15, 12, 0.328412, 56.5640, 86.3852);
    add(1, 15, 155, 0.008342, 12.3883, 382.2391);
    add(1, 16, 155, 0.008342, 12.3883, 382.2391);
    add(1, 18, 400, 0.000213, 4.4231, 395.3749);
    add(1, 21, 400, 0.000213, 4.4231, 395.3749);
    add(6, 22, 50, 0.0, 0.001, 0.001);
    add(2, 23, 155, 0.008342, 12.3883, 382.2391);
    add(1, 23, 350, 0.004895, 11.8495, 665.1094);
    return result;
}

const std::vector<Generator>& generators() {
    static const std::vector<Generator> values = rts_generators();
    return values;
}

DispatchResult dispatch(
    const double demand_mw,
    const double wind_mw,
    const int connection_bus,
    const bool with_wind
) {
    std::vector<const Generator*> active;
    double capacity = 0.0;
    for (const auto& generator : generators()) {
        const bool paper_shutdown_bus =
            with_wind
            && (connection_bus == 7 || connection_bus == 16
                || connection_bus == 21 || connection_bus == 23)
            && generator.bus == connection_bus;
        if (paper_shutdown_bus) continue;
        active.push_back(&generator);
        capacity += generator.maximum_mw;
    }
    const double residual = std::max(0.0, demand_mw - wind_mw);
    DispatchResult result;
    // Equation (43) names Rs but supplies no value. A fixed 50 MW operating
    // reserve is the declared completion; unlike a hidden percentage rule it
    // keeps every paper-reported connection-bus case feasible after the
    // paper-stated shutdown of the generator at buses 7/16/21/23.
    constexpr double reserve_requirement_mw = 50.0;
    result.reserve_violation_mw = std::max(
        0.0, demand_mw + reserve_requirement_mw - (capacity + wind_mw)
    );
    if (residual > capacity + 1.0e-8) {
        result.feasible = false;
        result.cost_eur = 1.0e9 * (residual - capacity);
        return result;
    }
    double lower = -1.0e3;
    double upper = 2.0e3;
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double lambda = 0.5 * (lower + upper);
        double supplied = 0.0;
        for (const Generator* generator : active) {
            const double power = generator->quadratic > 0.0
                ? std::clamp(
                    (lambda - generator->linear)
                        / (2.0 * generator->quadratic),
                    0.0, generator->maximum_mw
                )
                : (lambda >= generator->linear
                    ? generator->maximum_mw : 0.0);
            supplied += power;
        }
        if (supplied < residual) lower = lambda;
        else upper = lambda;
    }
    const double lambda = upper;
    double remaining = residual;
    for (std::size_t index = 0; index < active.size(); ++index) {
        const Generator& generator = *active[index];
        double power = generator.quadratic > 0.0
            ? std::clamp(
                (lambda - generator.linear) / (2.0 * generator.quadratic),
                0.0, generator.maximum_mw
            )
            : (lambda >= generator.linear ? generator.maximum_mw : 0.0);
        if (index + 1U == active.size()) {
            power = std::clamp(remaining, 0.0, generator.maximum_mw);
        }
        remaining -= power;
        if (power > 1.0e-10) {
            result.cost_eur += generator.quadratic * power * power
                + generator.linear * power + generator.constant;
        }
    }
    if (std::abs(remaining) > 1.0e-5) {
        result.feasible = false;
        result.cost_eur += 1.0e9 * std::abs(remaining);
    }
    result.emission_eur = 0.03 * result.cost_eur;
    result.feasible = result.feasible && result.reserve_violation_mw <= 1.0e-8;
    return result;
}

double farm_power_mw(
    const std::vector<Point>& layout,
    const TurbineSpec& turbine,
    const double free_speed,
    const double direction_degrees
) {
    const int count = static_cast<int>(layout.size());
    const double angle = direction_degrees * std::numbers::pi / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    std::vector<double> along(static_cast<std::size_t>(count));
    std::vector<double> across(static_cast<std::size_t>(count));
    std::vector<int> order(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        along[static_cast<std::size_t>(index)] =
            cosine * layout[static_cast<std::size_t>(index)].x_m
            + sine * layout[static_cast<std::size_t>(index)].y_m;
        across[static_cast<std::size_t>(index)] =
            -sine * layout[static_cast<std::size_t>(index)].x_m
            + cosine * layout[static_cast<std::size_t>(index)].y_m;
        order[static_cast<std::size_t>(index)] = index;
    }
    std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
        const double left = along[static_cast<std::size_t>(a)];
        const double right = along[static_cast<std::size_t>(b)];
        return left != right ? left < right : a < b;
    });
    std::vector<double> inflow(static_cast<std::size_t>(count), free_speed);
    double total = 0.0;
    constexpr double entrainment = 0.032;
    for (int position = 0; position < count; ++position) {
        const int downstream = order[static_cast<std::size_t>(position)];
        double squared_deficit = 0.0;
        for (int prior = 0; prior < position; ++prior) {
            const int upstream = order[static_cast<std::size_t>(prior)];
            const double downstream_distance =
                along[static_cast<std::size_t>(downstream)]
                - along[static_cast<std::size_t>(upstream)];
            if (!(downstream_distance > 0.0)) continue;
            const double sigma = 0.5 * (
                turbine.rotor_radius_m + entrainment * downstream_distance
            );
            const double ratio = 2.0 * sigma / turbine.rotor_radius_m;
            const double ct = thrust_coefficient(
                turbine, inflow[static_cast<std::size_t>(upstream)]
            );
            const double amplitude = 1.0 - std::sqrt(std::max(
                0.0, 1.0 - ct / std::max(1.0e-12, ratio * ratio)
            ));
            const double cross = across[static_cast<std::size_t>(downstream)]
                - across[static_cast<std::size_t>(upstream)];
            const double deficit = amplitude * std::exp(
                -cross * cross / (2.0 * sigma * sigma)
            );
            squared_deficit += deficit * deficit;
        }
        inflow[static_cast<std::size_t>(downstream)] = free_speed * std::max(
            0.0, 1.0 - std::sqrt(squared_deficit)
        );
        total += turbine_power_mw(
            turbine, inflow[static_cast<std::size_t>(downstream)]
        );
    }
    return total;
}

std::vector<std::vector<int>> cable_parent_candidates(
    const std::vector<Point>& layout
) {
    const int count = static_cast<int>(layout.size());
    std::vector<double> radius(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        radius[static_cast<std::size_t>(index)] = std::hypot(
            layout[static_cast<std::size_t>(index)].x_m
                - substation_coordinate_m,
            layout[static_cast<std::size_t>(index)].y_m
                - substation_coordinate_m
        );
    }
    std::vector<std::vector<int>> result(static_cast<std::size_t>(count));
    for (int node = 0; node < count; ++node) {
        auto& candidates = result[static_cast<std::size_t>(node)];
        candidates.push_back(count);
        for (int parent = 0; parent < count; ++parent) {
            if (parent == node) continue;
            const double parent_radius = radius[static_cast<std::size_t>(parent)];
            const double node_radius = radius[static_cast<std::size_t>(node)];
            if (parent_radius < node_radius - 1.0e-9
                || (parent_radius == node_radius && parent < node)) {
                candidates.push_back(parent);
            }
        }
        std::stable_sort(
            candidates.begin() + 1, candidates.end(), [&](const int a, const int b) {
                const double da = std::hypot(
                    layout[static_cast<std::size_t>(node)].x_m
                        - layout[static_cast<std::size_t>(a)].x_m,
                    layout[static_cast<std::size_t>(node)].y_m
                        - layout[static_cast<std::size_t>(a)].y_m
                );
                const double db = std::hypot(
                    layout[static_cast<std::size_t>(node)].x_m
                        - layout[static_cast<std::size_t>(b)].x_m,
                    layout[static_cast<std::size_t>(node)].y_m
                        - layout[static_cast<std::size_t>(b)].y_m
                );
                return da != db ? da < db : a < b;
            }
        );
    }
    return result;
}

std::vector<int> decode_cable_parents(
    const std::vector<unsigned char>& bits,
    const std::vector<std::vector<int>>& candidates
) {
    constexpr int bits_per_node = 7;
    std::vector<int> result(candidates.size());
    for (std::size_t node = 0; node < candidates.size(); ++node) {
        int code = 0;
        for (int bit = 0; bit < bits_per_node; ++bit) {
            code |= static_cast<int>(
                bits[node * bits_per_node + static_cast<std::size_t>(bit)]
            ) << bit;
        }
        const auto& choices = candidates[node];
        result[node] = choices[static_cast<std::size_t>(code) % choices.size()];
    }
    return result;
}

double cable_price_eur_per_km(const CableSpec& cable) {
    constexpr double alpha = 4.11e5;
    constexpr double beta = 5.96e5;
    constexpr double gamma = 4.1;
    return alpha + beta * std::exp(gamma * cable.capacity_mva / 100.0);
}

CableSolution score_cable_tree(
    const std::vector<Point>& layout,
    const std::vector<int>& parents,
    const bool retain_edges
) {
    const int count = static_cast<int>(layout.size());
    std::vector<int> subtree(static_cast<std::size_t>(count), 1);
    std::vector<int> order(static_cast<std::size_t>(count));
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](const int a, const int b) {
        const double da = std::hypot(
            layout[static_cast<std::size_t>(a)].x_m - substation_coordinate_m,
            layout[static_cast<std::size_t>(a)].y_m - substation_coordinate_m
        );
        const double db = std::hypot(
            layout[static_cast<std::size_t>(b)].x_m - substation_coordinate_m,
            layout[static_cast<std::size_t>(b)].y_m - substation_coordinate_m
        );
        return da != db ? da > db : a > b;
    });
    for (const int node : order) {
        const int parent = parents[static_cast<std::size_t>(node)];
        if (parent < count) {
            subtree[static_cast<std::size_t>(parent)] +=
                subtree[static_cast<std::size_t>(node)];
        }
    }
    CableSolution result;
    result.objective = 0.0;
    if (retain_edges) result.edges.reserve(layout.size());
    for (int node = 0; node < count; ++node) {
        const int parent = parents[static_cast<std::size_t>(node)];
        const Point target = parent == count
            ? Point{substation_coordinate_m, substation_coordinate_m}
            : layout[static_cast<std::size_t>(parent)];
        const double length = std::hypot(
            layout[static_cast<std::size_t>(node)].x_m - target.x_m,
            layout[static_cast<std::size_t>(node)].y_m - target.y_m
        );
        const double flow = rated_power_mw
            * static_cast<double>(subtree[static_cast<std::size_t>(node)]);
        double best = std::numeric_limits<double>::infinity();
        int best_type = 5;
        for (const auto& cable : cables) {
            if (cable.capacity_mva + 1.0e-9 < flow) continue;
            const double investment =
                (cable_installation_eur_per_km
                 + cable_price_eur_per_km(cable))
                * length / 1000.0 * capital_recovery_daily(25);
            const double current_a = flow * 1.0e6
                / (std::sqrt(3.0) * cable_voltage_v);
            const double resistance = copper_resistivity_ohm_m * length
                / (cable.area_mm2 * 1.0e-6);
            const double loss_kwh = 24.0 * 3.0 * current_a * current_a
                * resistance / 1000.0;
            const double daily = investment * (1.0 + cable_om_fraction)
                + energy_loss_eur_per_kwh * loss_kwh;
            if (daily < best) {
                best = daily;
                best_type = cable.type;
            }
        }
        if (!std::isfinite(best)) {
            const double excess = std::max(0.0, flow - cables.back().capacity_mva);
            result.violation_mw += excess;
            best = 1.0e7 + 1.0e6 * excess + 100.0 * length;
        }
        result.daily_cost_eur += best;
        result.length_m += length;
        result.objective += best;
        if (retain_edges) {
            result.edges.push_back({node, parent, best_type, flow, length});
        }
    }
    result.objective += 1.0e8 * result.violation_mw;
    return result;
}

std::vector<unsigned char> encoded_sector_tree(
    const std::vector<Point>& layout,
    const std::vector<std::vector<int>>& candidates
) {
    constexpr int bits_per_node = 7;
    const int count = static_cast<int>(layout.size());
    std::vector<unsigned char> result(
        static_cast<std::size_t>(count * bits_per_node), 0U
    );
    for (int node = 0; node < count; ++node) {
        const Point& point = layout[static_cast<std::size_t>(node)];
        double angle = std::atan2(
            point.y_m - substation_coordinate_m,
            point.x_m - substation_coordinate_m
        );
        if (angle < 0.0) angle += 2.0 * std::numbers::pi;
        const int sector = std::min(7, static_cast<int>(
            angle * 8.0 / (2.0 * std::numbers::pi)
        ));
        int chosen = count;
        double best = std::numeric_limits<double>::infinity();
        const auto& choices = candidates[static_cast<std::size_t>(node)];
        for (const int parent : choices) {
            if (parent == count) continue;
            const Point& candidate = layout[static_cast<std::size_t>(parent)];
            double parent_angle = std::atan2(
                candidate.y_m - substation_coordinate_m,
                candidate.x_m - substation_coordinate_m
            );
            if (parent_angle < 0.0) parent_angle += 2.0 * std::numbers::pi;
            const int parent_sector = std::min(7, static_cast<int>(
                parent_angle * 8.0 / (2.0 * std::numbers::pi)
            ));
            if (parent_sector != sector) continue;
            const double distance = std::hypot(
                point.x_m - candidate.x_m, point.y_m - candidate.y_m
            );
            if (distance < best) {
                best = distance;
                chosen = parent;
            }
        }
        const auto iterator = std::find(choices.begin(), choices.end(), chosen);
        const int code = static_cast<int>(iterator - choices.begin());
        for (int bit = 0; bit < bits_per_node; ++bit) {
            result[static_cast<std::size_t>(node * bits_per_node + bit)] =
                static_cast<unsigned char>((code >> bit) & 1);
        }
    }
    return result;
}

CableSolution solve_cable_bpso(
    const std::vector<Point>& layout,
    const std::uint64_t seed,
    const int population,
    const int iterations,
    const bool retain_edges
) {
    if (population < 2 || iterations < 0) {
        throw std::invalid_argument("L0298 invalid BPSO configuration");
    }
    constexpr int bits_per_node = 7;
    constexpr double inertia = 0.7298;
    constexpr double cognitive = 1.4961;
    constexpr double social = 1.4961;
    const int dimensions = static_cast<int>(layout.size()) * bits_per_node;
    const auto candidates = cable_parent_candidates(layout);
    const fode::CounterRng rng(seed);
    std::vector<std::vector<unsigned char>> positions(
        static_cast<std::size_t>(population),
        std::vector<unsigned char>(static_cast<std::size_t>(dimensions), 0U)
    );
    std::vector<std::vector<unsigned char>> personal = positions;
    std::vector<std::vector<double>> velocity(
        static_cast<std::size_t>(population),
        std::vector<double>(static_cast<std::size_t>(dimensions), 0.0)
    );
    std::vector<double> personal_score(
        static_cast<std::size_t>(population),
        std::numeric_limits<double>::infinity()
    );
    std::vector<unsigned char> global = encoded_sector_tree(layout, candidates);
    double global_score = std::numeric_limits<double>::infinity();
    CableSolution receipt;
    for (int particle = 0; particle < population; ++particle) {
        if (particle == 0) {
            positions.front() = global;
        } else {
            for (int bit = 0; bit < dimensions; ++bit) {
                positions[static_cast<std::size_t>(particle)]
                    [static_cast<std::size_t>(bit)] =
                    rng.uniform(0, 51, particle, bit) < 0.5 ? 0U : 1U;
                velocity[static_cast<std::size_t>(particle)]
                    [static_cast<std::size_t>(bit)] =
                    2.0 * rng.uniform(0, 52, particle, bit) - 1.0;
            }
        }
        const auto parents = decode_cable_parents(
            positions[static_cast<std::size_t>(particle)], candidates
        );
        const double score = score_cable_tree(layout, parents, false).objective;
        ++receipt.evaluations;
        personal[static_cast<std::size_t>(particle)] =
            positions[static_cast<std::size_t>(particle)];
        personal_score[static_cast<std::size_t>(particle)] = score;
        if (score < global_score
            || (score == global_score
                && positions[static_cast<std::size_t>(particle)] < global)) {
            global_score = score;
            global = positions[static_cast<std::size_t>(particle)];
        }
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const std::uint64_t generation = static_cast<std::uint64_t>(iteration + 1);
        for (int particle = 0; particle < population; ++particle) {
            auto& position = positions[static_cast<std::size_t>(particle)];
            auto& speed = velocity[static_cast<std::size_t>(particle)];
            const auto& best = personal[static_cast<std::size_t>(particle)];
            for (int bit = 0; bit < dimensions; ++bit) {
                const double current = position[static_cast<std::size_t>(bit)];
                speed[static_cast<std::size_t>(bit)] = std::clamp(
                    inertia * speed[static_cast<std::size_t>(bit)]
                    + cognitive * rng.uniform(generation, 53, particle, bit, 0)
                        * (static_cast<double>(best[static_cast<std::size_t>(bit)])
                           - current)
                    + social * rng.uniform(generation, 53, particle, bit, 1)
                        * (static_cast<double>(global[static_cast<std::size_t>(bit)])
                           - current),
                    -8.0, 8.0
                );
                const double probability = 1.0
                    / (1.0 + std::exp(-speed[static_cast<std::size_t>(bit)]));
                position[static_cast<std::size_t>(bit)] =
                    rng.uniform(generation, 54, particle, bit) < probability
                    ? 1U : 0U;
            }
            const auto parents = decode_cable_parents(position, candidates);
            const double score = score_cable_tree(layout, parents, false).objective;
            ++receipt.evaluations;
            if (score < personal_score[static_cast<std::size_t>(particle)]
                || (score == personal_score[static_cast<std::size_t>(particle)]
                    && position < best)) {
                personal_score[static_cast<std::size_t>(particle)] = score;
                personal[static_cast<std::size_t>(particle)] = position;
            }
            if (score < global_score || (score == global_score && position < global)) {
                global_score = score;
                global = position;
            }
        }
    }
    const auto parents = decode_cable_parents(global, candidates);
    CableSolution best = score_cable_tree(layout, parents, retain_edges);
    best.evaluations = receipt.evaluations;
    return best;
}

std::array<double, 3> objective_costs(const Evaluation& value) {
    return {
        -value.profit_rate_percent,
        -value.capacity_factor_percent,
        value.variability_percent,
    };
}

bool dominates(const Genome& left, const Genome& right) {
    if (left.evaluation.feasible != right.evaluation.feasible) {
        return left.evaluation.feasible;
    }
    if (!left.evaluation.feasible) {
        return left.evaluation.constraint_violation
            < right.evaluation.constraint_violation;
    }
    const auto a = objective_costs(left.evaluation);
    const auto b = objective_costs(right.evaluation);
    bool strict = false;
    for (int objective = 0; objective < 3; ++objective) {
        if (a[static_cast<std::size_t>(objective)]
            > b[static_cast<std::size_t>(objective)]) return false;
        strict = strict || a[static_cast<std::size_t>(objective)]
            < b[static_cast<std::size_t>(objective)];
    }
    return strict;
}

std::vector<std::vector<int>> assign_rank(
    std::vector<Genome>& population,
    fode::PersistentExecutor& executor
) {
    const int count = static_cast<int>(population.size());
    std::vector<std::vector<int>> outgoing(static_cast<std::size_t>(count));
    std::vector<int> incoming(static_cast<std::size_t>(count), 0);
    executor.parallel_for(0, count, [&](const int left) {
        auto& row = outgoing[static_cast<std::size_t>(left)];
        int degree = 0;
        for (int right = 0; right < count; ++right) {
            if (left == right) continue;
            if (dominates(population[static_cast<std::size_t>(left)],
                          population[static_cast<std::size_t>(right)])) {
                row.push_back(right);
            } else if (dominates(population[static_cast<std::size_t>(right)],
                                 population[static_cast<std::size_t>(left)])) {
                ++degree;
            }
        }
        incoming[static_cast<std::size_t>(left)] = degree;
    });
    std::vector<std::vector<int>> fronts(1);
    for (int index = 0; index < count; ++index) {
        if (incoming[static_cast<std::size_t>(index)] == 0) {
            population[static_cast<std::size_t>(index)].rank = 1;
            fronts.front().push_back(index);
        }
    }
    std::size_t current = 0;
    while (current < fronts.size() && !fronts[current].empty()) {
        std::vector<int> next;
        for (const int source : fronts[current]) {
            for (const int target : outgoing[static_cast<std::size_t>(source)]) {
                int& count_in = incoming[static_cast<std::size_t>(target)];
                if (--count_in == 0) {
                    population[static_cast<std::size_t>(target)].rank =
                        static_cast<int>(current) + 2;
                    next.push_back(target);
                }
            }
        }
        if (!next.empty()) {
            std::sort(next.begin(), next.end());
            fronts.push_back(std::move(next));
        }
        ++current;
    }
    return fronts;
}

const std::vector<std::array<double, 3>>& reference_points() {
    static const std::vector<std::array<double, 3>> values = [] {
        std::vector<std::array<double, 3>> result;
        for (int first = 0; first <= reference_divisions; ++first) {
            for (int second = 0; second <= reference_divisions - first; ++second) {
                const int third = reference_divisions - first - second;
                result.push_back({
                    static_cast<double>(first) / reference_divisions,
                    static_cast<double>(second) / reference_divisions,
                    static_cast<double>(third) / reference_divisions,
                });
            }
        }
        return result;
    }();
    return values;
}

std::array<double, 3> solve_intercepts(
    const std::array<std::array<double, 3>, 3>& extreme,
    const std::array<double, 3>& fallback
) {
    double matrix[3][4]{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            matrix[row][column] = extreme[static_cast<std::size_t>(column)]
                [static_cast<std::size_t>(row)];
        }
        matrix[row][3] = 1.0;
    }
    for (int pivot = 0; pivot < 3; ++pivot) {
        int selected = pivot;
        for (int row = pivot + 1; row < 3; ++row) {
            if (std::abs(matrix[row][pivot]) > std::abs(matrix[selected][pivot])) {
                selected = row;
            }
        }
        if (std::abs(matrix[selected][pivot]) < 1.0e-12) return fallback;
        for (int column = pivot; column < 4; ++column) {
            std::swap(matrix[pivot][column], matrix[selected][column]);
        }
        const double scale = matrix[pivot][pivot];
        for (int column = pivot; column < 4; ++column) {
            matrix[pivot][column] /= scale;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == pivot) continue;
            const double factor = matrix[row][pivot];
            for (int column = pivot; column < 4; ++column) {
                matrix[row][column] -= factor * matrix[pivot][column];
            }
        }
    }
    std::array<double, 3> intercept{};
    for (int index = 0; index < 3; ++index) {
        const double weight = matrix[index][3];
        if (!(weight > 1.0e-12) || !std::isfinite(weight)) return fallback;
        intercept[static_cast<std::size_t>(index)] = 1.0 / weight;
    }
    return intercept;
}

void associate(
    std::vector<Genome>& population,
    NormalizationState& state,
    fode::PersistentExecutor& executor
) {
    for (const auto& item : population) {
        const auto costs = objective_costs(item.evaluation);
        for (int objective = 0; objective < 3; ++objective) {
            state.ideal[static_cast<std::size_t>(objective)] = std::min(
                state.ideal[static_cast<std::size_t>(objective)],
                costs[static_cast<std::size_t>(objective)]
            );
        }
    }
    std::array<double, 3> maximum_shift{};
    for (const auto& item : population) {
        const auto costs = objective_costs(item.evaluation);
        std::array<double, 3> shifted{};
        for (int objective = 0; objective < 3; ++objective) {
            shifted[static_cast<std::size_t>(objective)] =
                costs[static_cast<std::size_t>(objective)]
                - state.ideal[static_cast<std::size_t>(objective)];
            maximum_shift[static_cast<std::size_t>(objective)] = std::max(
                maximum_shift[static_cast<std::size_t>(objective)],
                shifted[static_cast<std::size_t>(objective)]
            );
        }
        for (int target = 0; target < 3; ++target) {
            double scalar = 0.0;
            for (int objective = 0; objective < 3; ++objective) {
                const double weight = objective == target ? 1.0 : 1.0e-10;
                scalar = std::max(
                    scalar,
                    shifted[static_cast<std::size_t>(objective)] / weight
                );
            }
            if (scalar < state.scalar_min[static_cast<std::size_t>(target)]) {
                state.scalar_min[static_cast<std::size_t>(target)] = scalar;
                state.extremes[static_cast<std::size_t>(target)] = shifted;
            }
        }
    }
    for (double& value : maximum_shift) value = std::max(value, 1.0e-12);
    const auto intercept = solve_intercepts(state.extremes, maximum_shift);
    const auto& references = reference_points();
    executor.parallel_for(0, static_cast<int>(population.size()), [&](const int index) {
        auto& item = population[static_cast<std::size_t>(index)];
        const auto costs = objective_costs(item.evaluation);
        std::array<double, 3> normalized{};
        for (int objective = 0; objective < 3; ++objective) {
            normalized[static_cast<std::size_t>(objective)] =
                (costs[static_cast<std::size_t>(objective)]
                 - state.ideal[static_cast<std::size_t>(objective)])
                / std::max(1.0e-12, intercept[static_cast<std::size_t>(objective)]);
        }
        int best_reference = 0;
        double best_distance = std::numeric_limits<double>::infinity();
        for (std::size_t reference = 0; reference < references.size(); ++reference) {
            const auto& vector = references[reference];
            const double norm = std::sqrt(
                vector[0] * vector[0] + vector[1] * vector[1]
                + vector[2] * vector[2]
            );
            const double projection = (
                normalized[0] * vector[0] + normalized[1] * vector[1]
                + normalized[2] * vector[2]
            ) / norm;
            double squared = 0.0;
            for (int objective = 0; objective < 3; ++objective) {
                const double residual = normalized[static_cast<std::size_t>(objective)]
                    - projection * vector[static_cast<std::size_t>(objective)] / norm;
                squared += residual * residual;
            }
            const double distance = std::sqrt(squared);
            if (distance < best_distance
                || (distance == best_distance
                    && static_cast<int>(reference) < best_reference)) {
                best_reference = static_cast<int>(reference);
                best_distance = distance;
            }
        }
        item.reference = best_reference;
        item.reference_distance = best_distance;
    });
}

std::vector<int> decode_cells(const std::vector<double>& genes, const int fixed) {
    if (genes.size() != static_cast<std::size_t>(grid_cells + 1)) {
        throw std::invalid_argument("L0298 genome length");
    }
    const int count = fixed > 0 ? fixed : std::clamp(
        60 + static_cast<int>(std::llround(20.0 * genes.front())), 60, 80
    );
    std::vector<int> cells(static_cast<std::size_t>(grid_cells));
    std::iota(cells.begin(), cells.end(), 0);
    std::stable_sort(cells.begin(), cells.end(), [&](const int a, const int b) {
        const double left = genes[static_cast<std::size_t>(a + 1)];
        const double right = genes[static_cast<std::size_t>(b + 1)];
        return left != right ? left > right : a < b;
    });
    cells.resize(static_cast<std::size_t>(count));
    std::sort(cells.begin(), cells.end());
    return cells;
}

int select_index(
    const std::vector<Genome>& population,
    const std::string& role
) {
    int best = 0;
    auto preferred = [&](const Genome& left, const Genome& right) {
        if (left.evaluation.feasible != right.evaluation.feasible) {
            return left.evaluation.feasible;
        }
        if (!left.evaluation.feasible) {
            return left.evaluation.constraint_violation
                < right.evaluation.constraint_violation;
        }
        if (role.find("profit") != std::string::npos) {
            return left.evaluation.profit_rate_percent
                > right.evaluation.profit_rate_percent;
        }
        if (role.find("capacity_factor") != std::string::npos) {
            return left.evaluation.capacity_factor_percent
                > right.evaluation.capacity_factor_percent;
        }
        return left.evaluation.variability_percent
            < right.evaluation.variability_percent;
    };
    for (int index = 1; index < static_cast<int>(population.size()); ++index) {
        if (preferred(population[static_cast<std::size_t>(index)],
                      population[static_cast<std::size_t>(best)])) best = index;
    }
    return best;
}

}  // namespace

struct Problem::Impl {
    ProfileId profile = ProfileId::model_comparison;
    std::string id;
    int turbine_index = 0;
    bool is_summer = false;
    int connection_bus = 3;
    std::vector<Context> contexts;
    std::vector<std::vector<std::string>> roles;
};

Problem::Problem(const ProfileId profile) : impl_(std::make_unique<Impl>()) {
    impl_->profile = profile;
    impl_->id = to_string(profile);
    auto add_model1 = [&] {
        impl_->contexts.push_back({"Model 1", 0, false});
        impl_->roles.push_back({
            "model1_best_profit", "model1_best_capacity_factor",
            "model1_best_variability",
        });
    };
    switch (profile) {
    case ProfileId::model_comparison:
        add_model1();
        impl_->contexts.push_back({"Model 2", 70, false});
        impl_->roles.push_back({
            "model2_best_profit", "model2_best_capacity_factor",
            "model2_best_variability",
        });
        impl_->contexts.push_back({"Model 3", 0, true});
        impl_->roles.push_back({"model3_best_profit"});
        break;
    case ProfileId::turbine_e115:
        impl_->turbine_index = 1;
        add_model1();
        impl_->roles.back().erase(impl_->roles.back().begin());
        break;
    case ProfileId::turbine_ltw101:
        impl_->turbine_index = 2;
        add_model1();
        impl_->roles.back().erase(impl_->roles.back().begin());
        break;
    case ProfileId::summer:
        impl_->is_summer = true;
        add_model1();
        break;
    case ProfileId::bus5: impl_->connection_bus = 5; add_model1(); break;
    case ProfileId::bus7: impl_->connection_bus = 7; add_model1(); break;
    case ProfileId::bus16: impl_->connection_bus = 16; add_model1(); break;
    case ProfileId::bus21: impl_->connection_bus = 21; add_model1(); break;
    case ProfileId::bus23: impl_->connection_bus = 23; add_model1(); break;
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;
ProfileId Problem::profile() const noexcept { return impl_->profile; }
const std::string& Problem::id() const noexcept { return impl_->id; }

int Problem::expected_role_count() const noexcept {
    int count = 0;
    for (const auto& roles : impl_->roles) count += static_cast<int>(roles.size());
    return count;
}

Evaluation Problem::evaluate_cells(
    const std::vector<int>& active_cells,
    const std::string& model,
    const std::uint64_t cable_seed,
    const int inner_population,
    const int inner_iterations,
    std::vector<CableEdge>* cable_edges,
    std::uint64_t* cable_evaluations
) const {
    if (active_cells.size() < 60U || active_cells.size() > 80U) {
        throw std::invalid_argument("L0298 capacity/cardinality constraint");
    }
    if (!std::is_sorted(active_cells.begin(), active_cells.end())
        || std::adjacent_find(active_cells.begin(), active_cells.end())
            != active_cells.end()) {
        throw std::invalid_argument("L0298 cells must be sorted and unique");
    }
    std::vector<Point> layout;
    layout.reserve(active_cells.size());
    for (const int cell : active_cells) layout.push_back(cell_point(cell));
    CableSolution cable = solve_cable_bpso(
        layout, cable_seed, inner_population, inner_iterations,
        cable_edges != nullptr
    );
    if (cable_edges) *cable_edges = cable.edges;
    if (cable_evaluations) *cable_evaluations = cable.evaluations;

    const auto& speeds = impl_->is_summer ? summer_speeds() : winter_speeds();
    const auto& directions = impl_->is_summer
        ? summer_directions() : winter_directions();
    const auto& loads = impl_->is_summer ? summer_load() : winter_load();
    const TurbineSpec& turbine = turbines[static_cast<std::size_t>(impl_->turbine_index)];
    std::array<double, 24> powers{};
    double sum_normalized = 0.0;
    double daily_generation_cost = 0.0;
    double daily_emission_cost = 0.0;
    double no_wind_cost = 0.0;
    double revenue = 0.0;
    double violation = cable.violation_mw;
    for (int hour = 0; hour < 24; ++hour) {
        const double power = farm_power_mw(
            layout, turbine, speeds[static_cast<std::size_t>(hour)],
            directions[static_cast<std::size_t>(hour)]
        );
        powers[static_cast<std::size_t>(hour)] = power;
        sum_normalized += power
            / (rated_power_mw * static_cast<double>(layout.size()));
        const double demand = 2850.0 * loads[static_cast<std::size_t>(hour)];
        const DispatchResult with_wind = dispatch(
            demand, power, impl_->connection_bus, true
        );
        const DispatchResult without_wind = dispatch(
            demand, 0.0, impl_->connection_bus, false
        );
        daily_generation_cost += with_wind.cost_eur;
        daily_emission_cost += with_wind.emission_eur;
        no_wind_cost += without_wind.cost_eur + without_wind.emission_eur;
        violation += with_wind.reserve_violation_mw;
        const double normalized_power = power
            / (rated_power_mw * static_cast<double>(layout.size()));
        const double price = 1000.0 - 120.0 * normalized_power;
        revenue += price * power;
    }
    Evaluation result;
    result.turbine_count = static_cast<int>(layout.size());
    result.installed_capacity_mw = rated_power_mw * layout.size();
    result.capacity_factor_percent = 100.0 * sum_normalized / 24.0;
    double variance = 0.0;
    for (const double power : powers) {
        const double normalized = power / result.installed_capacity_mw;
        const double residual = normalized - result.capacity_factor_percent / 100.0;
        variance += residual * residual;
        result.wind_daily_energy_mwh += power;
    }
    result.variability_percent = 100.0 * std::sqrt(variance / 24.0);
    result.cable_daily_cost_eur = cable.daily_cost_eur;
    result.cable_length_m = cable.length_m;
    result.grid_benefit_eur = no_wind_cost
        - (daily_generation_cost + daily_emission_cost);

    const double turbine_unit_eur = 1.0e5 * (-1.46 + 8.38 * rated_power_mw);
    const double turbine_investment = layout.size() * turbine_unit_eur
        * (2.0 / 3.0 + std::exp(-0.00174 * layout.size() * layout.size()) / 3.0)
        * capital_recovery_daily(20);
    const double turbine_cost = turbine_investment * 1.02;
    const double transformer_unit = -1.208e6
        + 2143.0 * std::pow(result.installed_capacity_mw * 1.0e6, 0.4473);
    const double transformer_cost = std::max(0.0, transformer_unit)
        * capital_recovery_daily(50) * 1.01;
    double outlay = turbine_cost + transformer_cost;
    if (model != "Model 2") {
        outlay += cable.daily_cost_eur
            + daily_generation_cost + daily_emission_cost;
    }
    result.profit_rate_percent = 100.0 * (revenue - outlay)
        / std::max(1.0, outlay);
    result.constraint_violation = violation;
    result.feasible = violation <= 1.0e-8;
    return result;
}

namespace {

std::vector<Genome> optimize_context(
    const Problem& problem,
    const Context& context,
    const RunConfig& config,
    const std::uint64_t context_key,
    fode::PersistentExecutor& executor,
    std::uint64_t& outer_evaluations,
    std::uint64_t& cable_evaluations,
    std::uint64_t& hourly_evaluations,
    double& evaluator_seconds,
    double& algorithm_seconds
) {
    const fode::CounterRng rng(event_seed(config.seed, 0, 0, context_key));
    const int dimensions = grid_cells + 1;
    std::vector<Genome> population(static_cast<std::size_t>(config.outer_population));
    const auto initialization_started = Clock::now();
    executor.parallel_for(0, config.outer_population, [&](const int individual) {
        auto& item = population[static_cast<std::size_t>(individual)];
        item.genes.resize(static_cast<std::size_t>(dimensions));
        for (int coordinate = 0; coordinate < dimensions; ++coordinate) {
            item.genes[static_cast<std::size_t>(coordinate)] = rng.uniform(
                0, 61, individual, coordinate
            );
        }
        item.cells = decode_cells(item.genes, context.fixed_turbines);
    });
    algorithm_seconds += elapsed_seconds(initialization_started);
    auto evaluate = [&](std::vector<Genome>& items, const std::uint64_t generation) {
        std::vector<std::uint64_t> local_cable(items.size(), 0);
        const auto started = Clock::now();
        executor.parallel_for(0, static_cast<int>(items.size()), [&](const int index) {
            auto& item = items[static_cast<std::size_t>(index)];
            const std::uint64_t seed = event_seed(
                config.seed, generation, static_cast<std::uint64_t>(index), context_key
            );
            item.evaluation = problem.evaluate_cells(
                item.cells, context.model, seed,
                config.inner_population, config.inner_iterations,
                nullptr, &local_cable[static_cast<std::size_t>(index)]
            );
        });
        evaluator_seconds += elapsed_seconds(started);
        outer_evaluations += items.size();
        hourly_evaluations += 24U * items.size();
        cable_evaluations += std::accumulate(
            local_cable.begin(), local_cable.end(), std::uint64_t{0}
        );
    };
    evaluate(population, 0);
    NormalizationState normalization;
    auto sorting_started = Clock::now();
    assign_rank(population, executor);
    associate(population, normalization, executor);
    algorithm_seconds += elapsed_seconds(sorting_started);

    for (int iteration = 0; iteration < config.outer_iterations; ++iteration) {
        const std::uint64_t generation = static_cast<std::uint64_t>(iteration + 1);
        const auto variation_started = Clock::now();
        std::vector<Genome> offspring(static_cast<std::size_t>(config.outer_population));
        const int crossover_children = 2 * static_cast<int>(std::llround(
            0.5 * static_cast<double>(config.outer_population) / 2.0
        ));
        executor.parallel_for(0, config.outer_population, [&](const int child_index) {
            auto& child = offspring[static_cast<std::size_t>(child_index)];
            if (child_index < crossover_children) {
                const int pair = child_index / 2;
                const int first = rng.integer(
                    0, config.outer_population, generation, 62, pair, 0
                );
                const int second = rng.integer(
                    0, config.outer_population, generation, 62, pair, 1
                );
                child.genes.resize(static_cast<std::size_t>(dimensions));
                for (int coordinate = 0; coordinate < dimensions; ++coordinate) {
                    double alpha = rng.uniform(
                        generation, 63, pair, coordinate
                    );
                    if ((child_index & 1) != 0) alpha = 1.0 - alpha;
                    child.genes[static_cast<std::size_t>(coordinate)] =
                        alpha * population[static_cast<std::size_t>(first)]
                            .genes[static_cast<std::size_t>(coordinate)]
                        + (1.0 - alpha)
                            * population[static_cast<std::size_t>(second)]
                                .genes[static_cast<std::size_t>(coordinate)];
                }
            } else {
                const int mutant = child_index - crossover_children;
                const int parent = rng.integer(
                    0, config.outer_population, generation, 64, mutant
                );
                child.genes = population[static_cast<std::size_t>(parent)].genes;
                const int mutation_count = static_cast<int>(std::ceil(
                    0.02 * static_cast<double>(dimensions)
                ));
                for (int event = 0; event < mutation_count; ++event) {
                    const int coordinate = rng.integer(
                        0, dimensions, generation, 65, mutant, event
                    );
                    child.genes[static_cast<std::size_t>(coordinate)] = std::clamp(
                        child.genes[static_cast<std::size_t>(coordinate)]
                        + 0.1 * rng.normal(
                            generation, 66, mutant, coordinate, event
                        ), 0.0, 1.0
                    );
                }
            }
            child.cells = decode_cells(child.genes, context.fixed_turbines);
        });
        algorithm_seconds += elapsed_seconds(variation_started);
        evaluate(offspring, generation);

        sorting_started = Clock::now();
        std::vector<Genome> merged;
        merged.reserve(population.size() + offspring.size());
        for (auto& item : population) merged.push_back(std::move(item));
        for (auto& item : offspring) merged.push_back(std::move(item));
        const auto fronts = assign_rank(merged, executor);
        associate(merged, normalization, executor);
        std::vector<int> selected;
        std::vector<int> last_front;
        for (const auto& front : fronts) {
            if (selected.size() + front.size()
                <= static_cast<std::size_t>(config.outer_population)) {
                selected.insert(selected.end(), front.begin(), front.end());
            } else {
                last_front = front;
                break;
            }
        }
        std::vector<int> niche(reference_points().size(), 0);
        for (const int index : selected) {
            ++niche[static_cast<std::size_t>(
                merged[static_cast<std::size_t>(index)].reference
            )];
        }
        std::vector<std::vector<int>> buckets(reference_points().size());
        for (const int index : last_front) {
            buckets[static_cast<std::size_t>(
                merged[static_cast<std::size_t>(index)].reference
            )].push_back(index);
        }
        std::uint64_t step = 0;
        while (selected.size() < static_cast<std::size_t>(config.outer_population)) {
            int reference = -1;
            int minimum = std::numeric_limits<int>::max();
            for (int index = 0; index < static_cast<int>(buckets.size()); ++index) {
                if (buckets[static_cast<std::size_t>(index)].empty()) continue;
                if (niche[static_cast<std::size_t>(index)] < minimum) {
                    minimum = niche[static_cast<std::size_t>(index)];
                    reference = index;
                }
            }
            if (reference < 0) {
                throw std::runtime_error("L0298 NSGA-III niching exhausted");
            }
            auto& bucket = buckets[static_cast<std::size_t>(reference)];
            std::size_t position = 0;
            if (minimum == 0) {
                for (std::size_t candidate = 1; candidate < bucket.size(); ++candidate) {
                    const auto& left = merged[static_cast<std::size_t>(bucket[candidate])];
                    const auto& right = merged[static_cast<std::size_t>(bucket[position])];
                    if (left.reference_distance < right.reference_distance
                        || (left.reference_distance == right.reference_distance
                            && bucket[candidate] < bucket[position])) {
                        position = candidate;
                    }
                }
            } else {
                position = static_cast<std::size_t>(rng.integer(
                    0, static_cast<int>(bucket.size()), generation, 67, step
                ));
            }
            selected.push_back(bucket[position]);
            bucket.erase(bucket.begin() + static_cast<std::ptrdiff_t>(position));
            ++niche[static_cast<std::size_t>(reference)];
            ++step;
        }
        population.clear();
        population.reserve(static_cast<std::size_t>(config.outer_population));
        for (const int index : selected) {
            population.push_back(std::move(merged[static_cast<std::size_t>(index)]));
        }
        assign_rank(population, executor);
        associate(population, normalization, executor);
        algorithm_seconds += elapsed_seconds(sorting_started);
    }
    return population;
}

}  // namespace

RunResult run(const Problem& problem, const RunConfig& config) {
    if (config.workers <= 0 || config.outer_population < 4
        || config.outer_iterations < 0 || config.inner_population < 2
        || config.inner_iterations < 0) {
        throw std::invalid_argument("invalid L0298 run configuration");
    }
    if (config.outer_population != 120 && config.outer_population > 120) {
        throw std::invalid_argument("L0298 population exceeds reference set");
    }
    const auto total_started = Clock::now();
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    RunResult result;
    result.profile_id = problem.id();
    result.seed = config.seed;
    result.requested_workers = config.workers;
    result.outer_population = config.outer_population;
    result.outer_iterations = config.outer_iterations;
    result.inner_population = config.inner_population;
    result.inner_iterations = config.inner_iterations;
    for (std::size_t context_index = 0;
         context_index < problem.impl_->contexts.size(); ++context_index) {
        const Context& context = problem.impl_->contexts[context_index];
        auto population = optimize_context(
            problem, context, config, context_index + 1U, executor,
            result.complete_outer_evaluations,
            result.cable_particle_evaluations,
            result.hourly_wake_evaluations,
            result.wake_and_coupled_evaluator_seconds,
            result.evolutionary_orchestration_seconds
        );
        for (const std::string& role : problem.impl_->roles[context_index]) {
            const int index = select_index(population, role);
            const Genome& selected = population[static_cast<std::size_t>(index)];
            RoleResult receipt;
            receipt.role = role;
            receipt.model = context.model;
            receipt.active_cells = selected.cells;
            std::uint64_t cable_work = 0;
            const auto started = Clock::now();
            receipt.evaluation = problem.evaluate_cells(
                receipt.active_cells, context.model,
                event_seed(config.seed, config.outer_iterations + 1U,
                           static_cast<std::uint64_t>(index), context_index + 1U),
                config.inner_population, config.inner_iterations,
                &receipt.cable_edges, &cable_work
            );
            result.wake_and_coupled_evaluator_seconds += elapsed_seconds(started);
            ++result.complete_outer_evaluations;
            result.hourly_wake_evaluations += 24U;
            result.cable_particle_evaluations += cable_work;
            result.roles.push_back(std::move(receipt));
        }
    }
    const auto executor_receipt = executor.work_receipt();
    result.observed_workers = executor_receipt.distinct_participants;
    result.parallel_regions = executor_receipt.parallel_regions;
    result.end_to_end_seconds = elapsed_seconds(total_started);
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mix_hash(hash, result.complete_outer_evaluations);
    hash = mix_hash(hash, result.cable_particle_evaluations);
    for (const auto& role : result.roles) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
            role.evaluation.profit_rate_percent
        ));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
            role.evaluation.capacity_factor_percent
        ));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(
            role.evaluation.variability_percent
        ));
        for (const int cell : role.active_cells) {
            hash = mix_hash(hash, static_cast<std::uint64_t>(cell));
        }
        for (const auto& edge : role.cable_edges) {
            hash = mix_hash(hash, static_cast<std::uint64_t>(edge.from_turbine));
            hash = mix_hash(hash, static_cast<std::uint64_t>(edge.to_node));
            hash = mix_hash(hash, static_cast<std::uint64_t>(edge.cable_type));
        }
    }
    result.scientific_hash = hash;
    return result;
}

std::vector<ProfileId> paper_profiles() {
    return {
        ProfileId::model_comparison,
        ProfileId::turbine_e115,
        ProfileId::turbine_ltw101,
        ProfileId::summer,
        ProfileId::bus5,
        ProfileId::bus7,
        ProfileId::bus16,
        ProfileId::bus21,
        ProfileId::bus23,
    };
}

std::string to_string(const ProfileId value) {
    switch (value) {
    case ProfileId::model_comparison: return "models-winter-e82-bus3";
    case ProfileId::turbine_e115: return "model1-winter-e115-bus3";
    case ProfileId::turbine_ltw101: return "model1-winter-ltw101-bus3";
    case ProfileId::summer: return "model1-summer-e82-bus3";
    case ProfileId::bus5: return "model1-winter-e82-bus5";
    case ProfileId::bus7: return "model1-winter-e82-bus7";
    case ProfileId::bus16: return "model1-winter-e82-bus16";
    case ProfileId::bus21: return "model1-winter-e82-bus21";
    case ProfileId::bus23: return "model1-winter-e82-bus23";
    }
    throw std::invalid_argument("unknown L0298 profile");
}

}  // namespace core99::l0298
