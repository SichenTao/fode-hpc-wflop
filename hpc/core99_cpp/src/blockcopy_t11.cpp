/*
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T11 pure-C++ BlockCopy search and incremental evaluator
Paper/DOI: BlockCopy-Based Operators for Evolving Efficient Wind Farm
Layouts; 10.1109/CEC.2016.7743909
Public source, missing/conflicting facts and completion policy:
hpc/core99_cpp/include/core99/blockcopy_t11.hpp
Official evaluator lineage: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2 (MIT).
Official 2014 XML SHA-256: competition_1
4fa3a26df31f786949d6afabd0180c26dde11ecfe23be0742ddffd11798c42d4;
competition_3
c5669f54904cd1d76985c0f68bfce5fcfc9e5f842c0c8cb20ec388c693d67b1b.
Method/problem semantic IDs: t11_blockcopy_four_es_methods_v1;
t11_kusiak_and_2014_competition_four_cases_v1
Controlling contract: shared/contracts/core99_t11_blockcopy_2016.json
Independent equation oracle: scripts/validate_core99_t11.py
HPC design: algebraic cone test replaces source acos without changing its
geometry; Weibull/power quadrature is lookup compiled; full direction/turbine
tasks are persistent-team parallel; child layouts update a parent's squared
deficits only for removed and added coordinates.
Claim boundary: source-backed flexible academic reproduction, not author
BlockCopy source, RNG or exact-number replay.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
*/
#include "core99/blockcopy_t11.hpp"

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
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace core99::t11 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kRotorRadiusM = 38.5;
constexpr double kMinimumSpacingM = 8.0 * kRotorRadiusM;
constexpr double kWakeExpansion = 0.075;
constexpr double kThrustCoefficient = 0.8;
constexpr double kDeficitScale =
    1.0 - std::sqrt(1.0 - kThrustCoefficient);
constexpr int kLookupBins = 65536;
constexpr const char* kProblemSemanticId =
    "t11_kusiak_and_2014_competition_four_cases_v1";
constexpr const char* kMethodSemanticId =
    "t11_blockcopy_four_es_methods_v1";

struct WindDirection {
    double scale = 0.0;
    double shape = 0.0;
    double probability_mass = 0.0;
    double cosine = 1.0;
    double sine = 0.0;
};

struct Obstacle {
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
};

double seconds_since(const Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

double weibull_cdf(
    const double speed,
    const double scale,
    const double shape
) {
    if (!(speed > 0.0) || !(scale > 0.0) || !(shape > 0.0)) return 0.0;
    return 1.0 - std::exp(-std::pow(speed / scale, shape));
}

double turbine_power_kw(const double speed) {
    if (speed < 3.5) return 0.0;
    if (speed <= 14.0) return 140.86 * speed - 500.0;
    if (speed < 20.0) return 1500.0;
    return 0.0;
}

double expected_power_kw(
    const double scale,
    const double shape
) {
    if (!(scale > 0.0)) return 0.0;
    double total = 0.0;
    for (int interval = 0; interval < 21; ++interval) {
        const double lower = 3.5 + 0.5 * static_cast<double>(interval);
        const double upper = lower + 0.5;
        const double midpoint = 0.5 * (lower + upper);
        total += (
            weibull_cdf(upper, scale, shape)
            - weibull_cdf(lower, scale, shape)
        ) * turbine_power_kw(midpoint);
    }
    // This deliberately follows the released evaluator: the rated-power
    // tail is integrated from 14 m/s to infinity, not truncated at 20 m/s.
    total += 1500.0 * (1.0 - weibull_cdf(14.0, scale, shape));
    return total;
}

std::uint64_t mix_hash(std::uint64_t hash, const std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t point_key(const Point& point) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
    return mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
}

bool same_point(const Point& left, const Point& right) {
    return left.x_m == right.x_m && left.y_m == right.y_m;
}

double squared_distance(const Point& left, const Point& right) {
    const double dx = left.x_m - right.x_m;
    const double dy = left.y_m - right.y_m;
    return dx * dx + dy * dy;
}

bool better(
    const Evaluation& left,
    const std::vector<Point>& left_layout,
    const Evaluation& right,
    const std::vector<Point>& right_layout
) {
    if (left.feasible != right.feasible) return left.feasible;
    if (
        left.constraint_violation_m
        != right.constraint_violation_m
    ) {
        return left.constraint_violation_m
            < right.constraint_violation_m;
    }
    if (left.energy_cost != right.energy_cost) {
        return left.energy_cost < right.energy_cost;
    }
    for (std::size_t index = 0; index < left_layout.size(); ++index) {
        if (left_layout[index].x_m != right_layout[index].x_m) {
            return left_layout[index].x_m < right_layout[index].x_m;
        }
        if (left_layout[index].y_m != right_layout[index].y_m) {
            return left_layout[index].y_m < right_layout[index].y_m;
        }
    }
    return false;
}

std::uint64_t result_hash(
    const std::string& algorithm,
    const std::vector<Point>& layout,
    const Evaluation& evaluation
) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char value : algorithm) {
        hash = mix_hash(hash, value);
    }
    hash = mix_hash(
        hash,
        std::bit_cast<std::uint64_t>(evaluation.energy_cost)
    );
    for (const Point& point : layout) {
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.x_m));
        hash = mix_hash(hash, std::bit_cast<std::uint64_t>(point.y_m));
    }
    return hash;
}

struct Transition {
    std::vector<int> child_to_parent;
    std::vector<int> removed_parent;
    std::vector<Point> added;
};

Transition transition_between(
    const std::vector<Point>& parent,
    const std::vector<Point>& child
) {
    std::unordered_map<std::uint64_t, std::vector<int>> parent_indices;
    parent_indices.reserve(parent.size() * 2U);
    for (std::size_t index = 0; index < parent.size(); ++index) {
        parent_indices[point_key(parent[index])].push_back(
            static_cast<int>(index)
        );
    }
    std::vector<bool> consumed(parent.size(), false);
    Transition result;
    result.child_to_parent.assign(child.size(), -1);
    for (std::size_t child_index = 0; child_index < child.size(); ++child_index) {
        const auto found = parent_indices.find(point_key(child[child_index]));
        if (found == parent_indices.end()) {
            result.added.push_back(child[child_index]);
            continue;
        }
        int matched = -1;
        for (const int candidate : found->second) {
            if (
                !consumed[static_cast<std::size_t>(candidate)]
                && same_point(
                    parent[static_cast<std::size_t>(candidate)],
                    child[child_index]
                )
            ) {
                matched = candidate;
                break;
            }
        }
        if (matched < 0) {
            result.added.push_back(child[child_index]);
        } else {
            result.child_to_parent[child_index] = matched;
            consumed[static_cast<std::size_t>(matched)] = true;
        }
    }
    for (std::size_t index = 0; index < consumed.size(); ++index) {
        if (!consumed[index]) {
            result.removed_parent.push_back(static_cast<int>(index));
        }
    }
    return result;
}

class FeasibleSet {
public:
    explicit FeasibleSet(const double cell_size) : cell_size_(cell_size) {}

    void insert(const Point& point) {
        cells_[key(cell(point))].push_back(point);
    }

    bool spacing_valid(const Point& point) const {
        const auto [cx, cy] = cell(point);
        const double minimum_squared =
            kMinimumSpacingM * kMinimumSpacingM;
        for (int x = cx - 1; x <= cx + 1; ++x) {
            for (int y = cy - 1; y <= cy + 1; ++y) {
                const auto found = cells_.find(key({x, y}));
                if (found == cells_.end()) continue;
                for (const Point& other : found->second) {
                    if (squared_distance(point, other) < minimum_squared) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

private:
    double cell_size_;
    std::unordered_map<std::uint64_t, std::vector<Point>> cells_;

    std::pair<int, int> cell(const Point& point) const {
        return {
            static_cast<int>(std::floor(point.x_m / cell_size_)),
            static_cast<int>(std::floor(point.y_m / cell_size_))
        };
    }

    static std::uint64_t key(const std::pair<int, int> value) {
        return (
            static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(value.first)
            ) << 32U
        ) | static_cast<std::uint32_t>(value.second);
    }
};

}  // namespace

struct Problem::Impl {
    std::string id;
    int turbines = 0;
    int block_columns = 0;
    int block_rows = 0;
    double width = 0.0;
    double height = 0.0;
    double wake_free_energy = 0.0;
    std::vector<WindDirection> wind;
    std::vector<Obstacle> obstacles;
    std::vector<double> power_lookup;

    [[nodiscard]] double wake_contribution(
        const Point& source,
        const Point& target,
        const int direction
    ) const {
        if (same_point(source, target)) return 0.0;
        const WindDirection& wind_direction =
            wind[static_cast<std::size_t>(direction)];
        const double dx = target.x_m - source.x_m;
        const double dy = target.y_m - source.y_m;
        const double downstream =
            dx * wind_direction.cosine + dy * wind_direction.sine;
        const double crosswind =
            -dx * wind_direction.sine + dy * wind_direction.cosine;
        const double cone_radius =
            kRotorRadiusM + kWakeExpansion * downstream;
        if (
            !(cone_radius > 0.0)
            || std::abs(crosswind) >= cone_radius
        ) {
            return 0.0;
        }
        const double projected = std::abs(downstream);
        const double denominator =
            1.0 + (kWakeExpansion / kRotorRadiusM) * projected;
        const double deficit =
            kDeficitScale / (denominator * denominator);
        return deficit * deficit;
    }

    [[nodiscard]] double power_from_q(
        const int direction,
        const double squared_deficit
    ) const {
        if (!(squared_deficit < 1.0)) return 0.0;
        const double position =
            std::max(0.0, squared_deficit)
            * static_cast<double>(kLookupBins);
        const int low = static_cast<int>(position);
        const int high = std::min(low + 1, kLookupBins);
        const double fraction = position - static_cast<double>(low);
        const std::size_t offset =
            static_cast<std::size_t>(direction * (kLookupBins + 1));
        return std::lerp(
            power_lookup[offset + static_cast<std::size_t>(low)],
            power_lookup[offset + static_cast<std::size_t>(high)],
            fraction
        );
    }

    [[nodiscard]] Evaluation finish(
        const std::vector<double>& squared_deficits,
        const double violation
    ) const {
        Evaluation result;
        result.constraint_violation_m = violation;
        result.feasible = violation <= 1.0e-10;
        if (!result.feasible) {
            result.energy_cost = std::numeric_limits<double>::infinity();
            return result;
        }
        for (int direction = 0;
             direction < static_cast<int>(wind.size());
             ++direction) {
            for (int turbine = 0; turbine < turbines; ++turbine) {
                result.energy_output_kw +=
                    wind[static_cast<std::size_t>(direction)].probability_mass
                    * power_from_q(
                        direction,
                        squared_deficits[
                            static_cast<std::size_t>(
                                direction * turbines + turbine
                            )
                        ]
                    );
            }
        }
        result.wake_free_ratio =
            result.energy_output_kw
            / (wake_free_energy * static_cast<double>(turbines));
        const double count = static_cast<double>(turbines);
        const double capital = (
            750000.0 * count
            + 8000000.0 * std::floor(count / 30.0)
        ) * (
            2.0 / 3.0
            + std::exp(-0.00174 * count * count) / 3.0
        ) + 20000.0 * count;
        const double annuity =
            (1.0 - std::pow(1.03, -20.0)) / 0.03;
        result.energy_cost =
            capital / annuity / (8760.0 * result.energy_output_kw)
            + 0.1 / count;
        return result;
    }
};

namespace {

void append_direction(
    Problem::Impl& impl,
    const double degrees,
    const double scale,
    const double shape,
    const double probability_mass
) {
    const double radians = degrees * std::numbers::pi / 180.0;
    impl.wind.push_back({
        scale,
        shape,
        probability_mass,
        std::cos(radians),
        std::sin(radians)
    });
}

void add_kusiak_wind(Problem::Impl& impl, const int scenario) {
    constexpr std::array<double, 24> scenario1_probability{
        0.0, 0.01, 0.01, 0.01, 0.01, 0.20,
        0.60, 0.01, 0.01, 0.01, 0.01, 0.01,
        0.01, 0.01, 0.01, 0.01, 0.01, 0.01,
        0.01, 0.01, 0.01, 0.01, 0.01, 0.0
    };
    constexpr std::array<double, 24> scenario2_scale{
        7.0, 5.0, 5.0, 5.0, 5.0, 4.0,
        5.0, 6.0, 7.0, 7.0, 8.0, 9.5,
        10.0, 8.5, 8.5, 6.5, 4.6, 2.6,
        8.0, 5.0, 6.4, 5.2, 4.5, 3.9
    };
    constexpr std::array<double, 24> scenario2_probability{
        0.0002, 0.0080, 0.0227, 0.0242, 0.0225, 0.0339,
        0.0423, 0.0290, 0.0617, 0.0813, 0.0994, 0.1394,
        0.1839, 0.1115, 0.0765, 0.0080, 0.0051, 0.0019,
        0.0012, 0.0010, 0.0017, 0.0031, 0.0097, 0.0317
    };
    for (int direction = 0; direction < 24; ++direction) {
        append_direction(
            impl,
            7.5 + 15.0 * static_cast<double>(direction),
            scenario == 1 ? 13.0
                          : scenario2_scale[static_cast<std::size_t>(direction)],
            2.0,
            scenario == 1
                ? scenario1_probability[static_cast<std::size_t>(direction)]
                : scenario2_probability[static_cast<std::size_t>(direction)]
        );
    }
}

void add_competition_1(Problem::Impl& impl) {
    constexpr std::array<double, 24> scale{
        10.443094,11.497257,9.936782,11.960420,9.646003,7.850565,
        10.391076,11.689212,11.306598,10.067243,11.929232,7.213661,
        9.510614,8.435315,7.209497,11.928993,9.096070,8.920670,
        11.822872,9.433660,10.287406,8.840926,8.965424,11.006392
    };
    constexpr std::array<double, 24> shape{
        2.824893,4.473140,2.668569,4.041643,3.110461,2.667998,
        4.131375,4.355522,2.406574,3.788350,3.252150,3.985223,
        2.969414,3.979477,2.390965,3.563812,3.474557,4.451840,
        2.016430,2.424834,4.199515,3.289647,3.409203,4.241142
    };
    constexpr std::array<double, 24> density{
        .115109,.109885,.103791,.096939,.089457,.081482,
        .073164,.064654,.056111,.047693,.039556,.031850,
        .024717,.018290,.012687,.008012,.004352,.001775,
        .000327,.000036,.000908,.002925,.006051,.010228
    };
    for (int direction = 0; direction < 24; ++direction) {
        append_direction(
            impl,
            7.5 + 15.0 * static_cast<double>(direction),
            scale[static_cast<std::size_t>(direction)],
            shape[static_cast<std::size_t>(direction)],
            15.0 * density[static_cast<std::size_t>(direction)]
        );
    }
    impl.obstacles.push_back({1500.0,4000.0,1750.0,12000.0});
}

void add_competition_3(Problem::Impl& impl) {
    constexpr std::array<double, 24> scale{
        6.620156,7.555507,7.877089,7.783426,7.204585,6.550654,
        7.132810,8.154736,6.852388,8.681447,8.113125,8.997957,
        7.287286,7.660273,6.514132,6.528559,6.804044,6.763974,
        7.002693,8.238606,7.385678,8.075094,8.842956,8.346507
    };
    constexpr std::array<double, 24> shape{
        3.275544,2.507746,3.454448,3.436239,2.259019,2.930746,
        3.359333,2.187553,2.672880,3.623705,3.490578,3.427143,
        2.841829,2.346667,2.399485,2.918861,3.263791,2.191254,
        2.632711,2.366451,3.049989,3.068045,2.876107,2.723683
    };
    constexpr std::array<double, 24> density{
        .055156,.063460,.070582,.076149,.079871,.081553,
        .081107,.078558,.074037,.067781,.060117,.051445,
        .042218,.032919,.024032,.016022,.009307,.004239,
        .001082,.000000,.001051,.004179,.009221,.015914
    };
    for (int direction = 0; direction < 24; ++direction) {
        append_direction(
            impl,
            7.5 + 15.0 * static_cast<double>(direction),
            scale[static_cast<std::size_t>(direction)],
            shape[static_cast<std::size_t>(direction)],
            15.0 * density[static_cast<std::size_t>(direction)]
        );
    }
    impl.obstacles.push_back({2000.0,2000.0,5000.0,4000.0});
    impl.obstacles.push_back({7000.0,6000.0,8000.0,6500.0});
    impl.obstacles.push_back({10000.0,9000.0,12000.0,10000.0});
}

std::vector<Point> random_layout(
    const Problem& problem,
    const fode::CounterRng& rng,
    const std::uint64_t event
) {
    std::vector<Point> result;
    result.reserve(static_cast<std::size_t>(problem.turbine_count()));
    FeasibleSet grid(problem.minimum_spacing_m());
    std::uint64_t draw = 0;
    while (static_cast<int>(result.size()) < problem.turbine_count()) {
        if (draw > 10000000ULL) {
            throw std::runtime_error("T11 feasible initialization exhausted");
        }
        const Point candidate{
            problem.width_m() * rng.uniform(event, 11, draw, 0),
            problem.height_m() * rng.uniform(event, 11, draw, 1)
        };
        ++draw;
        if (!problem.valid_point(candidate) || !grid.spacing_valid(candidate)) {
            continue;
        }
        grid.insert(candidate);
        result.push_back(candidate);
    }
    return result;
}

int point_block(
    const Problem& problem,
    const Point& point
) {
    const int column = std::clamp(
        static_cast<int>(
            point.x_m / (
                problem.width_m()
                / static_cast<double>(problem.block_columns())
            )
        ),
        0,
        problem.block_columns() - 1
    );
    const int row = std::clamp(
        static_cast<int>(
            point.y_m / (
                problem.height_m()
                / static_cast<double>(problem.block_rows())
            )
        ),
        0,
        problem.block_rows() - 1
    );
    return row * problem.block_columns() + column;
}

Point random_valid_addition(
    const Problem& problem,
    FeasibleSet& grid,
    const fode::CounterRng& rng,
    const std::uint64_t event,
    const std::uint64_t phase,
    std::uint64_t& draw
) {
    for (std::uint64_t attempt = 0; attempt < 1000000ULL; ++attempt) {
        const Point point{
            problem.width_m()
                * rng.uniform(event, phase, draw, 0, attempt),
            problem.height_m()
                * rng.uniform(event, phase, draw, 1, attempt)
        };
        ++draw;
        if (problem.valid_point(point) && grid.spacing_valid(point)) {
            grid.insert(point);
            return point;
        }
    }
    throw std::runtime_error("T11 count repair exhausted");
}

std::vector<Point> blockcopy(
    const Problem& problem,
    const std::vector<Point>& base,
    const std::vector<Point>& donor,
    const fode::CounterRng& rng,
    const std::uint64_t event,
    const bool mutation
) {
    const int blocks = problem.block_columns() * problem.block_rows();
    int source = rng.integer(0, blocks, event, 21, 0);
    int target = rng.integer(0, blocks, event, 21, 1);
    if (mutation && blocks > 1 && target == source) {
        target = (target + 1 + rng.integer(
            0, blocks - 1, event, 21, 2
        )) % blocks;
    }
    std::vector<Point> result;
    result.reserve(base.size() + donor.size() / static_cast<std::size_t>(blocks));
    for (const Point& point : base) {
        if (point_block(problem, point) != target) result.push_back(point);
    }
    FeasibleSet grid(problem.minimum_spacing_m());
    for (const Point& point : result) grid.insert(point);
    const int source_column = source % problem.block_columns();
    const int source_row = source / problem.block_columns();
    const int target_column = target % problem.block_columns();
    const int target_row = target / problem.block_columns();
    const double dx =
        static_cast<double>(target_column - source_column)
        * problem.width_m()
        / static_cast<double>(problem.block_columns());
    const double dy =
        static_cast<double>(target_row - source_row)
        * problem.height_m()
        / static_cast<double>(problem.block_rows());
    for (const Point& point : donor) {
        if (point_block(problem, point) != source) continue;
        const Point copied{point.x_m + dx, point.y_m + dy};
        // Paper authority: an invalid copied turbine is not placed.
        if (
            problem.valid_point(copied)
            && grid.spacing_valid(copied)
        ) {
            grid.insert(copied);
            result.push_back(copied);
        }
    }
    std::uint64_t draw = 0;
    while (result.size() > base.size()) {
        const int selected = rng.integer(
            0,
            static_cast<int>(result.size()),
            event,
            22,
            draw++
        );
        result.erase(result.begin() + selected);
    }
    if (result.size() < base.size()) {
        grid = FeasibleSet(problem.minimum_spacing_m());
        for (const Point& point : result) grid.insert(point);
        while (result.size() < base.size()) {
            result.push_back(random_valid_addition(
                problem, grid, rng, event, 23, draw
            ));
        }
    }
    return result;
}

std::vector<Point> perturb_ten(
    const Problem& problem,
    const std::vector<Point>& parent,
    const fode::CounterRng& rng,
    const std::uint64_t event
) {
    std::vector<int> indices(parent.size());
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = static_cast<int>(index);
    }
    for (int slot = 0; slot < 10; ++slot) {
        const int selected = rng.integer(
            slot,
            static_cast<int>(indices.size()),
            event,
            31,
            static_cast<std::uint64_t>(slot)
        );
        std::swap(
            indices[static_cast<std::size_t>(slot)],
            indices[static_cast<std::size_t>(selected)]
        );
    }
    std::vector<bool> removed(parent.size(), false);
    for (int slot = 0; slot < 10; ++slot) {
        removed[static_cast<std::size_t>(
            indices[static_cast<std::size_t>(slot)]
        )] = true;
    }
    std::vector<Point> result;
    result.reserve(parent.size());
    FeasibleSet grid(problem.minimum_spacing_m());
    for (std::size_t index = 0; index < parent.size(); ++index) {
        if (!removed[index]) {
            result.push_back(parent[index]);
            grid.insert(parent[index]);
        }
    }
    std::uint64_t draw = 0;
    while (result.size() < parent.size()) {
        result.push_back(random_valid_addition(
            problem, grid, rng, event, 32, draw
        ));
    }
    return result;
}

}  // namespace

Problem::Problem(const std::string& problem_id)
    : impl_(std::make_unique<Impl>()) {
    impl_->id = problem_id;
    if (problem_id == "t11_ks1_n100") {
        impl_->turbines = 100;
        impl_->block_columns = 4;
        impl_->block_rows = 4;
        impl_->width = 4000.0;
        impl_->height = 4000.0;
        add_kusiak_wind(*impl_, 1);
    } else if (problem_id == "t11_ks2_n100") {
        impl_->turbines = 100;
        impl_->block_columns = 4;
        impl_->block_rows = 4;
        impl_->width = 4000.0;
        impl_->height = 4000.0;
        add_kusiak_wind(*impl_, 2);
    } else if (problem_id == "t11_comp1_n220") {
        impl_->turbines = 220;
        impl_->block_columns = 3;
        impl_->block_rows = 16;
        impl_->width = 3500.0;
        impl_->height = 16100.0;
        impl_->wake_free_energy = 11963.514;
        add_competition_1(*impl_);
    } else if (problem_id == "t11_comp3_n710") {
        impl_->turbines = 710;
        impl_->block_columns = 16;
        impl_->block_rows = 11;
        impl_->width = 15800.0;
        impl_->height = 11300.0;
        impl_->wake_free_energy = 6965.442;
        add_competition_3(*impl_);
    } else {
        throw std::invalid_argument("unknown T11 problem: " + problem_id);
    }
    impl_->power_lookup.resize(
        impl_->wind.size() * static_cast<std::size_t>(kLookupBins + 1)
    );
    for (std::size_t direction = 0; direction < impl_->wind.size(); ++direction) {
        const WindDirection& wind = impl_->wind[direction];
        for (int bin = 0; bin <= kLookupBins; ++bin) {
            const double q =
                static_cast<double>(bin) / static_cast<double>(kLookupBins);
            const double retained = std::max(0.0, 1.0 - std::sqrt(q));
            impl_->power_lookup[
                direction * static_cast<std::size_t>(kLookupBins + 1)
                + static_cast<std::size_t>(bin)
            ] = expected_power_kw(wind.scale * retained, wind.shape);
        }
    }
    if (!(impl_->wake_free_energy > 0.0)) {
        for (const WindDirection& wind : impl_->wind) {
            impl_->wake_free_energy +=
                wind.probability_mass
                * expected_power_kw(wind.scale, wind.shape);
        }
    }
}

Problem::~Problem() = default;
Problem::Problem(Problem&&) noexcept = default;
Problem& Problem::operator=(Problem&&) noexcept = default;

const std::string& Problem::id() const noexcept { return impl_->id; }
int Problem::turbine_count() const noexcept { return impl_->turbines; }
int Problem::direction_count() const noexcept {
    return static_cast<int>(impl_->wind.size());
}
int Problem::block_columns() const noexcept { return impl_->block_columns; }
int Problem::block_rows() const noexcept { return impl_->block_rows; }
double Problem::width_m() const noexcept { return impl_->width; }
double Problem::height_m() const noexcept { return impl_->height; }
double Problem::minimum_spacing_m() const noexcept {
    return kMinimumSpacingM;
}

bool Problem::valid_point(const Point& point) const noexcept {
    if (
        point.x_m < 0.0 || point.x_m > impl_->width
        || point.y_m < 0.0 || point.y_m > impl_->height
    ) return false;
    for (const Obstacle& obstacle : impl_->obstacles) {
        if (
            point.x_m > obstacle.xmin && point.x_m < obstacle.xmax
            && point.y_m > obstacle.ymin && point.y_m < obstacle.ymax
        ) return false;
    }
    return true;
}

double Problem::constraint_violation(
    const std::vector<Point>& layout
) const {
    if (static_cast<int>(layout.size()) != impl_->turbines) {
        return std::numeric_limits<double>::infinity();
    }
    double violation = 0.0;
    for (const Point& point : layout) {
        violation += std::max(0.0, -point.x_m);
        violation += std::max(0.0, point.x_m - impl_->width);
        violation += std::max(0.0, -point.y_m);
        violation += std::max(0.0, point.y_m - impl_->height);
        for (const Obstacle& obstacle : impl_->obstacles) {
            if (
                point.x_m > obstacle.xmin && point.x_m < obstacle.xmax
                && point.y_m > obstacle.ymin && point.y_m < obstacle.ymax
            ) {
                violation += std::min({
                    point.x_m - obstacle.xmin,
                    obstacle.xmax - point.x_m,
                    point.y_m - obstacle.ymin,
                    obstacle.ymax - point.y_m
                });
            }
        }
    }
    for (std::size_t first = 0; first < layout.size(); ++first) {
        for (std::size_t second = first + 1; second < layout.size(); ++second) {
            violation += std::max(
                0.0,
                kMinimumSpacingM
                    - std::sqrt(squared_distance(
                        layout[first], layout[second]
                    ))
            );
        }
    }
    return violation;
}

std::unique_ptr<Problem::State> Problem::make_state(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    auto state = std::make_unique<State>();
    state->layout = layout;
    const int tasks = direction_count() * turbine_count();
    state->squared_deficits.assign(static_cast<std::size_t>(tasks), 0.0);
    const int partitions = std::min(executor.thread_count(), tasks);
    executor.parallel_for(0, partitions, [&](const int partition) {
        for (int task = partition; task < tasks; task += partitions) {
            const int direction = task / turbine_count();
            const int target = task % turbine_count();
            double sum = 0.0;
            for (int source = 0; source < turbine_count(); ++source) {
                if (source == target) continue;
                sum += impl_->wake_contribution(
                    layout[static_cast<std::size_t>(source)],
                    layout[static_cast<std::size_t>(target)],
                    direction
                );
            }
            state->squared_deficits[static_cast<std::size_t>(task)] = sum;
        }
    });
    state->evaluation = impl_->finish(
        state->squared_deficits,
        constraint_violation(layout)
    );
    return state;
}

std::unique_ptr<Problem::State> Problem::update_state(
    const State& parent,
    const std::vector<Point>& child,
    fode::PersistentExecutor& executor
) const {
    if (child.size() != parent.layout.size()) {
        throw std::invalid_argument("T11 incremental layout count drift");
    }
    const Transition transition = transition_between(parent.layout, child);
    auto state = std::make_unique<State>();
    state->layout = child;
    const int tasks = direction_count() * turbine_count();
    state->squared_deficits.assign(static_cast<std::size_t>(tasks), 0.0);
    const int partitions = std::min(executor.thread_count(), tasks);
    executor.parallel_for(0, partitions, [&](const int partition) {
        for (int task = partition; task < tasks; task += partitions) {
            const int direction = task / turbine_count();
            const int target = task % turbine_count();
            const int parent_target =
                transition.child_to_parent[static_cast<std::size_t>(target)];
            double sum = 0.0;
            if (parent_target >= 0) {
                sum = parent.squared_deficits[
                    static_cast<std::size_t>(
                        direction * turbine_count() + parent_target
                    )
                ];
                for (const int removed : transition.removed_parent) {
                    if (removed == parent_target) continue;
                    sum -= impl_->wake_contribution(
                        parent.layout[static_cast<std::size_t>(removed)],
                        child[static_cast<std::size_t>(target)],
                        direction
                    );
                }
                for (const Point& added : transition.added) {
                    if (
                        same_point(
                            added,
                            child[static_cast<std::size_t>(target)]
                        )
                    ) {
                        continue;
                    }
                    sum += impl_->wake_contribution(
                        added,
                        child[static_cast<std::size_t>(target)],
                        direction
                    );
                }
            } else {
                for (int source = 0; source < turbine_count(); ++source) {
                    if (source == target) continue;
                    sum += impl_->wake_contribution(
                        child[static_cast<std::size_t>(source)],
                        child[static_cast<std::size_t>(target)],
                        direction
                    );
                }
            }
            state->squared_deficits[static_cast<std::size_t>(task)] =
                std::max(0.0, sum);
        }
    });
    state->evaluation = impl_->finish(
        state->squared_deficits,
        constraint_violation(child)
    );
    return state;
}

Evaluation Problem::evaluate_full(
    const std::vector<Point>& layout
) const {
    fode::PersistentExecutor executor(1);
    return make_state(layout, executor)->evaluation;
}

Evaluation Problem::evaluate_parallel(
    const std::vector<Point>& layout,
    fode::PersistentExecutor& executor
) const {
    return make_state(layout, executor)->evaluation;
}

const std::vector<Point>& Problem::state_layout(
    const State& state
) const noexcept {
    return state.layout;
}

const Evaluation& Problem::state_evaluation(
    const State& state
) const noexcept {
    return state.evaluation;
}

std::vector<Point> Problem::random_feasible_layout(
    const std::uint64_t seed,
    const std::uint64_t event
) const {
    return random_layout(*this, fode::CounterRng(seed), event);
}

std::vector<std::string> paper_problem_ids() {
    return {
        "t11_ks1_n100",
        "t11_ks2_n100",
        "t11_comp1_n220",
        "t11_comp3_n710"
    };
}

std::vector<std::string> paper_algorithm_ids() {
    return {
        "t11_1plus1_blockcopy_mutation",
        "t11_1plus1_blockcopy_both",
        "t11_5comma10_blockcopy_mutation",
        "t11_5comma10_blockcopy_crossover"
    };
}

RunResult run(
    const Problem& problem,
    const RunConfig& config
) {
    const std::vector<std::string> admitted_algorithms =
        paper_algorithm_ids();
    if (
        std::find(
            admitted_algorithms.begin(),
            admitted_algorithms.end(),
            config.algorithm_id
        ) == admitted_algorithms.end()
    ) {
        throw std::invalid_argument("unknown T11 algorithm");
    }
    const bool population =
        config.algorithm_id.starts_with("t11_5comma10_");
    if (
        config.physical_fes < static_cast<std::uint64_t>(
            population ? 5 : 1
        )
        || config.workers <= 0
    ) {
        throw std::invalid_argument("invalid T11 run configuration");
    }
    const auto total_start = Clock::now();
    fode::CounterRng rng(config.seed);
    fode::PersistentExecutor executor(config.workers);
    executor.reset_work_receipt();
    double evaluator_seconds = 0.0;
    std::uint64_t fes = 0;
    std::uint64_t accepted = 0;

    std::vector<std::unique_ptr<Problem::State>> parents;
    const int initial_count = population ? 5 : 1;
    parents.reserve(static_cast<std::size_t>(initial_count));
    for (int index = 0; index < initial_count; ++index) {
        const auto layout = problem.random_feasible_layout(
            config.seed,
            static_cast<std::uint64_t>(index)
        );
        const auto start = Clock::now();
        parents.push_back(problem.make_state(layout, executor));
        evaluator_seconds += seconds_since(start);
        ++fes;
    }
    double initial_cost = parents.front()->evaluation.energy_cost;
    for (const auto& parent : parents) {
        initial_cost = std::min(initial_cost, parent->evaluation.energy_cost);
    }

    if (!population) {
        while (fes < config.physical_fes) {
            std::vector<Point> child;
            if (
                config.algorithm_id == "t11_1plus1_blockcopy_both"
                && rng.uniform(fes, 41, 0) < 0.5
            ) {
                child = perturb_ten(
                    problem, parents[0]->layout, rng, fes
                );
            } else {
                child = blockcopy(
                    problem,
                    parents[0]->layout,
                    parents[0]->layout,
                    rng,
                    fes,
                    true
                );
            }
            const auto start = Clock::now();
            auto candidate = problem.update_state(
                *parents[0], child, executor
            );
            evaluator_seconds += seconds_since(start);
            ++fes;
            if (
                !better(
                    parents[0]->evaluation,
                    parents[0]->layout,
                    candidate->evaluation,
                    candidate->layout
                )
            ) {
                parents[0] = std::move(candidate);
                ++accepted;
            }
        }
    } else {
        while (fes < config.physical_fes) {
            const int offspring_count = static_cast<int>(
                std::min<std::uint64_t>(10, config.physical_fes - fes)
            );
            std::vector<std::unique_ptr<Problem::State>> offspring;
            offspring.reserve(static_cast<std::size_t>(offspring_count));
            for (int child_index = 0;
                 child_index < offspring_count;
                 ++child_index) {
                const int base_index = rng.integer(
                    0, 5, fes, 51, child_index, 0
                );
                std::vector<Point> child;
                if (
                    config.algorithm_id
                    == "t11_5comma10_blockcopy_crossover"
                ) {
                    int donor_index = rng.integer(
                        0, 4, fes, 51, child_index, 1
                    );
                    if (donor_index >= base_index) ++donor_index;
                    child = blockcopy(
                        problem,
                        parents[static_cast<std::size_t>(base_index)]->layout,
                        parents[static_cast<std::size_t>(donor_index)]->layout,
                        rng,
                        fes + static_cast<std::uint64_t>(child_index),
                        false
                    );
                } else {
                    child = blockcopy(
                        problem,
                        parents[static_cast<std::size_t>(base_index)]->layout,
                        parents[static_cast<std::size_t>(base_index)]->layout,
                        rng,
                        fes + static_cast<std::uint64_t>(child_index),
                        true
                    );
                }
                const auto start = Clock::now();
                offspring.push_back(problem.update_state(
                    *parents[static_cast<std::size_t>(base_index)],
                    child,
                    executor
                ));
                evaluator_seconds += seconds_since(start);
            }
            fes += static_cast<std::uint64_t>(offspring_count);
            std::stable_sort(
                offspring.begin(),
                offspring.end(),
                [](const auto& left, const auto& right) {
                    return better(
                        left->evaluation,
                        left->layout,
                        right->evaluation,
                        right->layout
                    );
                }
            );
            if (offspring.size() < 5U) {
                for (auto& parent : parents) {
                    offspring.push_back(std::move(parent));
                }
                std::stable_sort(
                    offspring.begin(),
                    offspring.end(),
                    [](const auto& left, const auto& right) {
                        return better(
                            left->evaluation,
                            left->layout,
                            right->evaluation,
                            right->layout
                        );
                    }
                );
            }
            parents.clear();
            for (int index = 0; index < 5; ++index) {
                parents.push_back(std::move(
                    offspring[static_cast<std::size_t>(index)]
                ));
                ++accepted;
            }
        }
    }
    const auto best = std::min_element(
        parents.begin(),
        parents.end(),
        [](const auto& left, const auto& right) {
            return better(
                left->evaluation,
                left->layout,
                right->evaluation,
                right->layout
            );
        }
    );
    const double end_to_end = seconds_since(total_start);
    const auto receipt = executor.work_receipt();
    RunResult result;
    result.problem_id = problem.id();
    result.algorithm_id = config.algorithm_id;
    result.problem_semantic_id = kProblemSemanticId;
    result.method_semantic_id = kMethodSemanticId;
    result.seed = config.seed;
    result.physical_fes = fes;
    result.requested_workers = config.workers;
    result.observed_workers = receipt.distinct_participants;
    result.accepted_offspring = accepted;
    result.initial_energy_cost = initial_cost;
    result.evaluator_seconds = evaluator_seconds;
    result.algorithm_seconds =
        std::max(0.0, end_to_end - evaluator_seconds);
    result.end_to_end_seconds = end_to_end;
    result.final_evaluation = (*best)->evaluation;
    result.final_layout = (*best)->layout;
    result.scientific_hash = result_hash(
        config.algorithm_id,
        result.final_layout,
        result.final_evaluation
    );
    return result;
}

}  // namespace core99::t11
